// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// POLYLINE: the entity, its bulge arithmetic, and the vtable.
//
// The bulge is the only subtle part. R12 stores group 42 on each vertex as the
// tangent of a quarter of the arc's included angle, signed so that positive is
// counterclockwise. From a bulge b and a chord from p0 to p1:
//
//     included angle  = 4 * atan(b)
//     radius          = |chord| / (2 * sin(angle/2))
//     sagitta         = b * |chord| / 2
//
// Everything else -- centre, start and end angles, tessellation -- falls out of
// those. Storing bulge rather than a centre is what the format does and is also
// what survives editing: dragging a vertex keeps the arc's relationship to its
// neighbours rather than referring to a centre that may no longer make sense.
#include "ncad/entities.hpp"

#include "ncad/dxf.hpp"
#include "ncad/ecs.hpp"
#include "ncad/render.hpp"

#include <cmath>

namespace ncad {
namespace {

constexpr double kBulgeEps = 1e-12;

}  // namespace

void Polyline::set_uniform_width(double w) {
    for (PolyVertex& v : vertices_) {
        v.start_width = w;
        v.end_width = w;
    }
}

bool Polyline::has_width() const {
    for (const PolyVertex& v : vertices_) {
        if (v.start_width != 0.0 || v.end_width != 0.0) return true;
    }
    return false;
}

std::size_t Polyline::segment_count() const {
    if (vertices_.size() < 2) return 0;
    return closed_ ? vertices_.size() : vertices_.size() - 1;
}

bool Polyline::segment_arc(std::size_t i, Vec3* centre, double* radius, double* start_angle,
                           double* end_angle) const {
    if (i >= segment_count()) return false;
    const double bulge = vertices_[i].bulge;
    if (std::abs(bulge) < kBulgeEps) return false;  // a straight segment

    const Vec3& p0 = vertices_[i].pos;
    const Vec3& p1 = vertices_[(i + 1) % vertices_.size()].pos;

    const Basis b = arbitrary_axis(props().normal);
    const Vec3 chord = p1 - p0;
    const double chord_len = ncad::length(chord);
    if (chord_len < kBulgeEps) return false;  // coincident vertices: no arc

    const double included = 4.0 * std::atan(bulge);
    const double half = included * 0.5;
    const double sin_half = std::sin(half);
    if (std::abs(sin_half) < kBulgeEps) return false;

    const double r = chord_len / (2.0 * std::abs(sin_half));

    // The centre sits off the chord's midpoint, perpendicular to it in the
    // polyline's own plane. Which side depends on the bulge's sign.
    const Vec3 mid = (p0 + p1) * 0.5;
    const Vec3 chord_dir = chord / chord_len;
    // Perpendicular within the plane, obtained by rotating the chord direction
    // a quarter turn about the normal.
    const Vec3 perp = cross(normalize(props().normal), chord_dir);

    // Signed distance from the chord's midpoint to the centre, along perp.
    //
    // Written with tan rather than r*cos(half) because tan is odd and cos is
    // not: cos would give the same value for a bulge and its negative, putting
    // clockwise and counterclockwise arcs on the same side of the chord. It
    // also handles a major arc without a case, since tan goes negative past a
    // half turn and the centre crosses to the other side by itself.
    const double tan_half = std::tan(half);
    if (std::abs(tan_half) < kBulgeEps) return false;
    const double apothem = (chord_len * 0.5) / tan_half;
    const Vec3 c = mid + perp * apothem;

    *centre = c;
    *radius = r;
    *start_angle = std::atan2(dot(p0 - c, b.ay), dot(p0 - c, b.ax));
    *end_angle = std::atan2(dot(p1 - c, b.ay), dot(p1 - c, b.ax));
    return true;
}

double Polyline::length() const {
    double total = 0.0;
    for (std::size_t i = 0; i < segment_count(); ++i) {
        Vec3 c{};
        double r = 0.0;
        double a0 = 0.0;
        double a1 = 0.0;
        if (segment_arc(i, &c, &r, &a0, &a1)) {
            total += r * std::abs(4.0 * std::atan(vertices_[i].bulge));
        } else {
            total += ncad::length(vertices_[(i + 1) % vertices_.size()].pos - vertices_[i].pos);
        }
    }
    return total;
}

EntityPtr Polyline::clone() const {
    auto copy = std::make_unique<Polyline>();
    copy->vertices_ = vertices_;
    copy->closed_ = closed_;
    copy_common_to(*copy);
    return copy;
}

void Polyline::transform(const Mat4& m) {
    for (PolyVertex& v : vertices_) v.pos = m.transform_point(v.pos);

    // The normal is rebuilt from the transformed plane rather than transformed
    // directly, so a mirror flips the plane's orientation instead of leaving a
    // normal that disagrees with the winding -- the same reasoning the circular
    // entities use.
    const Basis b = arbitrary_axis(props().normal);
    const Vec3 tx = m.transform_vector(b.ax);
    const Vec3 ty = m.transform_vector(b.ay);
    Vec3 n = normalize(cross(tx, ty));
    if (!is_zero(n)) props().normal = n;

    // Bulges are unitless ratios and survive any similarity transform. A mirror
    // reverses the sense of every arc, though, which the sign carries.
    const double det = dot(cross(tx, ty), m.transform_vector(b.az));
    if (det < 0.0) {
        for (PolyVertex& v : vertices_) v.bulge = -v.bulge;
    }

    // Widths are lengths, so they scale. Uniform scale is assumed for the same
    // reason the circular entities assume it -- R12 cannot represent the
    // non-uniform result -- and the factor is taken from the same basis vector.
    const double scale = ncad::length(tx);
    if (scale != 1.0) {
        for (PolyVertex& v : vertices_) {
            v.start_width *= scale;
            v.end_width *= scale;
        }
    }
}

BBox Polyline::bbox() const {
    BBox box;
    for (const PolyVertex& v : vertices_) box.expand(v.pos);

    // Arc segments bulge outside the hull of their endpoints, so the vertices
    // alone would give a box that is too small -- and a too-small box makes an
    // entity unpickable exactly where it visibly is.
    for (std::size_t i = 0; i < segment_count(); ++i) {
        Vec3 c{};
        double r = 0.0;
        double a0 = 0.0;
        double a1 = 0.0;
        if (!segment_arc(i, &c, &r, &a0, &a1)) continue;

        const Basis b = arbitrary_axis(props().normal);
        const double included = 4.0 * std::atan(vertices_[i].bulge);
        // Sample the arc rather than solving for axis extremes: a handful of
        // points is enough for a bounding box and needs no case analysis about
        // which quadrant boundaries the sweep crosses.
        constexpr int kSamples = 16;
        for (int k = 0; k <= kSamples; ++k) {
            const double t = static_cast<double>(k) / kSamples;
            const double ang = a0 + included * t;
            box.expand(c + (b.ax * std::cos(ang) + b.ay * std::sin(ang)) * r);
        }
    }
    return box;
}

void Polyline::osnap_points(std::vector<OsnapPoint>& out) const {
    for (const PolyVertex& v : vertices_) out.push_back({v.pos, OsnapType::Endpoint});

    for (std::size_t i = 0; i < segment_count(); ++i) {
        const Vec3& p0 = vertices_[i].pos;
        const Vec3& p1 = vertices_[(i + 1) % vertices_.size()].pos;

        Vec3 c{};
        double r = 0.0;
        double a0 = 0.0;
        double a1 = 0.0;
        if (segment_arc(i, &c, &r, &a0, &a1)) {
            // An arc segment's midpoint is on the arc, not on the chord, and
            // its centre is a snap in its own right.
            const Basis b = arbitrary_axis(props().normal);
            const double mid_angle = a0 + 0.5 * 4.0 * std::atan(vertices_[i].bulge);
            out.push_back(
                {c + (b.ax * std::cos(mid_angle) + b.ay * std::sin(mid_angle)) * r,
                 OsnapType::Midpoint});
            out.push_back({c, OsnapType::Center});
        } else {
            out.push_back({(p0 + p1) * 0.5, OsnapType::Midpoint});
        }
    }
}

void Polyline::grips(std::vector<Grip>& out) const {
    // One stretch grip per vertex, indexed by position, which is what STRETCH
    // and grip dragging both want. No midpoint Move grip: a polyline is picked
    // up by its vertices, and adding one per segment would bury them.
    for (std::size_t i = 0; i < vertices_.size(); ++i) {
        out.push_back(Grip{vertices_[i].pos, GripKind::Stretch, static_cast<GripIndex>(i)});
    }
}

void Polyline::stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) {
    for (std::size_t k = 0; k < count; ++k) {
        const GripIndex i = indices[k];
        if (i < vertices_.size()) vertices_[i].pos = vertices_[i].pos + delta;
    }
}

void Polyline::draw(const DrawContext& ctx, Renderer& r) const {
    if (vertices_.size() < 2) return;

    std::vector<Vec3> pts;
    const Basis b = arbitrary_axis(props().normal);

    for (std::size_t i = 0; i < segment_count(); ++i) {
        pts.push_back(vertices_[i].pos);

        Vec3 c{};
        double radius = 0.0;
        double a0 = 0.0;
        double a1 = 0.0;
        if (!segment_arc(i, &c, &radius, &a0, &a1)) continue;

        // Flattened here like every other curve, so hit-testing, selection and
        // both renderers see the same geometry.
        const double included = 4.0 * std::atan(vertices_[i].bulge);
        const int steps = arc_segment_count(radius, std::abs(included), ctx.chord_tolerance);
        for (int k = 1; k < steps; ++k) {
            const double ang = a0 + included * (static_cast<double>(k) / steps);
            pts.push_back(c + (b.ax * std::cos(ang) + b.ay * std::sin(ang)) * radius);
        }
    }

    if (!closed_) pts.push_back(vertices_.back().pos);
    r.polyline(pts.data(), pts.size(), closed_);
}

}  // namespace ncad
