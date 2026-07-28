// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/osnap_derived.hpp"

#include "noto/ecs.hpp"
#include "noto/intersect.hpp"
#include "noto/entities.hpp"

#include <algorithm>
#include <cmath>

namespace noto {
namespace {

// Dispatch without RTTI, as the whole kernel does: the type enum is the tag,
// and DXF needs it regardless.
const Line* as_line(const Entity& e) {
    return e.type() == EntityType::Line ? static_cast<const Line*>(&e) : nullptr;
}

// Circles and arcs are one shape with one extra constraint, so every routine
// below works on this and filters the sweep at the end.
struct Circular {
    Vec3 center{};
    double radius{0.0};
    Vec3 normal{kWorldZ};
    const Arc* arc{nullptr};  // non-null when the sweep must be respected
};

bool as_circular(const Entity& e, Circular& out) {
    if (e.type() == EntityType::Circle) {
        const auto& c = static_cast<const Circle&>(e);
        out = {c.center(), c.radius(), c.props().normal, nullptr};
        return out.radius > kEps;
    }
    if (e.type() == EntityType::Arc) {
        const auto& a = static_cast<const Arc&>(e);
        out = {a.center(), a.radius(), a.props().normal, &a};
        return out.radius > kEps;
    }
    return false;
}

// True when the point lies on the drawn part of the entity, not merely on its
// host circle. A Circle has no sweep to fail.
bool on_sweep(const Circular& c, const Vec3& p) {
    if (!c.arc) return true;
    const Basis b = arbitrary_axis(c.normal);
    const Vec3 offset = p - c.center;
    return c.arc->contains_angle(std::atan2(dot(offset, b.ay), dot(offset, b.ax)));
}

Vec3 project_to_plane(const Vec3& p, const Vec3& origin, const Vec3& normal) {
    return p - normal * dot(p - origin, normal);
}

// The point on the host circle nearest `ref`, and whether one exists. Ambiguous
// only when `ref` projects exactly onto the centre, where every point ties.
bool circle_nearest(const Circular& c, const Vec3& ref, Vec3* out) {
    const Vec3 n = normalize(c.normal);
    const Vec3 in_plane = project_to_plane(ref, c.center, n);
    const Vec3 radial = in_plane - c.center;
    if (is_zero(radial)) {
        // Every point on the circle is equidistant; pick the ECS X axis so the
        // answer is at least repeatable rather than arbitrary.
        *out = c.center + arbitrary_axis(c.normal).ax * c.radius;
        return true;
    }
    *out = c.center + normalize(radial) * c.radius;
    return true;
}

// Nearest point on a circular entity, respecting the sweep: if the radial
// answer falls outside an arc, the nearer endpoint wins.
bool circular_nearest(const Circular& c, const Vec3& ref, Vec3* out) {
    Vec3 p{};
    if (!circle_nearest(c, ref, &p)) return false;
    if (on_sweep(c, p)) {
        *out = p;
        return true;
    }
    const Vec3 s = c.arc->start_point();
    const Vec3 e = c.arc->end_point();
    *out = (length_sq(ref - s) <= length_sq(ref - e)) ? s : e;
    return true;
}

// Foot of the perpendicular from `ref` to the infinite line through the
// segment, with `t` in segment parameter space.
bool line_foot(const Line& l, const Vec3& ref, Vec3* out, double* t_out) {
    const Vec3 d = l.direction();
    const double len_sq = length_sq(d);
    if (len_sq <= kEps * kEps) return false;  // degenerate line
    const double t = dot(ref - l.start(), d) / len_sq;
    if (t_out) *t_out = t;
    *out = l.start() + d * t;
    return true;
}

}  // namespace

bool nearest_point(const Entity& e, const Vec3& ref, Vec3* out) {
    if (!out) return false;

    if (const Line* l = as_line(e)) {
        double t = 0.0;
        Vec3 foot{};
        if (!line_foot(*l, ref, &foot, &t)) return false;
        // NEA stays on the entity, unlike PER: clamp to the segment.
        if (t <= 0.0) {
            *out = l->start();
        } else if (t >= 1.0) {
            *out = l->end();
        } else {
            *out = foot;
        }
        return true;
    }

    Circular c;
    if (as_circular(e, c)) return circular_nearest(c, ref, out);
    return false;
}

bool perpendicular_point(const Entity& e, const Vec3& ref, Vec3* out) {
    if (!out) return false;

    if (const Line* l = as_line(e)) {
        // Unclamped on purpose: see the header.
        return line_foot(*l, ref, out, nullptr);
    }

    Circular c;
    if (!as_circular(e, c)) return false;

    // Radial, and the nearer of the two candidates -- which is exactly the
    // nearest point, filtered by the sweep.
    Vec3 p{};
    if (!circle_nearest(c, ref, &p)) return false;
    if (on_sweep(c, p)) {
        *out = p;
        return true;
    }
    // The far side may still be on the arc when the near side is not.
    const Vec3 far = c.center * 2.0 - p;
    if (on_sweep(c, far)) {
        *out = far;
        return true;
    }
    return false;
}

int tangent_points(const Entity& e, const Vec3& ref, Vec3 out[kMaxTangents]) {
    Circular c;
    if (!out || !as_circular(e, c)) return 0;

    const Vec3 n = normalize(c.normal);
    const Vec3 p = project_to_plane(ref, c.center, n);
    const Vec3 radial = p - c.center;
    const double dist = length(radial);

    if (dist < c.radius - 1e-9) return 0;  // inside: no tangent exists

    const Vec3 along = normalize(radial);
    if (is_zero(along)) return 0;  // at the centre

    // On the circle: the tangent point is the point itself.
    if (dist <= c.radius + 1e-9) {
        const Vec3 t = c.center + along * c.radius;
        if (!on_sweep(c, t)) return 0;
        out[0] = t;
        return 1;
    }

    // The tangent point subtends acos(r/d) from the centre line.
    const double angle = std::acos(std::clamp(c.radius / dist, -1.0, 1.0));
    const Vec3 across = cross(n, along);

    int count = 0;
    for (const double sign : {1.0, -1.0}) {
        const Vec3 dir = along * std::cos(angle) + across * (std::sin(angle) * sign);
        const Vec3 t = c.center + dir * c.radius;
        if (on_sweep(c, t)) out[count++] = t;
    }
    return count;
}

int intersect_entities(const Entity& a, const Entity& b, Vec3 out[kMaxIntersections]) {
    if (!out) return 0;

    // Delegated to the kernel rather than solved again here. This function came
    // first and answered the narrower question osnap asks; intersect.hpp answers
    // the general one that TRIM and FILLET need. Keeping both implementations
    // would mean two sets of tolerances deciding whether a corner is closed,
    // and one of them would eventually be the wrong one.
    //
    // What stays osnap's own is the shape of the answer: points only, bounded
    // only, and at most two of them -- a cursor cannot usefully be offered the
    // twelve places two polylines cross.
    IntersectionList hits;
    intersect(a, b, IntersectMode::Bounded, hits);

    int count = 0;
    for (const Intersection& hit : hits) {
        if (count >= kMaxIntersections) break;

        // Nearly-coincident answers collapse: a polyline that meets a line at a
        // shared vertex reports it once per adjoining segment, and offering the
        // same point twice would make pick cycling stutter.
        bool duplicate = false;
        for (int i = 0; i < count; ++i) {
            if (near_equal(out[i], hit.point, 1e-9)) duplicate = true;
        }
        if (!duplicate) out[count++] = hit.point;
    }
    return count;
}

}  // namespace noto
