// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The intersection kernel.
//
// The structure worth knowing before reading: every entity is decomposed into
// SubCurves -- a line is one, a circle or arc is one, a polyline is one per
// segment -- and every pair of sub-curves goes through the same three
// primitives. That is what makes polylines fall out for free rather than
// needing a fourth case, and it is why a bulged polyline segment meets a circle
// by exactly the arithmetic a standalone ARC would use.
#include "noto/intersect.hpp"

#include "noto/ecs.hpp"
#include "noto/entities.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace noto {
namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

double normalize_angle(double a) {
    a = std::fmod(a, kTwoPi);
    return (a < 0.0) ? a + kTwoPi : a;
}

// One piece of an entity that the primitives can handle directly.
//
// A full circle is spelled as an arc of sweep 2*pi rather than as its own kind,
// so the sweep filter has no special case to forget.
struct SubCurve {
    bool is_arc{false};

    // Straight.
    Vec3 p0{};
    Vec3 p1{};

    // Circular.
    Vec3 centre{};
    double radius{0.0};
    Vec3 normal{kWorldZ};
    double start_angle{0.0};
    double sweep{kTwoPi};

    // Where this piece sits in its parent entity's 0..1 parameter, so a
    // polyline segment's local answer can be reported in the polyline's terms.
    double t_lo{0.0};
    double t_hi{1.0};

    // Whether this piece may be extended to its carrier. False for polyline
    // segments; see the header for why a polyline has no carrier.
    bool extendable{true};
};

// Decomposes an entity. Empty for anything with no curve.
std::vector<SubCurve> decompose(const Entity& e) {
    std::vector<SubCurve> out;

    switch (e.type()) {
        case EntityType::Line: {
            const Line& l = static_cast<const Line&>(e);
            SubCurve s;
            s.p0 = l.start();
            s.p1 = l.end();
            out.push_back(s);
            break;
        }

        case EntityType::Circle: {
            const Circle& c = static_cast<const Circle&>(e);
            SubCurve s;
            s.is_arc = true;
            s.centre = c.center();
            s.radius = c.radius();
            s.normal = c.props().normal;
            s.start_angle = 0.0;
            s.sweep = kTwoPi;
            out.push_back(s);
            break;
        }

        case EntityType::Arc: {
            const Arc& a = static_cast<const Arc&>(e);
            SubCurve s;
            s.is_arc = true;
            s.centre = a.center();
            s.radius = a.radius();
            s.normal = a.props().normal;
            s.start_angle = a.start_angle();
            s.sweep = a.sweep();
            out.push_back(s);
            break;
        }

        case EntityType::Polyline: {
            const Polyline& p = static_cast<const Polyline&>(e);
            const std::size_t n = p.segment_count();
            if (n == 0) break;
            const double step = 1.0 / static_cast<double>(n);

            for (std::size_t i = 0; i < n; ++i) {
                SubCurve s;
                s.t_lo = static_cast<double>(i) * step;
                s.t_hi = static_cast<double>(i + 1) * step;
                // Only the TERMINAL segments of an open polyline have a
                // carrier worth extending -- which is exactly what EXTEND grows.
                // An interior segment's extension runs into its own neighbours
                // and means nothing, and a closed polyline has no terminal
                // segment at all.
                //
                // A hit found by extending a terminal segment inwards maps back
                // to a parent parameter inside [0, 1], so it reads as an
                // ordinary interior hit and EXTEND ignores it. The direction
                // needs no separate guard.
                s.extendable = !p.closed() && (i == 0 || i + 1 == n);

                Vec3 centre{};
                double radius = 0.0;
                double a0 = 0.0;
                double a1 = 0.0;
                if (p.segment_arc(i, &centre, &radius, &a0, &a1)) {
                    s.is_arc = true;
                    s.centre = centre;
                    s.radius = radius;
                    s.normal = p.props().normal;
                    // The sweep is SIGNED, and must be: a negative bulge sweeps
                    // clockwise, and starting from the other end instead would
                    // run the segment's parameter backwards against the
                    // polyline's direction of travel. Every parameter reported
                    // for a hit on such a segment would then be mirrored within
                    // it, which is wrong for TRIM and invisible until something
                    // cuts a curve there.
                    s.start_angle = a0;
                    s.sweep = 4.0 * std::atan(p.vertices()[i].bulge);
                } else {
                    s.p0 = p.vertices()[i].pos;
                    s.p1 = p.vertices()[(i + 1) % p.size()].pos;
                }
                out.push_back(s);
            }
            break;
        }

        default: break;  // POINT, TEXT, SOLID, 3DFACE, PROXY have no curve
    }
    return out;
}

// A block reference's contents, as sub-curves in world space.
//
// Kept separate from decompose() because it owns the flattened copies for as
// long as the sub-curves referring to them are in use. The sub-curves hold
// coordinates rather than pointers, so the copies could be dropped immediately
// -- but that is a property of the current SubCurve and not one worth relying
// on silently.
struct FlatInsert {
    std::vector<EntityPtr> parts;
    std::vector<SubCurve> curves;
};

FlatInsert decompose_insert(const Insert& ins) {
    FlatInsert flat;
    flatten_insert(ins, flat.parts);

    // Parameters are assigned by index, evenly. An insert has no natural
    // parameterisation of its own, and it does not need one: R12 lets a block
    // be a TRIM cutting edge but never a trimmed object, so only the OTHER
    // curve's parameter is ever acted on.
    const std::size_t n = flat.parts.size();
    for (std::size_t i = 0; i < n; ++i) {
        std::vector<SubCurve> sub = decompose(*flat.parts[i]);
        const double lo = static_cast<double>(i) / static_cast<double>(n ? n : 1);
        const double span = 1.0 / static_cast<double>(n ? n : 1);
        for (SubCurve& s : sub) {
            s.t_lo = lo + s.t_lo * span;
            s.t_hi = lo + s.t_hi * span;
            s.extendable = false;  // as with a polyline, there is no carrier
            flat.curves.push_back(s);
        }
    }
    return flat;
}

// The fraction of the way along a sub-curve, from the raw parameter the
// primitives produce. For an arc that means turning a whole-circle fraction
// into a position along the sweep, which is where the direction matters.
// Where an angle falls along a possibly-clockwise sweep, as a fraction of it.
//
// One place that understands a signed sweep, so that a clockwise arc segment is
// not a special case anywhere else.
double fraction_along_sweep(double angle, double start_angle, double sweep) {
    const double magnitude = std::abs(sweep);
    if (magnitude < kIntersectTol) return 0.0;

    // Measured in the direction the sweep actually goes.
    double delta = (sweep >= 0.0) ? normalize_angle(angle - start_angle)
                                  : normalize_angle(start_angle - angle);

    if (magnitude >= kTwoPi - kIntersectTol) return delta / kTwoPi;  // a full circle

    // Past the end, report the shorter way round as a negative parameter rather
    // than as almost a full turn -- EXTEND asks "how far beyond the end", and a
    // value just under 1 would say the opposite.
    if (delta > magnitude + (kTwoPi - magnitude) * 0.5) delta -= kTwoPi;
    return delta / magnitude;
}

double local_parameter(const SubCurve& s, double raw) {
    if (!s.is_arc) return raw;
    // `raw` is the fraction of a full turn measured from the ECS X axis.
    return fraction_along_sweep(raw * kTwoPi, s.start_angle, s.sweep);
}

bool within_unit(double t) {
    return t >= -kIntersectTol && t <= 1.0 + kIntersectTol;
}

// Appends the intersections of two sub-curves, in the parents' parameters.
void intersect_sub(const SubCurve& a, const SubCurve& b, IntersectMode mode,
                   IntersectionList& out) {
    IntersectionList raw;

    if (!a.is_arc && !b.is_arc) {
        intersect_line_line(a.p0, a.p1, b.p0, b.p1, raw);
    } else if (!a.is_arc && b.is_arc) {
        intersect_line_circle(a.p0, a.p1, b.centre, b.radius, b.normal, raw);
    } else if (a.is_arc && !b.is_arc) {
        // Solved the one way round, then swapped, so there is one line/circle
        // routine rather than two that must agree.
        IntersectionList flipped;
        intersect_line_circle(b.p0, b.p1, a.centre, a.radius, a.normal, flipped);
        for (const Intersection& i : flipped) {
            Intersection s = i;
            std::swap(s.t0, s.t1);
            raw.push_back(s);
        }
    } else {
        intersect_circle_circle(a.centre, a.radius, a.normal, b.centre, b.radius, b.normal, raw);
    }

    for (Intersection hit : raw) {
        const double la = local_parameter(a, hit.t0);
        const double lb = local_parameter(b, hit.t1);

        const bool in_a = within_unit(la);
        const bool in_b = within_unit(lb);

        // A sub-curve that cannot be extended only ever reports what is on it,
        // whatever the caller asked for.
        const bool allow_a = in_a || (mode == IntersectMode::Extended && a.extendable);
        const bool allow_b = in_b || (mode == IntersectMode::Extended && b.extendable);
        if (!allow_a || !allow_b) continue;

        hit.t0 = a.t_lo + la * (a.t_hi - a.t_lo);
        hit.t1 = b.t_lo + lb * (b.t_hi - b.t_lo);
        hit.within0 = in_a;
        hit.within1 = in_b;
        out.push_back(hit);
    }
}

}  // namespace

// --- the primitives ---------------------------------------------------------

void intersect_line_line(const Vec3& a0, const Vec3& a1, const Vec3& b0, const Vec3& b1,
                         IntersectionList& out) {
    const Vec3 da = a1 - a0;
    const Vec3 db = b1 - b0;

    const double aa = dot(da, da);
    const double bb = dot(db, db);
    if (aa < kIntersectTol || bb < kIntersectTol) return;  // a degenerate line

    const double ab = dot(da, db);
    const Vec3 w = a0 - b0;
    const double d = dot(da, w);
    const double e = dot(db, w);

    const double denom = aa * bb - ab * ab;
    // Parallel. Collinear overlap is deliberately not reported: an overlap is a
    // range rather than a point, and every caller here wants points. TRIM
    // against a collinear edge is a degenerate case R12 declines as well.
    if (std::abs(denom) < kIntersectTol * aa * bb) return;

    const double ta = (ab * e - bb * d) / denom;
    const double tb = (aa * e - ab * d) / denom;

    const Vec3 pa = a0 + da * ta;
    const Vec3 pb = b0 + db * tb;

    // The closest-approach points coincide only if the lines really meet.
    // Skew lines that merely cross in projection fail here, which is the rule
    // osnap INT already follows and the reason this is 3-space arithmetic
    // rather than a 2D solve after flattening.
    if (length(pa - pb) > kIntersectTol) return;

    Intersection hit;
    hit.point = (pa + pb) * 0.5;
    hit.t0 = ta;
    hit.t1 = tb;
    out.push_back(hit);
}

void intersect_line_circle(const Vec3& a0, const Vec3& a1, const Vec3& centre, double radius,
                           const Vec3& normal, IntersectionList& out) {
    if (radius <= kIntersectTol) return;

    const Vec3 d = a1 - a0;
    if (dot(d, d) < kIntersectTol) return;

    const Vec3 n = normalize(normal);
    const Basis basis = arbitrary_axis(n);

    // The circle's parameter for a point on it: the angle in its own plane,
    // as a fraction of a full turn.
    auto circle_t = [&](const Vec3& p) {
        const Vec3 r = p - centre;
        return normalize_angle(std::atan2(dot(r, basis.ay), dot(r, basis.ax))) / kTwoPi;
    };

    const double along = dot(d, n);
    const double offset = dot(a0 - centre, n);

    if (std::abs(along) > kIntersectTol) {
        // The line crosses the circle's plane at exactly one point, so it can
        // only meet the circle if that point happens to sit on it. Generic
        // lines in space therefore miss a circle entirely, which is correct and
        // is the 3D behaviour a flattened solve would get wrong.
        const double t = -offset / along;
        const Vec3 p = a0 + d * t;
        if (std::abs(length(p - centre) - radius) > kIntersectTol) return;

        Intersection hit;
        hit.point = p;
        hit.t0 = t;
        hit.t1 = circle_t(p);
        out.push_back(hit);
        return;
    }

    // Parallel to the plane: either coplanar, or missing it entirely.
    if (std::abs(offset) > kIntersectTol) return;

    // Coplanar, so it is the ordinary quadratic.
    const Vec3 f = a0 - centre;
    const double qa = dot(d, d);
    const double qb = 2.0 * dot(f, d);
    const double qc = dot(f, f) - radius * radius;

    const double disc = qb * qb - 4.0 * qa * qc;
    if (disc < 0.0) return;

    const double root = std::sqrt(std::max(0.0, disc));
    const double t0 = (-qb - root) / (2.0 * qa);
    const double t1 = (-qb + root) / (2.0 * qa);

    auto emit = [&](double t) {
        const Vec3 p = a0 + d * t;
        Intersection hit;
        hit.point = p;
        hit.t0 = t;
        hit.t1 = circle_t(p);
        out.push_back(hit);
    };

    emit(t0);
    // A tangent line touches once; emitting it twice would make TRIM believe
    // there are two pieces where there is one.
    if (root > kIntersectTol) emit(t1);
}

void intersect_circle_circle(const Vec3& c0, double r0, const Vec3& n0, const Vec3& c1, double r1,
                             const Vec3& n1, IntersectionList& out) {
    if (r0 <= kIntersectTol || r1 <= kIntersectTol) return;

    const Vec3 u0 = normalize(n0);
    const Vec3 u1 = normalize(n1);
    const Basis b0 = arbitrary_axis(u0);
    const Basis b1 = arbitrary_axis(u1);

    auto param = [](const Basis& b, const Vec3& centre, const Vec3& p) {
        const Vec3 r = p - centre;
        return normalize_angle(std::atan2(dot(r, b.ay), dot(r, b.ax))) / kTwoPi;
    };

    auto emit = [&](const Vec3& p) {
        Intersection hit;
        hit.point = p;
        hit.t0 = param(b0, c0, p);
        hit.t1 = param(b1, c1, p);
        out.push_back(hit);
    };

    const bool parallel_planes = is_zero(cross(u0, u1), 1e-9);

    if (parallel_planes) {
        // Parallel but offset planes never meet.
        if (std::abs(dot(c1 - c0, u0)) > 1e-7) return;

        // Coplanar: the classic radical-line construction.
        const Vec3 delta = c1 - c0;
        const double dist = length(delta);
        if (dist <= kIntersectTol) return;                        // concentric
        if (dist > r0 + r1 + kIntersectTol) return;               // too far apart
        if (dist < std::abs(r0 - r1) - kIntersectTol) return;     // one inside the other

        const double x = (dist * dist + r0 * r0 - r1 * r1) / (2.0 * dist);
        const double h = std::sqrt(std::max(0.0, r0 * r0 - x * x));

        const Vec3 axis = delta / dist;
        const Vec3 across = cross(u0, axis);  // unit: u0 and axis are orthonormal
        const Vec3 mid = c0 + axis * x;

        emit(mid + across * h);
        if (h > kIntersectTol) emit(mid - across * h);
        return;
    }

    // Non-coplanar. The two planes meet in a line, and a point common to both
    // circles must lie on it -- so solve for where circle 0 crosses plane 1,
    // then keep whichever of those points is also at circle 1's radius.
    //
    // On circle 0, p(theta) = c0 + r0 * (ax cos + ay sin), and lying in plane 1
    // means dot(p - c1, u1) == 0. That is A cos + B sin + D == 0, which is one
    // shifted cosine and has at most two roots.
    const double A = dot(b0.ax, u1) * r0;
    const double B = dot(b0.ay, u1) * r0;
    const double D = dot(c0 - c1, u1);

    const double amp = std::hypot(A, B);
    if (amp < kIntersectTol) return;  // circle 0 is parallel to plane 1
    const double ratio = -D / amp;
    if (std::abs(ratio) > 1.0 + kIntersectTol) return;

    const double phi = std::atan2(B, A);
    const double delta_angle = std::acos(std::clamp(ratio, -1.0, 1.0));

    for (const double sign : {1.0, -1.0}) {
        const double theta = phi + sign * delta_angle;
        const Vec3 p = c0 + (b0.ax * std::cos(theta) + b0.ay * std::sin(theta)) * r0;
        // Crossing the plane is necessary but not sufficient: it also has to
        // land on the other circle rather than merely in its plane.
        if (std::abs(length(p - c1) - r1) > kIntersectTol) continue;
        emit(p);
        if (delta_angle < kIntersectTol) break;  // tangential: one point, not two
    }
}

// --- entities ---------------------------------------------------------------

std::size_t intersect(const Entity& a, const Entity& b, IntersectMode mode,
                      IntersectionList& out) {
    const std::size_t before = out.size();

    // Block references are flattened rather than special-cased in the
    // primitives, so a circle inside a block meets a line by exactly the
    // arithmetic a loose circle would.
    FlatInsert flat_a;
    FlatInsert flat_b;
    if (a.type() == EntityType::Insert) flat_a = decompose_insert(static_cast<const Insert&>(a));
    if (b.type() == EntityType::Insert) flat_b = decompose_insert(static_cast<const Insert&>(b));

    const std::vector<SubCurve> own_a =
        (a.type() == EntityType::Insert) ? std::vector<SubCurve>{} : decompose(a);
    const std::vector<SubCurve> own_b =
        (b.type() == EntityType::Insert) ? std::vector<SubCurve>{} : decompose(b);

    const std::vector<SubCurve>& sa = (a.type() == EntityType::Insert) ? flat_a.curves : own_a;
    const std::vector<SubCurve>& sb = (b.type() == EntityType::Insert) ? flat_b.curves : own_b;

    for (const SubCurve& ca : sa) {
        for (const SubCurve& cb : sb) {
            intersect_sub(ca, cb, mode, out);
        }
    }
    return out.size() - before;
}

bool curve_is_closed(const Entity& e) {
    if (e.type() == EntityType::Circle) return true;
    if (e.type() == EntityType::Polyline) return static_cast<const Polyline&>(e).closed();
    return false;
}

bool curve_parameter_at(const Entity& e, const Vec3& p, double* out) {
    if (!out) return false;

    const std::vector<SubCurve> subs = decompose(e);
    if (subs.empty()) return false;

    bool found = false;
    double best_distance = 0.0;
    double best_t = 0.0;

    for (const SubCurve& s : subs) {
        double local = 0.0;
        Vec3 on{};

        if (!s.is_arc) {
            const Vec3 d = s.p1 - s.p0;
            const double len_sq = dot(d, d);
            if (len_sq < kIntersectTol) continue;
            // Clamped: a point beyond the end belongs to the end, which is what
            // makes BREAK's "second point past the end" trim rather than fail.
            local = std::clamp(dot(p - s.p0, d) / len_sq, 0.0, 1.0);
            on = s.p0 + d * local;
        } else {
            const Basis b = arbitrary_axis(s.normal);
            const Vec3 radial = p - s.centre;
            const double angle = std::atan2(dot(radial, b.ay), dot(radial, b.ax));

            // Clamped to the arc, so a pick past the end lands on the end --
            // the same rule the straight case uses.
            local = std::clamp(fraction_along_sweep(angle, s.start_angle, s.sweep), 0.0, 1.0);

            const double at = s.start_angle + s.sweep * local;
            on = s.centre + (b.ax * std::cos(at) + b.ay * std::sin(at)) * s.radius;
        }

        const double distance = length(on - p);
        if (!found || distance < best_distance) {
            found = true;
            best_distance = distance;
            best_t = s.t_lo + local * (s.t_hi - s.t_lo);
        }
    }

    if (found) *out = best_t;
    return found;
}

bool curve_point_at(const Entity& e, double t, Vec3* out) {
    if (!out) return false;

    const std::vector<SubCurve> subs = decompose(e);
    if (subs.empty()) return false;

    // A polyline is the only entity with more than one piece, and it does not
    // extend, so a parameter off either end has no answer.
    if (subs.size() > 1 && !within_unit(t)) return false;

    // Find the piece this parameter falls in. Clamped at the top so t == 1
    // lands on the last segment rather than off the end of the list.
    std::size_t index = 0;
    for (std::size_t i = 0; i < subs.size(); ++i) {
        if (t >= subs[i].t_lo - kIntersectTol) index = i;
    }
    const SubCurve& s = subs[index];

    const double span = s.t_hi - s.t_lo;
    const double local = (span > kIntersectTol) ? (t - s.t_lo) / span : 0.0;

    if (!s.is_arc) {
        *out = s.p0 + (s.p1 - s.p0) * local;
        return true;
    }

    const Basis basis = arbitrary_axis(s.normal);
    const double angle = s.start_angle + s.sweep * local;
    *out = s.centre + (basis.ax * std::cos(angle) + basis.ay * std::sin(angle)) * s.radius;
    return true;
}

}  // namespace noto
