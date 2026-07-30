// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/osnap_derived.hpp"

#include "noto/ecs.hpp"
#include "noto/intersect.hpp"
#include "noto/entities.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

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

// --- Curves that are neither a line nor a circle ----------------------------
//
// POLYLINE, ELLIPSE and SPLINE all arrive here. Until now none of them had a
// derived snap at all: as_line() and as_circular() answer for none of the
// three, so NEAREST, PERPENDICULAR and TANGENT returned false and the cursor
// silently offered nothing on the entity being pointed at.

constexpr double kTwoPi = 2.0 * std::numbers::pi;

double wrap_angle(double a) {
    a = std::fmod(a, kTwoPi);
    return (a < 0.0) ? a + kTwoPi : a;
}

// Roots of a scalar function of a curve parameter, by sign change and then
// bisection.
//
// One routine for the ellipse and the spline, and for all three snaps, because
// each of them reduces to exactly this -- and three hand-written searches is
// how three tolerances end up disagreeing about whether a root is a root.
//
// Bisection rather than Newton on purpose: it converges more slowly but it
// cannot leave the bracket, and these functions are not always well behaved
// near a cusp or an inflection.
template <typename F>
void scan_roots(double lo, double hi, int samples, F f, std::vector<double>& out) {
    if (samples < 2) samples = 2;

    double prev_u = lo;
    double prev = f(lo);
    if (prev == 0.0) out.push_back(lo);

    for (int i = 1; i <= samples; ++i) {
        const double u = lo + (hi - lo) * (static_cast<double>(i) / static_cast<double>(samples));
        const double cur = f(u);

        if ((prev < 0.0 && cur > 0.0) || (prev > 0.0 && cur < 0.0)) {
            double a = prev_u;
            double b = u;
            double fa = prev;
            for (int k = 0; k < 60; ++k) {
                const double m = (a + b) * 0.5;
                const double fm = f(m);
                if ((fa < 0.0) == (fm < 0.0)) {
                    a = m;
                    fa = fm;
                } else {
                    b = m;
                }
            }
            out.push_back((a + b) * 0.5);
        } else if (cur == 0.0 && i < samples) {
            out.push_back(u);
        }

        prev_u = u;
        prev = cur;
    }
}

// An ellipse in its own orthonormal frame. Built to match Ellipse::point_at
// exactly -- centre + major*cos(t) + minor*sin(t) with minor = normal x major --
// so a parameter here and a parameter there mean the same place.
struct EllipseFrame {
    Vec3 centre{};
    Vec3 u{};  // unit, along the major axis
    Vec3 v{};  // unit, along the minor axis, in the entity's plane
    double a{1.0};
    double b{1.0};
    const Ellipse* src{nullptr};
};

bool as_ellipse(const Entity& e, EllipseFrame& f) {
    if (e.type() != EntityType::Ellipse) return false;
    const auto& el = static_cast<const Ellipse&>(e);

    f.a = el.major_length();
    f.b = el.minor_length();
    if (f.a <= kEps || f.b <= kEps) return false;

    const Vec3 major = el.major_axis();
    const Vec3 n = normalize(el.props().normal);
    const Vec3 minor = cross(n, major);
    if (is_zero(minor)) return false;

    f.centre = el.center();
    f.u = normalize(major);
    f.v = normalize(minor);
    f.src = &el;
    return true;
}

// On the drawn part? An elliptical arc sweeps counterclockwise in PARAMETER
// space exactly as an arc does in angle, so this mirrors Arc::contains_angle.
bool param_on_sweep(const EllipseFrame& f, double t) {
    if (f.src->is_full()) return true;
    return wrap_angle(t - f.src->start_param()) <= f.src->sweep() + kEps;
}

// Where the distance from `ref` is stationary. Those parameters are both the
// nearest point and the feet of every perpendicular -- an ellipse can have four
// -- so one function serves NEA and PER.
void ellipse_stationary(const EllipseFrame& f, const Vec3& ref, std::vector<double>& out) {
    const Vec3 d = ref - f.centre;
    // Dotting with the in-plane axes projects `ref` into the plane for free.
    const double px = dot(d, f.u);
    const double py = dot(d, f.v);

    // d/dt of |C(t) - ref|^2, with the constant factor of two dropped.
    scan_roots(0.0, kTwoPi, 24, [&](double t) {
        const double c = std::cos(t);
        const double s = std::sin(t);
        return (f.b * f.b - f.a * f.a) * s * c + f.a * px * s - f.b * py * c;
    }, out);
}

bool ellipse_nearest(const EllipseFrame& f, const Vec3& ref, Vec3* out, bool perpendicular) {
    std::vector<double> roots;
    ellipse_stationary(f, ref, roots);

    bool found = false;
    double best = 0.0;
    for (const double t : roots) {
        if (!param_on_sweep(f, t)) continue;
        const Vec3 p = f.src->point_at(t);
        const double d = length_sq(p - ref);
        if (!found || d < best) {
            best = d;
            *out = p;
            found = true;
        }
    }

    // NEA stays on the entity, so a reference past the end of an elliptical arc
    // answers with the end -- exactly as circular_nearest does. PER does not:
    // an endpoint is not a perpendicular foot.
    if (!perpendicular && !f.src->is_full()) {
        for (const Vec3& p : {f.src->start_point(), f.src->end_point()}) {
            const double d = length_sq(p - ref);
            if (!found || d < best) {
                best = d;
                *out = p;
                found = true;
            }
        }
    }
    return found;
}

// Tangents from a point to an ellipse, IN CLOSED FORM.
//
// The chord of contact -- the polar line of `ref` -- meets the ellipse exactly
// where the two tangents touch. Substituting the parametrisation into the polar
// reduces it to (px/a)cos t + (py/b)sin t = 1, which is one acos. So this is
// exact rather than iterated, and it is worth saying plainly that AutoCAD 2026
// gets this case wrong: there is no approximation here to be sloppy about.
int ellipse_tangents(const EllipseFrame& f, const Vec3& ref, Vec3 out[kMaxTangents]) {
    const Vec3 d = ref - f.centre;
    const double px = dot(d, f.u) / f.a;
    const double py = dot(d, f.v) / f.b;

    const double r = std::sqrt(px * px + py * py);
    if (r < 1.0 - 1e-9) return 0;  // strictly inside: no tangent exists

    const double phi = std::atan2(py, px);
    const double half = std::acos(std::clamp(1.0 / r, -1.0, 1.0));

    int count = 0;
    // On the curve, the two answers coincide and the tangent point is `ref`
    // itself -- the same reading the circular case gives.
    if (half <= 1e-9) {
        if (param_on_sweep(f, phi)) out[count++] = f.src->point_at(phi);
        return count;
    }
    for (const double sign : {1.0, -1.0}) {
        const double t = phi + sign * half;
        if (param_on_sweep(f, t)) out[count++] = f.src->point_at(t);
    }
    return count;
}

// How finely to look for roots. A spline can only wiggle as much as its control
// points let it, so the sample count follows them rather than being a constant
// that is too coarse for one curve and wasteful for another.
int spline_samples(const Spline& s) {
    const int n = static_cast<int>(s.control_points().size());
    return std::clamp(n * 8, 64, 2048);
}

bool spline_nearest(const Spline& s, const Vec3& ref, Vec3* out, bool perpendicular) {
    if (!s.valid()) return false;

    std::vector<double> roots;
    scan_roots(s.domain_min(), s.domain_max(), spline_samples(s), [&](double u) {
        return dot(s.point_at(u) - ref, s.tangent_at(u));
    }, roots);

    bool found = false;
    double best = 0.0;
    for (const double u : roots) {
        const Vec3 p = s.point_at(u);
        const double d = length_sq(p - ref);
        if (!found || d < best) {
            best = d;
            *out = p;
            found = true;
        }
    }

    if (!perpendicular) {
        for (const Vec3& p : {s.start_point(), s.end_point()}) {
            const double d = length_sq(p - ref);
            if (!found || d < best) {
                best = d;
                *out = p;
                found = true;
            }
        }
    }
    return found;
}

int spline_tangents(const Spline& s, const Vec3& ref, Vec3 out[kMaxTangents]) {
    if (!s.valid()) return 0;
    const Vec3 n = normalize(s.props().normal);

    std::vector<double> roots;
    scan_roots(s.domain_min(), s.domain_max(), spline_samples(s), [&](double u) {
        // The tangent line through `ref` touches where the chord from `ref` and
        // the curve's own direction are parallel.
        return dot(cross(s.point_at(u) - ref, s.tangent_at(u)), n);
    }, roots);

    // A spline can have many tangent points and a cursor can use two, so the
    // nearest win -- the same rule the block case uses for the same reason.
    std::sort(roots.begin(), roots.end(), [&](double x, double y) {
        return length_sq(s.point_at(x) - ref) < length_sq(s.point_at(y) - ref);
    });

    int count = 0;
    for (const double u : roots) {
        if (count >= kMaxTangents) break;
        const Vec3 p = s.point_at(u);
        bool duplicate = false;
        for (int i = 0; i < count; ++i) {
            if (near_equal(out[i], p, 1e-9)) duplicate = true;
        }
        if (!duplicate) out[count++] = p;
    }
    return count;
}

}  // namespace

// The best answer among a block reference's flattened contents.
//
// One helper for all three derived snaps, parameterised by which one, because
// they differ only in what "best" means -- and writing the descent three times
// is how the three would end up disagreeing about depth limits.
namespace {

template <typename Solve>
bool best_in_insert(const Entity& e, const Vec3& ref, Vec3* out, Solve solve) {
    if (e.type() != EntityType::Insert) return false;

    std::vector<EntityPtr> parts;
    flatten_insert(static_cast<const Insert&>(e), parts);

    bool found = false;
    double best = 0.0;
    for (const EntityPtr& part : parts) {
        Vec3 candidate{};
        if (!solve(*part, ref, &candidate)) continue;
        const double d = length(candidate - ref);
        if (!found || d < best) {
            best = d;
            *out = candidate;
            found = true;
        }
    }
    return found;
}

// The best answer among a polyline's segments.
//
// Solved segment by segment against the exact Line and Arc solvers rather than
// by sampling the flattened wireframe, so snapping to a polyline is as accurate
// as snapping to the same geometry drawn as separate entities -- which is
// exactly what a user will compare it against.
template <typename Solve>
bool best_in_polyline(const Entity& e, const Vec3& ref, Vec3* out, Solve solve) {
    if (e.type() != EntityType::Polyline) return false;
    const auto& p = static_cast<const Polyline&>(e);

    const std::size_t segments = p.segment_count();
    if (segments == 0) return false;

    bool found = false;
    double best = 0.0;
    for (std::size_t i = 0; i < segments; ++i) {
        Vec3 candidate{};
        bool ok = false;

        Vec3 centre{};
        double radius = 0.0;
        double start_angle = 0.0;
        double end_angle = 0.0;
        if (p.segment_arc(i, &centre, &radius, &start_angle, &end_angle)) {
            const Arc arc(centre, radius, start_angle, end_angle, p.props().normal);
            ok = solve(arc, ref, &candidate);
        } else {
            const Line seg(p.vertices()[i].pos, p.vertices()[(i + 1) % p.size()].pos);
            ok = solve(seg, ref, &candidate);
        }
        if (!ok) continue;

        const double d = length_sq(candidate - ref);
        if (!found || d < best) {
            best = d;
            *out = candidate;
            found = true;
        }
    }
    return found;
}

// Tangents to a polyline. Only its bulged segments have any, and the nearest
// one's win -- the same rule the block case uses, and for the same reason:
// offering every arc's tangents at once buries the one being pointed at.
int tangents_in_polyline(const Entity& e, const Vec3& ref, Vec3 out[kMaxTangents]) {
    if (e.type() != EntityType::Polyline) return 0;
    const auto& p = static_cast<const Polyline&>(e);

    int best_count = 0;
    double best = 0.0;
    for (std::size_t i = 0; i < p.segment_count(); ++i) {
        Vec3 centre{};
        double radius = 0.0;
        double start_angle = 0.0;
        double end_angle = 0.0;
        if (!p.segment_arc(i, &centre, &radius, &start_angle, &end_angle)) continue;

        const Arc arc(centre, radius, start_angle, end_angle, p.props().normal);
        Vec3 candidates[kMaxTangents];
        const int n = tangent_points(arc, ref, candidates);
        if (n == 0) continue;

        const double d = length_sq(candidates[0] - ref);
        if (best_count == 0 || d < best) {
            best = d;
            best_count = n;
            for (int k = 0; k < n; ++k) out[k] = candidates[k];
        }
    }
    return best_count;
}

// PERPENDICULAR on a polyline needs its own rule rather than best_in_polyline.
//
// perpendicular_point() on a Line is unclamped on purpose, so a segment happily
// reports a foot far past its own ends -- and on a polyline such a foot is not
// perpendicular to anything the entity actually draws. Worse, being unclamped it
// is often the CLOSER answer, so a plain nearest-wins search picks it.
//
// So a foot that lands on its own segment always beats one that does not, and
// the extended feet compete only when no segment has a real one. Comparing
// against the clamped answer is how "on its own segment" is tested: nearest_point
// clamps, perpendicular_point does not, and they agree exactly when the foot is
// inside.
bool perpendicular_in_polyline(const Polyline& p, const Vec3& ref, Vec3* out) {
    bool found_on = false;
    bool found_off = false;
    double best_on = 0.0;
    double best_off = 0.0;
    Vec3 on{};
    Vec3 off{};

    for (std::size_t i = 0; i < p.segment_count(); ++i) {
        Vec3 centre{};
        double radius = 0.0;
        double start_angle = 0.0;
        double end_angle = 0.0;
        const bool is_arc = p.segment_arc(i, &centre, &radius, &start_angle, &end_angle);

        Vec3 foot{};
        Vec3 clamped{};
        bool ok = false;
        bool ok_clamped = false;
        if (is_arc) {
            const Arc arc(centre, radius, start_angle, end_angle, p.props().normal);
            ok = perpendicular_point(arc, ref, &foot);
            ok_clamped = nearest_point(arc, ref, &clamped);
        } else {
            const Line seg(p.vertices()[i].pos, p.vertices()[(i + 1) % p.size()].pos);
            ok = perpendicular_point(seg, ref, &foot);
            ok_clamped = nearest_point(seg, ref, &clamped);
        }
        if (!ok) continue;

        const double d = length_sq(foot - ref);
        if (ok_clamped && near_equal(foot, clamped, 1e-9)) {
            if (!found_on || d < best_on) {
                best_on = d;
                on = foot;
                found_on = true;
            }
        } else if (!found_off || d < best_off) {
            best_off = d;
            off = foot;
            found_off = true;
        }
    }

    if (found_on) {
        *out = on;
        return true;
    }
    if (found_off) {
        *out = off;
        return true;
    }
    return false;
}

bool perpendicular_on_curve(const Entity& e, const Vec3& ref, Vec3* out) {
    if (e.type() == EntityType::Polyline) {
        return perpendicular_in_polyline(static_cast<const Polyline&>(e), ref, out);
    }

    EllipseFrame f;
    if (as_ellipse(e, f)) return ellipse_nearest(f, ref, out, true);

    if (e.type() == EntityType::Spline) {
        return spline_nearest(static_cast<const Spline&>(e), ref, out, true);
    }
    return false;
}

}  // namespace

bool nearest_point(const Entity& e, const Vec3& ref, Vec3* out) {
    if (!out) return false;

    if (e.type() == EntityType::Insert) {
        return best_in_insert(e, ref, out,
                              [](const Entity& c, const Vec3& r, Vec3* o) {
                                  return nearest_point(c, r, o);
                              });
    }

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

    if (e.type() == EntityType::Polyline) {
        return best_in_polyline(e, ref, out,
                                [](const Entity& seg, const Vec3& r, Vec3* o) {
                                    return nearest_point(seg, r, o);
                                });
    }

    EllipseFrame f;
    if (as_ellipse(e, f)) return ellipse_nearest(f, ref, out, false);

    if (e.type() == EntityType::Spline) {
        return spline_nearest(static_cast<const Spline&>(e), ref, out, false);
    }
    return false;
}

bool perpendicular_point(const Entity& e, const Vec3& ref, Vec3* out) {
    if (!out) return false;

    if (e.type() == EntityType::Insert) {
        return best_in_insert(e, ref, out,
                              [](const Entity& c, const Vec3& r, Vec3* o) {
                                  return perpendicular_point(c, r, o);
                              });
    }

    if (const Line* l = as_line(e)) {
        // Unclamped on purpose: see the header.
        return line_foot(*l, ref, out, nullptr);
    }

    Circular c;
    if (!as_circular(e, c)) return perpendicular_on_curve(e, ref, out);

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
    if (!out) return 0;

    if (e.type() == EntityType::Insert) {
        // The nearest circular thing inside wins. Offering tangents to every
        // circle in a block at once would bury the one being pointed at.
        std::vector<EntityPtr> parts;
        flatten_insert(static_cast<const Insert&>(e), parts);

        int best_count = 0;
        double best = 0.0;
        for (const EntityPtr& part : parts) {
            Vec3 candidates[kMaxTangents];
            const int n = tangent_points(*part, ref, candidates);
            if (n == 0) continue;
            const double d = length(candidates[0] - ref);
            if (best_count == 0 || d < best) {
                best = d;
                best_count = n;
                for (int i = 0; i < n; ++i) out[i] = candidates[i];
            }
        }
        return best_count;
    }

    if (e.type() == EntityType::Polyline) return tangents_in_polyline(e, ref, out);

    EllipseFrame ef;
    if (as_ellipse(e, ef)) return ellipse_tangents(ef, ref, out);

    if (e.type() == EntityType::Spline) {
        return spline_tangents(static_cast<const Spline&>(e), ref, out);
    }

    Circular c;
    if (!as_circular(e, c)) return 0;

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
