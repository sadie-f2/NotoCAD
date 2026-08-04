// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/offset.hpp"

#include "ncad/ecs.hpp"
#include "ncad/entities.hpp"
#include "ncad/intersect.hpp"

#include <cmath>
#include <memory>
#include <vector>

namespace ncad {
namespace {

// Degeneracy is measured with the shared ncad::kEps, for the reason
// intersect.hpp gives about its own tolerance: a second number here is a
// second opinion about whether a corner is closed.

// Below this a bulge is a straight segment, matching what segment_arc treats
// as one -- the two must agree or a segment changes kind between them.
constexpr double kBulgeEps = 1e-12;

EntityPtr offset_line(const Line& l, double d, const Vec3& side, const Vec3& plane) {
    const Vec3 dir = l.end() - l.start();
    const double len = length(dir);
    if (len < kEps) return nullptr;

    const Vec3 u = dir / len;
    const Vec3 left = cross(normalize(plane), u);
    const double left_len = length(left);
    // Zero when the line runs along the plane's normal: seen edge-on there is
    // no sideways to move in, and no answer is better than an arbitrary one.
    if (left_len < kEps) return nullptr;

    const Vec3 lu = left / left_len;
    const double s = dot(side - l.start(), lu) >= 0.0 ? 1.0 : -1.0;
    const Vec3 shift = lu * (s * d);

    auto out = std::make_unique<Line>(l.start() + shift, l.end() + shift);
    out->props() = l.props();
    return out;
}

// Which way a radial offset goes: outward when the side point lies outside the
// circle. Measured in the entity's own plane, so a pick a little off it still
// says what it meant.
bool offset_outward(const Vec3& centre, double radius, const Vec3& normal, const Vec3& side) {
    const Vec3 n = normalize(normal);
    const Vec3 rel = side - centre;
    return length(rel - n * dot(rel, n)) > radius;
}

EntityPtr offset_circle(const Circle& c, double d, const Vec3& side) {
    const double r = offset_outward(c.center(), c.radius(), c.props().normal, side)
                         ? c.radius() + d
                         : c.radius() - d;
    if (r <= kEps) return nullptr;  // offset inward past its own centre

    auto out = std::make_unique<Circle>(c.center(), r, c.props().normal);
    out->props() = c.props();
    return out;
}

EntityPtr offset_arc(const Arc& a, double d, const Vec3& side) {
    const double r = offset_outward(a.center(), a.radius(), a.props().normal, side)
                         ? a.radius() + d
                         : a.radius() - d;
    if (r <= kEps) return nullptr;

    // The angles are unchanged: an offset arc is concentric, so it subtends the
    // same sweep. Only the radius moves.
    auto out = std::make_unique<Arc>(a.center(), r, a.start_angle(), a.end_angle(),
                                     a.props().normal);
    out->props() = a.props();
    return out;
}

// --- polyline ---------------------------------------------------------------

// One segment of the polyline after offsetting, as the carrier its neighbours
// will be intersected against. Straight segments keep their endpoints; arc
// segments keep their centre and take a new radius, because a concentric arc
// is what the offset of an arc is.
struct OffsetSeg {
    bool arc{false};
    Vec3 a{};       // straight: the offset segment's endpoints
    Vec3 b{};
    Vec3 centre{};  // arc: unchanged by offsetting
    double radius{0.0};
    double sense{1.0};  // arc: +1 when the source bulge was positive
};

// Where vertex `p` lands under this segment's offset, ignoring its neighbour.
// The corner is then wherever two of these carriers actually meet; this is the
// answer only at a free end, and the fallback when they do not meet at all.
Vec3 naive_point(const OffsetSeg& s, const Vec3& p, bool at_start) {
    if (!s.arc) return at_start ? s.a : s.b;

    const Vec3 radial = p - s.centre;
    const double len = length(radial);
    if (len < kEps) return p;
    return s.centre + radial * (s.radius / len);
}

// The tangent direction of travel at a vertex, used only to decide which side
// the pick is on. Sign follows the drawn direction, so "left" means the same
// thing for every segment kind.
bool side_sign(const Polyline& pl, const Vec3& side, const Vec3& n, double* out) {
    double t = 0.0;
    if (!curve_parameter_at(pl, side, &t)) return false;

    Vec3 at{};
    if (!curve_point_at(pl, t, &at)) return false;

    const std::size_t segs = pl.segment_count();
    if (segs == 0) return false;
    std::size_t i = static_cast<std::size_t>(t * static_cast<double>(segs));
    if (i >= segs) i = segs - 1;

    Vec3 left{};
    Vec3 centre{};
    double radius = 0.0;
    double sa = 0.0;
    double ea = 0.0;
    if (pl.segment_arc(i, &centre, &radius, &sa, &ea)) {
        // Travelling counterclockwise -- a positive bulge -- the centre is on
        // the left, so the inward radial IS the left direction.
        const Vec3 radial = at - centre;
        const double len = length(radial);
        if (len < kEps) return false;
        const double sense = pl.vertices()[i].bulge > 0.0 ? 1.0 : -1.0;
        left = radial * (-sense / len);
    } else {
        const std::size_t next = (i + 1) % pl.vertices().size();
        const Vec3 dir = pl.vertices()[next].pos - pl.vertices()[i].pos;
        const double len = length(dir);
        if (len < kEps) return false;
        left = cross(n, dir / len);
        const double left_len = length(left);
        if (left_len < kEps) return false;
        left = left / left_len;
    }

    *out = dot(side - at, left) >= 0.0 ? 1.0 : -1.0;
    return true;
}

EntityPtr offset_polyline(const Polyline& pl, double d, const Vec3& side) {
    const std::size_t nverts = pl.vertices().size();
    const std::size_t nsegs = pl.segment_count();
    if (nsegs == 0) return nullptr;

    const Vec3 n = normalize(pl.props().normal);
    double s = 1.0;
    if (!side_sign(pl, side, n, &s)) return nullptr;

    // Every segment offset on its own first. The corners come afterwards, from
    // where these carriers meet -- which is the whole reason they are built as
    // carriers rather than as final geometry.
    std::vector<OffsetSeg> segs(nsegs);
    for (std::size_t i = 0; i < nsegs; ++i) {
        const Vec3& p0 = pl.vertices()[i].pos;
        const Vec3& p1 = pl.vertices()[(i + 1) % nverts].pos;

        Vec3 centre{};
        double radius = 0.0;
        double sa = 0.0;
        double ea = 0.0;
        if (pl.segment_arc(i, &centre, &radius, &sa, &ea)) {
            const double sense = pl.vertices()[i].bulge > 0.0 ? 1.0 : -1.0;
            segs[i].arc = true;
            segs[i].centre = centre;
            segs[i].sense = sense;
            // Offsetting to the left of travel moves toward the centre when the
            // arc turns counterclockwise, and away from it when it turns the
            // other way.
            segs[i].radius = radius - s * sense * d;
            if (segs[i].radius <= kEps) return nullptr;  // the arc collapses
        } else {
            const Vec3 dir = p1 - p0;
            const double len = length(dir);
            if (len < kEps) return nullptr;
            Vec3 left = cross(n, dir / len);
            const double left_len = length(left);
            if (left_len < kEps) return nullptr;
            left = left * (s * d / left_len);
            segs[i].a = p0 + left;
            segs[i].b = p1 + left;
        }
    }

    // Where two offset carriers meet, nearest to where the vertex would have
    // gone on its own. Tangent joints give no intersection and need none: the
    // two carriers already agree there, which is what `fallback` holds.
    const auto corner = [&](const OffsetSeg& prev, const OffsetSeg& next,
                            const Vec3& fallback) -> Vec3 {
        IntersectionList hits;
        if (!prev.arc && !next.arc) {
            intersect_line_line(prev.a, prev.b, next.a, next.b, hits);
        } else if (!prev.arc && next.arc) {
            intersect_line_circle(prev.a, prev.b, next.centre, next.radius, n, hits);
        } else if (prev.arc && !next.arc) {
            intersect_line_circle(next.a, next.b, prev.centre, prev.radius, n, hits);
        } else {
            intersect_circle_circle(prev.centre, prev.radius, n, next.centre, next.radius, n,
                                    hits);
        }
        if (hits.empty()) return fallback;

        std::size_t best = 0;
        double best_d = length(hits[0].point - fallback);
        for (std::size_t k = 1; k < hits.size(); ++k) {
            const double dist = length(hits[k].point - fallback);
            if (dist < best_d) {
                best_d = dist;
                best = k;
            }
        }
        return hits[best].point;
    };

    std::vector<Vec3> pts(nverts);
    for (std::size_t j = 0; j < nverts; ++j) {
        const Vec3& p = pl.vertices()[j].pos;

        const bool has_prev = pl.closed() || j > 0;
        const bool has_next = pl.closed() || j + 1 < nverts;
        const std::size_t prev_i = (j + nsegs - 1) % nsegs;
        const std::size_t next_i = j % nsegs;

        if (has_prev && has_next) {
            const Vec3 fallback = naive_point(segs[next_i], p, true);
            pts[j] = corner(segs[prev_i], segs[next_i], fallback);
        } else if (has_next) {
            pts[j] = naive_point(segs[next_i], p, true);
        } else {
            pts[j] = naive_point(segs[prev_i], p, false);
        }
    }

    auto out = std::make_unique<Polyline>();
    out->props() = pl.props();
    out->set_closed(pl.closed());

    const Basis basis = arbitrary_axis(n);
    for (std::size_t j = 0; j < nverts; ++j) {
        double bulge = 0.0;
        const std::size_t seg = j;
        if (seg < nsegs && segs[seg].arc) {
            // The sweep is recomputed rather than carried over: a corner joint
            // moves an arc's endpoints along its own circle, which changes the
            // included angle even though the radius change alone would not.
            const Vec3 c = segs[seg].centre;
            const Vec3 va = pts[j] - c;
            const Vec3 vb = pts[(j + 1) % nverts] - c;
            const double aa = std::atan2(dot(va, basis.ay), dot(va, basis.ax));
            const double ab = std::atan2(dot(vb, basis.ay), dot(vb, basis.ax));

            double sweep = ab - aa;
            // Wrapped into the turn direction the source arc had, so a major
            // arc stays major and the offset does not flip to the short way
            // round.
            if (segs[seg].sense > 0.0) {
                while (sweep <= 0.0) sweep += kFullTurn;
                while (sweep > kFullTurn) sweep -= kFullTurn;
            } else {
                while (sweep >= 0.0) sweep -= kFullTurn;
                while (sweep < -kFullTurn) sweep += kFullTurn;
            }
            bulge = std::tan(sweep / 4.0);
            if (std::abs(bulge) < kBulgeEps) bulge = 0.0;
        }
        out->add(pts[j], bulge, pl.vertices()[j].start_width, pl.vertices()[j].end_width);
    }
    return out;
}

}  // namespace

EntityPtr offset_curve(const Entity& e, double distance, const Vec3& side, const Vec3& plane) {
    if (distance <= kEps) return nullptr;

    switch (e.type()) {
        case EntityType::Line:
            return offset_line(static_cast<const Line&>(e), distance, side, plane);
        case EntityType::Circle:
            return offset_circle(static_cast<const Circle&>(e), distance, side);
        case EntityType::Arc:
            return offset_arc(static_cast<const Arc&>(e), distance, side);
        case EntityType::Polyline:
            return offset_polyline(static_cast<const Polyline&>(e), distance, side);
        default:
            // ELLIPSE and SPLINE decline here rather than being approximated --
            // see the header. Everything else has no curve to offset at all.
            return nullptr;
    }
}

}  // namespace ncad
