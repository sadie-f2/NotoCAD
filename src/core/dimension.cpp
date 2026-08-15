// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "annotation.hpp"

#include "ncad/dxf.hpp"
#include "ncad/ecs.hpp"
#include "ncad/entities.hpp"
#include "ncad/font.hpp"
#include "ncad/render.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace ncad {
namespace {

// Gap either side of the text where the dimension line is broken, as a fraction
// of the text height.
constexpr double kTextGap = 0.6;

// Wrapped into [0, 2*pi).
double norm_turn(double a) {
    while (a < 0.0) a += kFullTurn;
    while (a >= kFullTurn) a -= kFullTurn;
    return a;
}

// The measurement, formatted the way every other reported length in this
// program is -- fixed rather than %g, so a column of them lines up.
std::string fmt_measure(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    return buf;
}

// How far the label is turned, in the entity's plane.
//
// Horizontal is R12's default (DIMTIH) and is what AutoCAD draws for a file
// that does not say otherwise. Aligned text follows the dimension line, and
// then it must be kept READABLE: a line running right-to-left would otherwise
// put the digits upside down, which is how an imported drawing came back with
// half its labels mirrored. Every drafting standard says the same thing --
// text reads left to right, or bottom to top, and never the other way.
double label_rotation(bool horizontal, double along_angle) {
    if (horizontal) return 0.0;

    double a = along_angle;
    while (a <= -kFullTurn * 0.25) a += kFullTurn * 0.5;
    while (a > kFullTurn * 0.25) a -= kFullTurn * 0.5;
    return a;
}

}  // namespace

EntityPtr make_arrowhead(const Vec3& tip, const Vec3& back, const Vec3& normal, double size,
                         const EntityProps& props) {
    auto head = std::make_unique<Face>(EntityType::Solid);

    // Opened either side of `back` in the annotation's own plane. Built from
    // the plane's axes rather than by rotating, which needs no rotation helper
    // -- the only one in the tree is private to commands.cpp.
    const Vec3 u = normalize(back);
    const Vec3 side_raw = cross(normal, u);
    const Vec3 side = is_zero(side_raw) ? Vec3{} : normalize(side_raw);
    const Vec3 b1 = tip + (u * std::cos(kBarbSpread) + side * std::sin(kBarbSpread)) * size;
    const Vec3 b2 = tip + (u * std::cos(kBarbSpread) - side * std::sin(kBarbSpread)) * size;

    // Corner 3 repeats corner 2, which is how the format spells a triangle.
    head->set_corner(0, tip);
    head->set_corner(1, b1);
    head->set_corner(2, b2);
    head->set_corner(3, b2);
    head->props() = props;
    return head;
}

EntityPtr make_segment(const Vec3& a, const Vec3& b, const EntityProps& props) {
    auto line = std::make_unique<Line>(a, b);
    line->props() = props;
    return line;
}

void Dimension::apply_style(double text_height, double arrow_size, double ext_offset,
                            double ext_beyond) {
    // Guarded rather than trusted: a zero text height draws nothing and a zero
    // arrow size draws a degenerate SOLID, and both are reachable from SETVAR,
    // which does not range-check reals.
    if (text_height > 0.0) text_height_ = text_height;
    if (arrow_size > 0.0) arrow_size_ = arrow_size;
    if (ext_offset >= 0.0) ext_offset_ = ext_offset;
    if (ext_beyond >= 0.0) ext_beyond_ = ext_beyond;
}

// Which of the two angles between the arms was meant, and where the arc runs.
//
// Two rays cut the plane into an angle and its explement, and the pair of points
// alone cannot say which was wanted -- so the ARC LOCATION decides: the angle
// you are dimensioning is the one you put the arc inside. That is what makes a
// 90-degree corner dimensionable as 270 by dragging the other way, which is
// occasionally exactly what a drawing needs.
bool Dimension::angular_span(double& from, double& sweep) const {
    const Basis b = arbitrary_axis(props().normal);
    const Vec3 r1 = first_ - vertex_;
    const Vec3 r2 = second_ - vertex_;
    const Vec3 rl = definition_ - vertex_;
    if (is_zero(r1) || is_zero(r2)) return false;

    const double a1 = std::atan2(dot(r1, b.ay), dot(r1, b.ax));
    const double a2 = std::atan2(dot(r2, b.ay), dot(r2, b.ax));
    const double al = is_zero(rl) ? a1 : std::atan2(dot(rl, b.ay), dot(rl, b.ax));

    const double ccw = norm_turn(a2 - a1);
    // Inside the counterclockwise sweep from the first arm to the second?
    if (norm_turn(al - a1) <= ccw) {
        from = a1;
        sweep = ccw;
    } else {
        from = a2;
        sweep = kFullTurn - ccw;
    }
    return sweep > kEps;
}

double Dimension::measurement() const {
    if (kind_ == DimKind::Angular) {
        double from = 0.0;
        double sweep = 0.0;
        return angular_span(from, sweep) ? sweep : 0.0;
    }
    switch (kind_) {
        // Answered above, and named here only so that adding a kind produces a
        // warning rather than a silent zero. DimKind is the enum the traps note
        // in HANDOFF is about: its values are DXF's and are not contiguous.
        case DimKind::Angular: break;
        case DimKind::Radius: return length(first_ - definition_);
        case DimKind::Diameter: return 2.0 * length(first_ - definition_);
        case DimKind::Aligned: return length(second_ - first_);
        case DimKind::Linear: {
            // Along the dimension's OWN axis, not between the points. That is
            // the whole difference between a rotated dimension and an aligned
            // one, and it is why horizontal and vertical need no separate code.
            const Basis b = arbitrary_axis(props().normal);
            const Vec3 dir = b.ax * std::cos(rotation_) + b.ay * std::sin(rotation_);
            return std::abs(dot(second_ - first_, dir));
        }
    }
    return 0.0;
}

std::string Dimension::label() const {
    if (!text_.empty()) return text_;

    switch (kind_) {
        case DimKind::Radius: return "R" + fmt_measure(measurement());
        // R12's escape for the diameter sign. The label KEEPS the escape --
        // that is what goes to DXF and what AutoCAD expects -- and the font
        // layer resolves it when the string is laid out. See `decode_text`.
        case DimKind::Diameter: return "%%C" + fmt_measure(measurement());
        // Degrees, and R12's escape for the sign -- the same bargain %%C takes.
        case DimKind::Angular:
            return fmt_measure(measurement() * 180.0 / 3.14159265358979323846) + "%%D";
        default: return fmt_measure(measurement());
    }
}

void Dimension::regenerate(std::vector<EntityPtr>& out) const {
    const Vec3 n = normalize(props().normal);
    const EntityProps& props_ = props();

    if (angular()) {
        double from = 0.0;
        double sweep = 0.0;
        if (!angular_span(from, sweep)) return;

        // The arc runs at whatever distance from the corner the location was
        // put, so dragging further out gives a larger arc across the same
        // angle -- which is how an angular dimension is placed clear of the
        // geometry it measures.
        const double r = length(definition_ - vertex_);
        if (r < kEps) return;

        const Basis b = arbitrary_axis(n);
        const auto at = [&](double a) {
            return vertex_ + (b.ax * std::cos(a) + b.ay * std::sin(a)) * r;
        };
        // Counterclockwise tangent, which is the direction the arc leaves in.
        const auto tangent = [&](double a) {
            return b.ax * -std::sin(a) + b.ay * std::cos(a);
        };

        auto arc = std::make_unique<Arc>(vertex_, r, from, from + sweep, n);
        arc->props() = props_;
        out.push_back(std::move(arc));

        // Extension lines, but only where an arm is shorter than the arc: an
        // arm that already reaches past it needs no help getting there.
        for (const Vec3& arm : {first_, second_}) {
            const double reach = length(arm - vertex_);
            if (reach + kEps >= r) continue;
            const Vec3 dir = normalize(arm - vertex_);
            out.push_back(make_segment(vertex_ + dir * (reach + ext_offset_),
                                  vertex_ + dir * (r + ext_beyond_), props_));
        }

        // Tips on the arc, barbs trailing back along it.
        out.push_back(make_arrowhead(at(from), tangent(from), n, arrow_size_, props_));
        out.push_back(make_arrowhead(at(from + sweep), tangent(from + sweep) * -1.0, n, arrow_size_,
                                props_));

        // The label sits just outside the arc at its midpoint, reading along
        // the tangent there so it lies with the curve rather than across it.
        const double half = from + sweep * 0.5;
        const Vec3 outward = b.ax * std::cos(half) + b.ay * std::sin(half);
        const Vec3 anchor = vertex_ + outward * (r + text_height_ * (0.5 + kTextGap));
        const Vec3 along = tangent(half);

        auto text = std::make_unique<Text>(anchor, label(), text_height_);
        text->set_rotation(
            label_rotation(text_horizontal_, std::atan2(dot(along, b.ay), dot(along, b.ax))));
        text->set_align(TextHAlign::Center, TextVAlign::Middle);
        text->set_align_point(anchor);
        text->props() = props_;
        out.push_back(std::move(text));
        return;
    }

    if (radial()) {
        const Vec3 spoke = first_ - definition_;
        const double r = length(spoke);
        if (r < kEps) return;
        const Vec3 dir = spoke / r;

        // A radius runs centre-to-rim; a diameter runs rim-to-rim through the
        // centre. Both get an arrow where they touch the curve, pointing out.
        const Vec3 tail = kind_ == DimKind::Diameter ? definition_ - dir * r : definition_;
        out.push_back(make_segment(tail, first_, props_));
        out.push_back(make_arrowhead(first_, dir * -1.0, n, arrow_size_, props_));
        if (kind_ == DimKind::Diameter) {
            out.push_back(make_arrowhead(tail, dir, n, arrow_size_, props_));
        }

        // Text alongside the leader rather than on it, so the line does not
        // strike through the digits.
        const Vec3 perp = normalize(cross(n, dir));
        const Vec3 mid = (tail + first_) * 0.5;
        auto text = std::make_unique<Text>(mid + perp * (text_height_ * kTextGap), label(),
                                           text_height_);
        text->set_rotation(label_rotation(
            text_horizontal_,
            std::atan2(dot(dir, arbitrary_axis(n).ay), dot(dir, arbitrary_axis(n).ax))));
        text->set_align(TextHAlign::Center, TextVAlign::Middle);
        text->set_align_point(mid + perp * (text_height_ * kTextGap));
        text->props() = props_;
        out.push_back(std::move(text));
        return;
    }

    // --- linear and aligned --------------------------------------------------

    const Basis basis = arbitrary_axis(n);
    Vec3 dir;
    if (kind_ == DimKind::Aligned) {
        const Vec3 span = second_ - first_;
        if (is_zero(span)) return;
        dir = normalize(span);
    } else {
        dir = basis.ax * std::cos(rotation_) + basis.ay * std::sin(rotation_);
    }

    const Vec3 perp_raw = cross(n, dir);
    if (is_zero(perp_raw)) return;
    const Vec3 perp = normalize(perp_raw);

    // The dimension line passes through the definition point, so each measured
    // point drops onto it along the perpendicular. That is what lets one point
    // pulled off to the side still produce a straight, aligned dimension.
    const double off1 = dot(definition_ - first_, perp);
    const double off2 = dot(definition_ - second_, perp);
    const Vec3 p1 = first_ + perp * off1;
    const Vec3 p2 = second_ + perp * off2;

    const Vec3 run = p2 - p1;
    const double span = length(run);
    if (span < kEps) return;
    const Vec3 along = run / span;

    // Extension lines stand off the geometry by DIMEXO and overshoot the
    // dimension line by DIMEXE, both measured toward the dimension line so a
    // dimension placed on either side looks the same.
    const double sign1 = off1 >= 0.0 ? 1.0 : -1.0;
    const double sign2 = off2 >= 0.0 ? 1.0 : -1.0;
    out.push_back(make_segment(first_ + perp * (sign1 * ext_offset_), p1 + perp * (sign1 * ext_beyond_),
                          props_));
    out.push_back(make_segment(second_ + perp * (sign2 * ext_offset_),
                          p2 + perp * (sign2 * ext_beyond_), props_));

    out.push_back(make_arrowhead(p1, along, n, arrow_size_, props_));
    out.push_back(make_arrowhead(p2, along * -1.0, n, arrow_size_, props_));

    const Vec3 mid = (p1 + p2) * 0.5;
    const std::string caption = label();

    // Text sits IN the dimension line with the line broken around it, which is
    // the mechanical convention and R12's DIMTAD 0. When the text will not fit
    // between the arrows the line is left whole and the text goes above it,
    // which is what every drafting package does and beats drawing a line
    // through the digits.
    Text probe(mid, caption, text_height_);
    const double half_gap = probe.text_width() * 0.5 + text_height_ * kTextGap;
    const bool fits = span > half_gap * 2.0 + arrow_size_ * 2.0;

    if (fits) {
        out.push_back(make_segment(p1, mid - along * half_gap, props_));
        out.push_back(make_segment(mid + along * half_gap, p2, props_));
    } else {
        out.push_back(make_segment(p1, p2, props_));
    }

    const Vec3 anchor = fits ? mid : mid + perp * (text_height_ * (0.5 + kTextGap));
    auto text = std::make_unique<Text>(anchor, caption, text_height_);
    text->set_rotation(
        label_rotation(text_horizontal_, std::atan2(dot(along, basis.ay), dot(along, basis.ax))));
    text->set_align(TextHAlign::Center, TextVAlign::Middle);
    text->set_align_point(anchor);
    text->props() = props_;
    out.push_back(std::move(text));
}

EntityPtr Dimension::clone() const {
    auto copy = std::make_unique<Dimension>();
    copy->kind_ = kind_;
    copy->definition_ = definition_;
    copy->first_ = first_;
    copy->second_ = second_;
    copy->vertex_ = vertex_;
    copy->rotation_ = rotation_;
    copy->text_ = text_;
    copy->text_height_ = text_height_;
    copy->arrow_size_ = arrow_size_;
    copy->ext_offset_ = ext_offset_;
    copy->ext_beyond_ = ext_beyond_;
    copy->text_horizontal_ = text_horizontal_;
    copy_common_to(*copy);
    return copy;
}

void Dimension::transform(const Mat4& m) {
    definition_ = m.transform_point(definition_);
    first_ = m.transform_point(first_);
    second_ = m.transform_point(second_);
    vertex_ = m.transform_point(vertex_);

    // The style sizes scale with the drawing, as TEXT's height does: a drawing
    // scaled up whose annotation stayed put would be unreadable at one end and
    // absurd at the other. Taken from how far a unit vector in the plane
    // travelled, so a rotation leaves them alone.
    const Basis b = arbitrary_axis(props().normal);
    const double factor = length(m.transform_vector(b.ax));
    if (factor > kEps) {
        text_height_ *= factor;
        arrow_size_ *= factor;
        ext_offset_ *= factor;
        ext_beyond_ *= factor;
    }

    // The plane may have turned, and the stored rotation is measured in it.
    const Vec3 turned = m.transform_vector(b.ax * std::cos(rotation_) + b.ay * std::sin(rotation_));
    const Vec3 nx = m.transform_vector(b.ax);
    const Vec3 ny = m.transform_vector(b.ay);
    if (!is_zero(turned) && !is_zero(nx) && !is_zero(ny)) {
        rotation_ = std::atan2(dot(turned, normalize(ny)), dot(turned, normalize(nx)));
    }

    const Vec3 n = m.transform_vector(props().normal);
    if (!is_zero(n)) props().normal = normalize(n);
}

BBox Dimension::bbox() const {
    // From the drawn form rather than from the definition points: the text and
    // the extension lines reach past them, and ZOOM Extents that cut the label
    // off would be wrong in the direction people notice.
    BBox box;
    std::vector<EntityPtr> parts;
    regenerate(parts);
    for (const EntityPtr& e : parts) {
        if (e) box.expand(e->bbox());
    }
    if (!box.valid()) {
        box.expand(definition_);
        box.expand(first_);
    }
    return box;
}

void Dimension::osnap_points(std::vector<OsnapPoint>& out) const {
    // The points it was given, which are the only places on a dimension that
    // mean anything exact. Deliberately not the generated geometry: snapping to
    // the middle of a dimension line is how drawings acquire measurements of
    // measurements.
    out.push_back({definition_, OsnapType::Insert});
    out.push_back({first_, OsnapType::Endpoint});
    if (!radial()) out.push_back({second_, OsnapType::Endpoint});
    // The corner is the one point on an angular dimension worth snapping to.
    if (angular()) out.push_back({vertex_, OsnapType::Center});
}

void Dimension::grips(std::vector<Grip>& out) const {
    out.push_back(Grip{first_, GripKind::Stretch, 0});
    if (!radial()) out.push_back(Grip{second_, GripKind::Stretch, 1});
    out.push_back(Grip{definition_, GripKind::Move, 2});
    if (angular()) out.push_back(Grip{vertex_, GripKind::Stretch, 3});
}

void Dimension::stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        switch (indices[i]) {
            case 0: first_ = first_ + delta; break;
            case 1: if (!radial()) second_ = second_ + delta; break;
            case 2: definition_ = definition_ + delta; break;
            case 3: if (angular()) vertex_ = vertex_ + delta; break;
            default: break;
        }
    }
}

void Dimension::dxf_write(DxfWriter& w) const {
    w.write_common(*this);

    // The block holding the drawn geometry. Written by DxfWriter, which names
    // it and remembers which dimension it belongs to -- the block is a
    // serialisation detail and the database has never heard of it.
    const std::string& block = w.dimension_block(handle());
    if (!block.empty()) w.code(2, block);

    // Group 10 is the dimension line location for a linear dimension and the
    // centre for a radial one, which is what each subclass means by it.
    w.point(10, definition_);

    // Group 11, the middle of the text. Readers that regenerate use it to put
    // the label back where it was placed rather than where they would choose.
    const Vec3 mid = radial() ? (definition_ + first_) * 0.5
                    : angular() ? definition_
                                : (first_ + second_) * 0.5;
    w.point(11, mid);

    // Group 70. The kind IS the low bits, and bit 128 says the text position in
    // group 11 was set deliberately rather than derived.
    w.code(70, static_cast<int>(kind_) | 128);

    if (!text_.empty()) w.code(1, text_);
    w.code(3, "STANDARD");

    // Group 42 is what the dimension states. Read-only to a reader, and written
    // so a program that does not recompute still reports the right number.
    w.code(42, measurement());

    if (angular()) {
        // Written in the three-point form even when it came from two picked
        // lines, because that is how it is HELD -- the vertex is explicit and
        // the arms are points on it, so what the file says is what the entity
        // knows rather than a reconstruction of how it was asked for.
        w.subclass("AcDb3PointAngularDimension");
        w.point(13, first_);
        w.point(14, second_);
        w.point(15, vertex_);
    } else if (radial()) {
        w.subclass(kind_ == DimKind::Diameter ? "AcDbDiametricDimension"
                                              : "AcDbRadialDimension");
        // Where the leader meets the curve.
        w.point(15, first_);
        w.code(40, 0.0);  // leader length
    } else if (kind_ == DimKind::Aligned) {
        w.subclass("AcDbAlignedDimension");
        w.point(13, first_);
        w.point(14, second_);
    } else {
        // R12 spells a rotated dimension as an aligned one with an angle, and
        // the subclass chain says both -- AcDbRotatedDimension derives from
        // AcDbAlignedDimension, so a reader looking for either finds it.
        w.subclass("AcDbAlignedDimension");
        w.point(13, first_);
        w.point(14, second_);
        w.code(50, rotation_ * 180.0 / 3.14159265358979323846);
        w.subclass("AcDbRotatedDimension");
    }

    w.write_extrusion(props().normal);
}

void Dimension::draw(const DrawContext& ctx, Renderer& r) const {
    std::vector<EntityPtr> parts;
    regenerate(parts);
    for (const EntityPtr& e : parts) {
        if (e) e->draw(ctx, r);
    }
}

}  // namespace ncad
