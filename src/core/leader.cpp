// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// LEADER: an arrow, a path, and a note. See entities.hpp for why this is an
// entity of its own on R13's model rather than a DimKind on R12's.

#include "annotation.hpp"

#include "ncad/dxf.hpp"
#include "ncad/ecs.hpp"
#include "ncad/entities.hpp"
#include "ncad/render.hpp"

#include <cmath>
#include <memory>
#include <vector>

namespace ncad {
namespace {

// How far off horizontal the last segment must run before a hook is worth
// adding, in radians -- about fifteen degrees.
//
// CHOSEN, not taken from the R12 manual. R12 appends the shoulder itself rather
// than making you draw it, and the threshold it used is not established here;
// see SF_todo. What the guard is for is real either way: a hook collinear with
// the segment it extends is not a shoulder, it is a longer segment.
constexpr double kHookThreshold = 0.26;

// The shoulder's length, as a multiple of the text height. A landing about as
// long as the text is tall is what reads as a shoulder rather than a stub.
constexpr double kHookLengths = 1.0;

}  // namespace

void Leader::apply_style(double text_height, double arrow_size) {
    // Guarded rather than trusted, as Dimension::apply_style is and for the
    // same reason: both are reachable from SETVAR, which does not range-check.
    if (text_height > 0.0) text_height_ = text_height;
    if (arrow_size > 0.0) arrow_size_ = arrow_size;
}

bool Leader::has_hook() const {
    if (vertices_.size() < 2) return false;

    const Basis b = arbitrary_axis(props().normal);
    const Vec3 last = vertices_.back() - vertices_[vertices_.size() - 2];
    if (is_zero(last)) return false;

    // The angle the last segment makes with the plane's own horizontal, folded
    // into a quarter turn so that a segment running right-to-left counts as
    // horizontal too.
    const double a = std::atan2(dot(last, b.ay), dot(last, b.ax));
    const double off = std::abs(std::abs(std::abs(a) - kFullTurn * 0.5) - kFullTurn * 0.5);
    return off > kHookThreshold;
}

Vec3 Leader::hook_start() const {
    return vertices_.empty() ? Vec3{} : vertices_.back();
}

Vec3 Leader::hook_end() const {
    if (vertices_.empty()) return Vec3{};
    if (!has_hook()) return vertices_.back();

    const Basis b = arbitrary_axis(props().normal);
    const Vec3 last = vertices_.back() - vertices_[vertices_.size() - 2];

    // The shoulder continues the way the path was already going, so a leader
    // coming in from the left lands pointing right and its note sits to the
    // right of it. Ties go right, which is the common case.
    const double sign = dot(last, b.ax) < 0.0 ? -1.0 : 1.0;
    return vertices_.back() + b.ax * (sign * kHookLengths * text_height_);
}

bool Leader::annotation_on_left() const {
    if (vertices_.size() < 2) return false;
    const Basis b = arbitrary_axis(props().normal);
    // Which way the shoulder points is which way the path was already going,
    // and the note follows the shoulder.
    return dot(vertices_.back() - vertices_[vertices_.size() - 2], b.ax) < 0.0;
}

Vec3 Leader::annotation_origin() const {
    if (vertices_.empty()) return Vec3{};

    const Basis b = arbitrary_axis(props().normal);
    const Vec3 end = has_hook() ? hook_end() : vertices_.back();

    // Clear of the landing on both axes: a short gap along, so the text does
    // not touch the line it sits at the end of, and a lift so it rests ON the
    // landing rather than being cut by it.
    const double gap = text_height_ * 0.4;
    const double lift = text_height_ * 0.2;
    return end + b.ax * (annotation_on_left() ? -gap : gap) + b.ay * lift;
}

void Leader::regenerate(std::vector<EntityPtr>& out) const {
    if (vertices_.size() < 2) return;

    const Vec3 n = normalize(props().normal);
    const EntityProps& p = props();

    // The arrow sits at vertex 0 and opens back along the first segment, which
    // is why the tip is asked for first: it is the end that means something.
    const Vec3 back = vertices_[1] - vertices_[0];
    if (!is_zero(back)) {
        out.push_back(make_arrowhead(vertices_[0], back, n, arrow_size_, p));
    }

    for (std::size_t i = 0; i + 1 < vertices_.size(); ++i) {
        if (!is_zero(vertices_[i + 1] - vertices_[i])) {
            out.push_back(make_segment(vertices_[i], vertices_[i + 1], p));
        }
    }

    if (has_hook()) out.push_back(make_segment(hook_start(), hook_end(), p));
}

EntityPtr Leader::clone() const {
    auto copy = std::make_unique<Leader>();
    copy->vertices_ = vertices_;
    // Deep, because the annotation is OWNED. A shared note would make COPY
    // produce two leaders that edit each other.
    copy->annotation_ = annotation_ ? annotation_->clone() : nullptr;
    copy->text_height_ = text_height_;
    copy->arrow_size_ = arrow_size_;
    copy_common_to(*copy);
    return copy;
}

void Leader::transform(const Mat4& m) {
    for (Vec3& v : vertices_) v = m.transform_point(v);

    // The note travels with the leader that points at it. Delegating rather
    // than transforming a position by hand is what keeps a rotated MText's own
    // rotation and height right -- the annotation already knows how to do this
    // to itself.
    if (annotation_) annotation_->transform(m);

    const Basis b = arbitrary_axis(props().normal);
    const Vec3 nx = m.transform_vector(b.ax);
    const Vec3 ny = m.transform_vector(b.ay);
    const Vec3 n = normalize(cross(nx, ny));
    if (!is_zero(n)) props().normal = n;

    // Arrow and text scale with the drawing, as a dimension's do: annotation
    // left at its old size on a scaled drawing is worse than useless.
    const double factor = length(m.transform_vector(b.ay));
    if (factor > 0.0) {
        arrow_size_ *= factor;
        text_height_ *= factor;
    }
}

BBox Leader::bbox() const {
    BBox box;
    for (const Vec3& v : vertices_) box.expand(v);
    if (has_hook()) box.expand(hook_end());
    // The note is part of what the leader occupies: ZOOM Extents that cut the
    // text off would be wrong about the drawing.
    if (annotation_) box.expand(annotation_->bbox());
    return box;
}

void Leader::osnap_points(std::vector<OsnapPoint>& out) const {
    if (vertices_.empty()) return;

    // The arrow tip is the point on a leader that means something exact -- it
    // is what the note is about. The rest of the path is a route to it.
    out.push_back({vertices_[0], OsnapType::Endpoint});
    for (std::size_t i = 1; i < vertices_.size(); ++i) {
        out.push_back({vertices_[i], OsnapType::Endpoint});
    }
    if (has_hook()) out.push_back({hook_end(), OsnapType::Endpoint});
}

void Leader::grips(std::vector<Grip>& out) const {
    for (std::size_t i = 0; i < vertices_.size(); ++i) {
        out.push_back(Grip{vertices_[i], GripKind::Stretch,
                           static_cast<GripIndex>(i)});
    }
}

void Leader::stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        const GripIndex g = indices[i];
        if (g >= vertices_.size()) continue;
        vertices_[g] = vertices_[g] + delta;

        // The note rides the far end. Dragging the last vertex is how a leader
        // is repositioned, and leaving the text behind would break the one
        // thing the hook exists to express -- that the two belong together.
        if (g + 1 == vertices_.size() && annotation_) {
            annotation_->transform(Mat4::translation(delta));
        }
    }
}

void Leader::draw(const DrawContext& ctx, Renderer& r) const {
    std::vector<EntityPtr> parts;
    regenerate(parts);
    for (const EntityPtr& part : parts) {
        if (part) part->draw(ctx, r);
    }
    // Drawn through the same call the database would make, so a leader's note
    // looks exactly like the same Text standing on its own.
    if (annotation_) annotation_->draw(ctx, r);
}

void Leader::dxf_write(DxfWriter& w) const {
    // THE DIVERGENCE, PAID FOR HERE. AC1009 has no LEADER entity at all, and
    // while R13 and later do, writing one means binding the annotation with a
    // hard pointer (group 340) to a record that has to exist and be read back
    // as one -- and our reader does not know LEADER, so writing it would open a
    // round trip we cannot close. Producing a file we can only half read is a
    // worse failure than degrading honestly, so both versions get the line work.
    //
    // Same bargain ELLIPSE, SPLINE and MTEXT take at R12: the database keeps
    // geometry the file cannot name, and what goes out is what every reader
    // understands. Stated rather than discovered -- a round trip is LOSSY, and
    // a leader comes back as loose lines and a piece of text.
    std::vector<EntityPtr> parts;
    regenerate(parts);
    for (const EntityPtr& part : parts) {
        if (part) part->dxf_write(w);
    }
    if (annotation_) annotation_->dxf_write(w);
}

}  // namespace ncad
