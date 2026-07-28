// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/osnap_derived.hpp"

#include "noto/ecs.hpp"
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

// Intersection of two segments in three dimensions. Skew lines do not meet, so
// the closest approach has to actually close for this to count.
int line_line(const Line& a, const Line& b, Vec3 out[kMaxIntersections]) {
    const Vec3 p = a.start();
    const Vec3 q = b.start();
    const Vec3 u = a.direction();
    const Vec3 v = b.direction();

    const double uu = dot(u, u);
    const double vv = dot(v, v);
    if (uu <= kEps * kEps || vv <= kEps * kEps) return 0;

    const Vec3 w = p - q;
    const double uv = dot(u, v);
    const double uw = dot(u, w);
    const double vw = dot(v, w);

    const double denom = uu * vv - uv * uv;
    // Parallel or collinear. Collinear overlap has no single intersection
    // point, and R12 does not offer one either.
    if (std::abs(denom) <= kEps * uu * vv) return 0;

    const double s = (uv * vw - vv * uw) / denom;
    const double t = (uu * vw - uv * uw) / denom;

    const Vec3 pa = p + u * s;
    const Vec3 pb = q + v * t;
    // Skew: the lines pass, they do not meet. The tolerance is on the gap, in
    // drawing units, which is what "these two lines cross" means to a user.
    if (!near_equal(pa, pb, 1e-7)) return 0;

    // Both hits must lie on the drawn segments, not their extensions.
    if (s < -kEps || s > 1.0 + kEps || t < -kEps || t > 1.0 + kEps) return 0;

    out[0] = pa;
    return 1;
}

// Segment against a circular entity. The case that matters is coplanar, where
// it is a quadratic; a line crossing the plane meets the circle only if it
// happens to land on it.
int line_circular(const Line& l, const Circular& c, Vec3 out[kMaxIntersections]) {
    const Vec3 n = normalize(c.normal);
    const Vec3 p0 = l.start();
    const Vec3 d = l.direction();
    const double len_sq = length_sq(d);
    if (len_sq <= kEps * kEps) return 0;

    const double along = dot(d, n);
    const double off = dot(p0 - c.center, n);

    int count = 0;
    auto accept = [&](double t, const Vec3& p) {
        if (t < -kEps || t > 1.0 + kEps) return;  // off the segment
        if (!on_sweep(c, p)) return;              // off the arc
        out[count++] = p;
    };

    if (std::abs(along) > kEps) {
        // The line crosses the plane at one point. It meets the circle only if
        // that point happens to sit on it.
        const double t = -off / along;
        const Vec3 p = p0 + d * t;
        if (std::abs(length(p - c.center) - c.radius) <= 1e-7) accept(t, p);
        return count;
    }

    // Parallel to the plane: only coplanar lines can reach the circle.
    if (std::abs(off) > 1e-7) return 0;

    // |p0 + t*d - centre|^2 = r^2
    const Vec3 f = p0 - c.center;
    const double a = len_sq;
    const double b = 2.0 * dot(f, d);
    const double cc = dot(f, f) - c.radius * c.radius;
    const double disc = b * b - 4.0 * a * cc;
    if (disc < 0.0) return 0;

    const double root = std::sqrt(std::max(0.0, disc));
    const double t0 = (-b - root) / (2.0 * a);
    const double t1 = (-b + root) / (2.0 * a);

    accept(t0, p0 + d * t0);
    if (root > kEps) accept(t1, p0 + d * t1);
    return count;
}

// Two coplanar circles. The classic construction: the intersections sit on the
// radical line, symmetric about it.
int circular_circular(const Circular& a, const Circular& b, Vec3 out[kMaxIntersections]) {
    const Vec3 na = normalize(a.normal);
    const Vec3 nb = normalize(b.normal);

    // Coplanar means parallel planes at the same offset. Non-coplanar circles
    // are documented as unsupported; see the header.
    if (!is_zero(cross(na, nb), 1e-9)) return 0;
    if (std::abs(dot(b.center - a.center, na)) > 1e-7) return 0;

    const Vec3 delta = b.center - a.center;
    const double dist = length(delta);
    if (dist <= kEps) return 0;                              // concentric
    if (dist > a.radius + b.radius + 1e-9) return 0;         // too far apart
    if (dist < std::abs(a.radius - b.radius) - 1e-9) return 0;  // one inside the other

    // Distance from a's centre to the radical line, along the centre line.
    const double x = (dist * dist + a.radius * a.radius - b.radius * b.radius) / (2.0 * dist);
    const double h_sq = a.radius * a.radius - x * x;
    const double h = std::sqrt(std::max(0.0, h_sq));

    const Vec3 along = delta / dist;
    const Vec3 across = cross(na, along);  // unit: na and along are orthonormal
    const Vec3 mid = a.center + along * x;

    int count = 0;
    auto accept = [&](const Vec3& p) {
        if (on_sweep(a, p) && on_sweep(b, p)) out[count++] = p;
    };

    accept(mid + across * h);
    if (h > kEps) accept(mid - across * h);
    return count;
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

    const Line* la = as_line(a);
    const Line* lb = as_line(b);
    if (la && lb) return line_line(*la, *lb, out);

    Circular ca;
    Circular cb;
    if (la && as_circular(b, cb)) return line_circular(*la, cb, out);
    if (lb && as_circular(a, ca)) return line_circular(*lb, ca, out);
    if (as_circular(a, ca) && as_circular(b, cb)) return circular_circular(ca, cb, out);
    return 0;
}

}  // namespace noto
