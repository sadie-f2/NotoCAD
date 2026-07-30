// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Derived object snaps on POLYLINE, ELLIPSE and SPLINE -- the three that had
// none at all until now.
//
// Everything here is checked against the DEFINING CONDITION rather than against
// a reference implementation: a nearest point is where the chord meets the
// curve at a right angle, a tangent point is where the chord is parallel to the
// curve. That is deliberate. AutoCAD 2026 gets tangents to an ellipse wrong and
// is inconsistent about tangents to a spline, so matching it would be matching a
// bug. Curve directions here come from finite differences, so the test does not
// borrow the algebra it is checking.

#include "test.hpp"

#include "noto/entities.hpp"
#include "noto/osnap.hpp"
#include "noto/osnap_derived.hpp"

#include <cmath>
#include <numbers>
#include <vector>

using namespace noto;

namespace {

constexpr double kPi = std::numbers::pi;

// Curve direction by finite difference, so the check is independent of how the
// solver computes it.
Vec3 ellipse_dir(const Ellipse& e, const Vec3& at) {
    // Recover the parameter by search: cheap, and avoids assuming the frame.
    double best_t = 0.0;
    double best = 1e30;
    for (int i = 0; i <= 20000; ++i) {
        const double t = kPi * 2.0 * static_cast<double>(i) / 20000.0;
        const double d = length_sq(e.point_at(t) - at);
        if (d < best) {
            best = d;
            best_t = t;
        }
    }
    const double h = 1e-6;
    return normalize(e.point_at(best_t + h) - e.point_at(best_t - h));
}

bool on_ellipse(const Ellipse& e, const Vec3& p, double tol) {
    const Vec3 d = p - e.center();
    const Vec3 n = normalize(e.props().normal);
    const Vec3 u = normalize(e.major_axis());
    const Vec3 v = normalize(cross(n, e.major_axis()));
    const double x = dot(d, u) / e.major_length();
    const double y = dot(d, v) / e.minor_length();
    return std::abs(x * x + y * y - 1.0) < tol;
}

}  // namespace

// --- ELLIPSE ----------------------------------------------------------------

TEST_CASE("ellipse: NEAREST lands on the curve, at a right angle to the chord") {
    const Ellipse e({0, 0, 0}, {10, 0, 0}, 0.4);

    for (const Vec3& ref : {Vec3{20, 15, 0}, Vec3{-3, 9, 0}, Vec3{0.5, 0.2, 0}, Vec3{14, 0, 0}}) {
        Vec3 got{};
        REQUIRE(nearest_point(e, ref, &got));
        CHECK(on_ellipse(e, got, 1e-7));

        // The defining condition: the chord meets the curve perpendicularly.
        const Vec3 chord = ref - got;
        if (length(chord) > 1e-6) {
            CHECK(std::abs(dot(normalize(chord), ellipse_dir(e, got))) < 1e-3);
        }
    }
}

TEST_CASE("ellipse: NEAREST beats a coarse sampling of the curve") {
    // The real property NEAREST has to have, stated without reference to how it
    // is computed: nothing else on the curve is closer.
    const Ellipse e({1, 2, 0}, {7, 3, 0}, 0.55);
    const Vec3 ref{-4, 9, 0};

    Vec3 got{};
    REQUIRE(nearest_point(e, ref, &got));

    double best = 1e30;
    for (int i = 0; i < 4000; ++i) {
        const double t = kPi * 2.0 * static_cast<double>(i) / 4000.0;
        best = std::min(best, length_sq(e.point_at(t) - ref));
    }
    CHECK(length_sq(got - ref) <= best + 1e-9);
}

TEST_CASE("ellipse: TANGENT touches where the chord is parallel to the curve") {
    const Ellipse e({0, 0, 0}, {6, 0, 0}, 0.5);
    const Vec3 ref{14, 9, 0};

    Vec3 pts[kMaxTangents];
    const int n = tangent_points(e, ref, pts);
    CHECK(n == 2);

    for (int i = 0; i < n; ++i) {
        CHECK(on_ellipse(e, pts[i], 1e-7));
        // Tangency: the chord from `ref` runs along the curve, not across it.
        const Vec3 chord = normalize(pts[i] - ref);
        const Vec3 dir = ellipse_dir(e, pts[i]);
        CHECK(length(cross(chord, dir)) < 1e-3);
    }
}

TEST_CASE("ellipse: TANGENT has no answer from inside and one from on the curve") {
    const Ellipse e({0, 0, 0}, {6, 0, 0}, 0.5);
    Vec3 pts[kMaxTangents];

    CHECK(tangent_points(e, {0, 0, 0}, pts) == 0);
    CHECK(tangent_points(e, {1, 1, 0}, pts) == 0);

    // On the curve, the tangent point is the point itself -- the same reading
    // the circular case gives.
    const Vec3 on = e.point_at(0.9);
    CHECK(tangent_points(e, on, pts) == 1);
    CHECK(near_equal(pts[0], on, 1e-6));
}

TEST_CASE("ellipse: with ratio 1 it agrees with the circle solver") {
    // The strongest available cross-check: a ratio-1 ellipse IS a circle, so
    // three new code paths have to reproduce three trusted ones exactly.
    const Ellipse e({2, 3, 0}, {5, 0, 0}, 1.0);
    const Circle c({2, 3, 0}, 5.0);
    const Vec3 ref{11, 7, 0};

    Vec3 a{}, b{};
    REQUIRE(nearest_point(e, ref, &a));
    REQUIRE(nearest_point(c, ref, &b));
    CHECK(near_equal(a, b, 1e-7));

    REQUIRE(perpendicular_point(e, ref, &a));
    REQUIRE(perpendicular_point(c, ref, &b));
    CHECK(near_equal(a, b, 1e-7));

    Vec3 ea[kMaxTangents], ca[kMaxTangents];
    const int ne = tangent_points(e, ref, ea);
    const int nc = tangent_points(c, ref, ca);
    CHECK(ne == 2);
    CHECK(nc == 2);
    // Either order is legitimate; both points must appear.
    for (int i = 0; i < ne; ++i) {
        CHECK(near_equal(ea[i], ca[0], 1e-6) || near_equal(ea[i], ca[1], 1e-6));
    }
}

TEST_CASE("ellipse: a tilted one snaps in its own plane") {
    // If the frame were wrong this would land off the curve entirely, which is
    // the failure ECS exists to prevent.
    const Ellipse e({0, 0, 0}, {8, 0, 0}, 0.5, 0.0, kPi * 2.0, {0, 1, 1});
    Vec3 got{};
    REQUIRE(nearest_point(e, {3, 9, 2}, &got));
    CHECK(on_ellipse(e, got, 1e-6));
    // And it stays in the entity's plane rather than drifting out of it.
    CHECK(std::abs(dot(got - e.center(), normalize(e.props().normal))) < 1e-7);
}

TEST_CASE("ellipse: an arc offers only what it draws") {
    // Half an ellipse, the upper side.
    const Ellipse arc({0, 0, 0}, {10, 0, 0}, 0.5, 0.0, kPi);

    // A reference below the curve: the perpendicular foot is on the missing
    // half, so NEAREST answers with an end instead of a point that is not drawn.
    Vec3 got{};
    REQUIRE(nearest_point(arc, {0, -20, 0}, &got));
    const bool at_end = near_equal(got, arc.start_point(), 1e-6) ||
                        near_equal(got, arc.end_point(), 1e-6);
    CHECK(at_end);
}

// --- SPLINE -----------------------------------------------------------------

TEST_CASE("spline: NEAREST is perpendicular, or it is an end") {
    const EntityPtr s = Spline::interpolating({{0, 0, 0}, {5, 8, 0}, {12, -3, 0}, {18, 6, 0}});
    REQUIRE(s != nullptr);
    const Spline& sp = static_cast<const Spline&>(*s);

    for (const Vec3& ref : {Vec3{6, 12, 0}, Vec3{10, -9, 0}, Vec3{-6, 0, 0}}) {
        Vec3 got{};
        REQUIRE(nearest_point(*s, ref, &got));

        const bool at_end = near_equal(got, sp.start_point(), 1e-6) ||
                            near_equal(got, sp.end_point(), 1e-6);
        if (at_end) continue;

        // Nothing on the curve should be closer.
        const double lo = sp.domain_min(), hi = sp.domain_max();
        double best = 1e30;
        for (int i = 0; i <= 5000; ++i) {
            const double u = lo + (hi - lo) * static_cast<double>(i) / 5000.0;
            best = std::min(best, length_sq(sp.point_at(u) - ref));
        }
        CHECK(length_sq(got - ref) <= best + 1e-6);
    }
}

TEST_CASE("spline: TANGENT touches where the chord runs along the curve") {
    const EntityPtr s = Spline::interpolating({{0, 0, 0}, {6, 7, 0}, {14, -2, 0}, {20, 5, 0}});
    REQUIRE(s != nullptr);
    const Spline& sp = static_cast<const Spline&>(*s);
    const Vec3 ref{10, 20, 0};

    Vec3 pts[kMaxTangents];
    const int n = tangent_points(*s, ref, pts);
    REQUIRE(n >= 1);

    for (int i = 0; i < n; ++i) {
        // Recover the parameter, then check tangency by finite difference.
        double best_u = 0.0, best = 1e30;
        const double lo = sp.domain_min(), hi = sp.domain_max();
        for (int k = 0; k <= 20000; ++k) {
            const double u = lo + (hi - lo) * static_cast<double>(k) / 20000.0;
            const double d = length_sq(sp.point_at(u) - pts[i]);
            if (d < best) {
                best = d;
                best_u = u;
            }
        }
        const double h = (hi - lo) * 1e-6;
        const Vec3 dir = normalize(sp.point_at(best_u + h) - sp.point_at(best_u - h));
        CHECK(length(cross(normalize(pts[i] - ref), dir)) < 1e-3);
    }
}

TEST_CASE("spline: a straight one agrees with the line solver") {
    // Collinear fit points make a spline that is a segment, so the numeric path
    // has to reproduce the exact one.
    const EntityPtr s = Spline::interpolating({{0, 0, 0}, {4, 4, 0}, {8, 8, 0}, {12, 12, 0}});
    REQUIRE(s != nullptr);
    const Line l({0, 0, 0}, {12, 12, 0});
    const Vec3 ref{10, 2, 0};

    Vec3 a{}, b{};
    REQUIRE(nearest_point(*s, ref, &a));
    REQUIRE(nearest_point(l, ref, &b));
    CHECK(near_equal(a, b, 1e-5));
}

// --- POLYLINE ---------------------------------------------------------------

TEST_CASE("polyline: NEAREST picks the right segment and is exact on it") {
    Polyline p;
    p.add({0, 0, 0});
    p.add({10, 0, 0});
    p.add({10, 10, 0});

    Vec3 got{};
    REQUIRE(nearest_point(p, {4, 3, 0}, &got));
    CHECK_VEC(got, 4.0, 0.0, 0.0, 1e-9);

    REQUIRE(nearest_point(p, {13, 7, 0}, &got));
    CHECK_VEC(got, 10.0, 7.0, 0.0, 1e-9);

    // Past the far end, NEAREST stays on the entity.
    REQUIRE(nearest_point(p, {10, 40, 0}, &got));
    CHECK_VEC(got, 10.0, 10.0, 0.0, 1e-9);
}

TEST_CASE("polyline: PERPENDICULAR takes the nearest foot among the segments") {
    Polyline p;
    p.add({0, 0, 0});
    p.add({10, 0, 0});
    p.add({10, 10, 0});

    // (3,4) has a foot on each segment -- (3,0) and (10,4) -- and the first is
    // nearer. Note that a reference like (6,5) would legitimately answer with
    // (10,5) on the second segment for exactly the same reason.
    Vec3 got{};
    REQUIRE(perpendicular_point(p, {3, 4, 0}, &got));
    CHECK_VEC(got, 3.0, 0.0, 0.0, 1e-9);
}

TEST_CASE("polyline: a foot on its own segment beats a nearer one off the end") {
    // The last segment is short and high. Its infinite line passes closer to the
    // reference than the long bottom segment does, but the foot lands well past
    // the segment's own end -- so it is perpendicular to nothing the polyline
    // draws, and must lose despite being nearer.
    Polyline p;
    p.add({0, 0, 0});
    p.add({10, 0, 0});
    p.add({10, 3, 0});
    p.add({9, 3, 0});

    Vec3 got{};
    REQUIRE(perpendicular_point(p, {5, 2, 0}, &got));
    CHECK_VEC(got, 5.0, 0.0, 0.0, 1e-9);
}

TEST_CASE("polyline: a bulged segment gives the same tangents as the arc it is") {
    // A half-circle bulge from (0,0) to (10,0): centre (5,0), radius 5.
    //
    // A POSITIVE bulge arcs BELOW a left-to-right chord -- the trap recorded in
    // test_polyline.cpp -- so the drawn half is the lower one and the reference
    // has to be below it. Put it above and the honest answer is no tangents at
    // all, because both touch points would be on the half that is not drawn.
    Polyline p;
    p.add({0, 0, 0}, 1.0);
    p.add({10, 0, 0});

    const Vec3 ref{5, -20, 0};
    Vec3 pts[kMaxTangents];
    const int n = tangent_points(p, ref, pts);
    REQUIRE(n >= 1);

    for (int i = 0; i < n; ++i) {
        // On the circle the bulge describes...
        CHECK_NEAR(length(pts[i] - Vec3{5, 0, 0}), 5.0, 1e-7);
        // ...on the half that is actually drawn...
        CHECK(pts[i].y < 0.0);
        // ...and tangent from `ref`: the radius meets the chord at a right
        // angle, which is the definition and needs no reference implementation.
        const Vec3 radius = normalize(pts[i] - Vec3{5, 0, 0});
        const Vec3 chord = normalize(pts[i] - ref);
        CHECK(std::abs(dot(radius, chord)) < 1e-6);
    }
}

TEST_CASE("polyline: a bulge offers nothing from the side it does not draw") {
    Polyline p;
    p.add({0, 0, 0}, 1.0);  // arcs below the chord
    p.add({10, 0, 0});

    Vec3 pts[kMaxTangents];
    // Refusing is the honest answer: both touch points lie on the half that is
    // not there, and offering them would snap to geometry the drawing has not.
    CHECK(tangent_points(p, {5, 20, 0}, pts) == 0);
}

TEST_CASE("polyline: a straight one has no tangents to offer") {
    Polyline p;
    p.add({0, 0, 0});
    p.add({10, 0, 0});
    p.add({10, 10, 0});

    Vec3 pts[kMaxTangents];
    CHECK(tangent_points(p, {30, 30, 0}, pts) == 0);
}

// --- ELLIPSE static snaps ---------------------------------------------------

TEST_CASE("ellipse arc: quadrants are filtered by the sweep, as an arc's are") {
    // The upper half only. The -Y axis end is not on it, and offering a QUA
    // there puts a snap on geometry the drawing does not contain.
    const Ellipse arc({0, 0, 0}, {10, 0, 0}, 0.5, 0.0, kPi);

    std::vector<OsnapPoint> pts;
    arc.osnap_points(pts);

    bool has_bottom = false;
    int quadrants = 0;
    for (const OsnapPoint& p : pts) {
        if (p.type != OsnapType::Quadrant) continue;
        ++quadrants;
        if (near_equal(p.pos, Vec3{0, -5, 0}, 1e-9)) has_bottom = true;
    }
    CHECK(!has_bottom);
    CHECK(quadrants == 3);  // +X, +Y, -X; the -Y end is off the sweep
}

TEST_CASE("ellipse arc: has a midpoint, and a whole ellipse does not") {
    const Ellipse arc({0, 0, 0}, {10, 0, 0}, 0.5, 0.0, kPi);

    std::vector<OsnapPoint> pts;
    arc.osnap_points(pts);

    bool mid = false;
    for (const OsnapPoint& p : pts) {
        if (p.type == OsnapType::Midpoint) {
            mid = true;
            // By parameter, which for a half sweep is the +Y axis end.
            CHECK(near_equal(p.pos, Vec3{0, 5, 0}, 1e-9));
        }
    }
    CHECK(mid);

    // A whole ellipse has no ends and no middle between them.
    const Ellipse full({0, 0, 0}, {10, 0, 0}, 0.5);
    pts.clear();
    full.osnap_points(pts);
    for (const OsnapPoint& p : pts) {
        CHECK(p.type != OsnapType::Midpoint);
        CHECK(p.type != OsnapType::Endpoint);
    }
    CHECK(pts.size() == 5);  // centre and four quadrants
}
