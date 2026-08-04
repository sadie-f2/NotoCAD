// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/corner.hpp"

#include "ncad/ecs.hpp"
#include "ncad/entities.hpp"
#include "ncad/intersect.hpp"

#include <cmath>

namespace ncad {
namespace {

// What the two lines share before either ending is chosen: where the carriers
// cross, and which way each line runs from there toward the piece the pick
// keeps.
struct Meeting {
    Vec3 point{};   // the crossing
    Vec3 dir_a{};   // unit, from the crossing toward the kept side of a
    Vec3 dir_b{};
    Vec3 keep_a{};  // the endpoint each pick kept
    Vec3 keep_b{};
    double len_a{0.0};  // how much line there is to give up, along dir
    double len_b{0.0};
};

// The endpoint of `l` that a pick at `pick` keeps, given the crossing at `p`.
//
// One rule covers both readings. When the crossing falls inside the line the
// endpoints sit on opposite sides of it and the pick's side wins outright; when
// it falls beyond an end -- the line has to be EXTENDED to reach the corner --
// both endpoints lie the same way and the farther one is the one that keeps the
// line rather than erasing it.
Vec3 kept_endpoint(const Line& l, const Vec3& p, const Vec3& pick) {
    const Vec3 toward = pick - p;
    return dot(l.start() - p, toward) >= dot(l.end() - p, toward) ? l.start() : l.end();
}

bool meet(const Line& a, const Vec3& pick_a, const Line& b, const Vec3& pick_b,
          const Vec3& plane, Meeting* out) {
    IntersectionList hits;
    intersect_line_line(a.start(), a.end(), b.start(), b.end(), hits);
    if (hits.empty()) return false;  // parallel, or skew in space

    out->point = hits[0].point;
    out->keep_a = kept_endpoint(a, out->point, pick_a);
    out->keep_b = kept_endpoint(b, out->point, pick_b);

    const Vec3 va = out->keep_a - out->point;
    const Vec3 vb = out->keep_b - out->point;
    out->len_a = length(va);
    out->len_b = length(vb);
    // A pick that resolves to the crossing itself names no side, and there is
    // no corner to build without two of them.
    if (out->len_a < kEps || out->len_b < kEps) return false;

    out->dir_a = va / out->len_a;
    out->dir_b = vb / out->len_b;

    // Collinear lines have a crossing but no corner: the "arc" between them
    // would have to be a half turn of no particular radius.
    const Vec3 turn = cross(out->dir_a, out->dir_b);
    if (length(turn) < kEps) return false;
    (void)plane;
    return true;
}

}  // namespace

bool fillet_lines(const Line& a, const Vec3& pick_a, const Line& b, const Vec3& pick_b,
                  double radius, const Vec3& plane, CornerFit* out) {
    Meeting m;
    if (!meet(a, pick_a, b, pick_b, plane, &m)) return false;

    out->keep_a = m.keep_a;
    out->keep_b = m.keep_b;

    if (radius <= kEps) {
        // R12's corner: both lines meet the crossing and nothing fills it.
        out->cut_a = m.point;
        out->cut_b = m.point;
        out->has_arc = false;
        return true;
    }

    // The tangent length: how far back from the crossing each line must be cut
    // for an arc of this radius to touch both. Half the angle between the two
    // kept directions is what sets it.
    const double cos_full = std::clamp(dot(m.dir_a, m.dir_b), -1.0, 1.0);
    const double half = std::acos(cos_full) * 0.5;
    const double sin_half = std::sin(half);
    const double tan_half = std::tan(half);
    if (sin_half < kEps || tan_half < kEps) return false;

    const double tangent = radius / tan_half;
    // Refusing here rather than extending is deliberate: an arc that needs more
    // line than exists would have to lengthen the very lines the fillet is
    // shortening, and R12 says the radius is too large instead.
    if (tangent > m.len_a + kEps || tangent > m.len_b + kEps) return false;

    out->cut_a = m.point + m.dir_a * tangent;
    out->cut_b = m.point + m.dir_b * tangent;

    // The centre sits on the bisector, at the distance that puts it `radius`
    // from both lines.
    const Vec3 bisector = normalize(m.dir_a + m.dir_b);
    if (length(bisector) < kEps) return false;
    out->centre = m.point + bisector * (radius / sin_half);
    out->radius = radius;
    out->has_arc = true;

    // The arc is the minor one between the two tangent points, and R12 stores
    // every arc counterclockwise -- so the ends are ordered by which way round
    // the short sweep runs, not by which line was picked first.
    const Basis basis = arbitrary_axis(plane);
    const Vec3 ra = out->cut_a - out->centre;
    const Vec3 rb = out->cut_b - out->centre;
    const double angle_a = std::atan2(dot(ra, basis.ay), dot(ra, basis.ax));
    const double angle_b = std::atan2(dot(rb, basis.ay), dot(rb, basis.ax));

    double sweep = angle_b - angle_a;
    while (sweep < 0.0) sweep += kFullTurn;
    if (sweep <= kFullTurn * 0.5) {
        out->start_angle = angle_a;
        out->end_angle = angle_b;
    } else {
        out->start_angle = angle_b;
        out->end_angle = angle_a;
    }
    return true;
}

bool chamfer_lines(const Line& a, const Vec3& pick_a, const Line& b, const Vec3& pick_b,
                   double dist_a, double dist_b, const Vec3& plane, CornerFit* out) {
    Meeting m;
    if (!meet(a, pick_a, b, pick_b, plane, &m)) return false;

    if (dist_a < 0.0 || dist_b < 0.0) return false;
    if (dist_a > m.len_a + kEps || dist_b > m.len_b + kEps) return false;

    out->keep_a = m.keep_a;
    out->keep_b = m.keep_b;
    out->cut_a = m.point + m.dir_a * dist_a;
    out->cut_b = m.point + m.dir_b * dist_b;
    out->has_arc = false;
    return true;
}

}  // namespace ncad
