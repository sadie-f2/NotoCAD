// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The intersection kernel.
//
// Three properties carry most of the weight here, because getting any of them
// wrong produces geometry that looks right from one angle:
//
//   Skew lines do not meet. Two lines that cross only in projection have no
//   intersection, and a kernel that says otherwise puts TRIM's cut in a place
//   that exists in no other view.
//
//   A line generally misses a circle it is not coplanar with. The line pierces
//   the circle's plane at one point, and that point is almost never on the rim.
//
//   Extended mode changes which answers are reported, never where they are. An
//   intersection found by extending has the same coordinates as the same
//   intersection found without -- what changes is the `within` flags.

#include "test.hpp"

#include "noto/database.hpp"
#include "noto/entities.hpp"
#include "noto/ecs.hpp"
#include "noto/intersect.hpp"

#include <cmath>
#include <numbers>

using namespace noto;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kTol = 1e-9;

bool has_point(const IntersectionList& hits, const Vec3& p) {
    for (const Intersection& h : hits) {
        if (near_equal(h.point, p, 1e-7)) return true;
    }
    return false;
}

Polyline square_with_arc() {
    // (0,0) -> (10,0) as a half-turn arc, then straight up to (10,10). A
    // positive bulge sweeps counterclockwise from the first vertex to the
    // second, which for a left-to-right chord puts the arc BELOW it -- through
    // (5,-5), not (5,5). See test_polyline.cpp, which pins the same convention.
    Polyline p;
    p.add({0, 0, 0}, 1.0);
    p.add({10, 0, 0});
    p.add({10, 10, 0});
    return p;
}

}  // namespace

// --- line / line ------------------------------------------------------------

TEST_CASE("intersect: two crossing segments meet once, in the middle") {
    Line a{{-5, 0, 0}, {5, 0, 0}};
    Line b{{0, -5, 0}, {0, 5, 0}};

    IntersectionList hits;
    REQUIRE(intersect(a, b, IntersectMode::Bounded, hits) == 1);
    CHECK(near_equal(hits[0].point, Vec3{0, 0, 0}, kTol));
    CHECK_NEAR(hits[0].t0, 0.5, kTol);
    CHECK_NEAR(hits[0].t1, 0.5, kTol);
    CHECK(hits[0].within0);
    CHECK(hits[0].within1);
}

TEST_CASE("intersect: skew lines do not meet, however they look from above") {
    // Both run through x=0 and y=0 respectively, but at different heights, so
    // they cross only in plan. This is the property osnap INT already pins and
    // the kernel must not lose.
    Line a{{-5, 0, 0}, {5, 0, 0}};
    Line b{{0, -5, 3}, {0, 5, 3}};

    IntersectionList hits;
    CHECK(intersect(a, b, IntersectMode::Extended, hits) == 0);
    CHECK(hits.empty());
}

TEST_CASE("intersect: segments that would cross if extended are declined when bounded") {
    Line a{{0, 0, 0}, {1, 0, 0}};
    Line b{{5, -5, 0}, {5, 5, 0}};

    IntersectionList bounded;
    CHECK(intersect(a, b, IntersectMode::Bounded, bounded) == 0);

    IntersectionList extended;
    REQUIRE(intersect(a, b, IntersectMode::Extended, extended) == 1);
    CHECK(near_equal(extended[0].point, Vec3{5, 0, 0}, kTol));
    // Five times the length of a, so t0 is 5 -- which is what EXTEND reads to
    // decide how far the line has to grow.
    CHECK_NEAR(extended[0].t0, 5.0, kTol);
    CHECK(!extended[0].within0);
    CHECK(extended[0].within1);
}

TEST_CASE("intersect: parallel lines never meet, collinear ones included") {
    Line a{{0, 0, 0}, {10, 0, 0}};
    Line b{{0, 5, 0}, {10, 5, 0}};
    Line c{{20, 0, 0}, {30, 0, 0}};  // collinear with a

    IntersectionList hits;
    CHECK(intersect(a, b, IntersectMode::Extended, hits) == 0);
    // Collinear overlap is a range rather than a point, and is not reported.
    CHECK(intersect(a, c, IntersectMode::Extended, hits) == 0);
}

TEST_CASE("intersect: lines meeting exactly at an endpoint count") {
    Line a{{0, 0, 0}, {10, 0, 0}};
    Line b{{10, 0, 0}, {10, 10, 0}};

    IntersectionList hits;
    REQUIRE(intersect(a, b, IntersectMode::Bounded, hits) == 1);
    CHECK(near_equal(hits[0].point, Vec3{10, 0, 0}, kTol));
    CHECK_NEAR(hits[0].t0, 1.0, kTol);
    CHECK_NEAR(hits[0].t1, 0.0, kTol);
}

// --- line / circle ----------------------------------------------------------

TEST_CASE("intersect: a chord crosses a circle twice") {
    Circle c{{0, 0, 0}, 5.0};
    Line l{{-10, 0, 0}, {10, 0, 0}};

    IntersectionList hits;
    REQUIRE(intersect(l, c, IntersectMode::Bounded, hits) == 2);
    CHECK(has_point(hits, {-5, 0, 0}));
    CHECK(has_point(hits, {5, 0, 0}));
}

TEST_CASE("intersect: a tangent line touches once, not twice") {
    Circle c{{0, 0, 0}, 5.0};
    Line l{{-10, 5, 0}, {10, 5, 0}};

    IntersectionList hits;
    // Reporting it twice would make TRIM believe there are two pieces where
    // there is one.
    REQUIRE(intersect(l, c, IntersectMode::Bounded, hits) == 1);
    CHECK(near_equal(hits[0].point, Vec3{0, 5, 0}, 1e-7));
}

TEST_CASE("intersect: a line off the plane misses the circle entirely") {
    // The line pierces the circle's plane at (0,0,0) -- the centre -- which is
    // not on the rim. A kernel that flattened first would report two hits.
    Circle c{{0, 0, 0}, 5.0};
    Line l{{0, 0, -10}, {0, 0, 10}};

    IntersectionList hits;
    CHECK(intersect(l, c, IntersectMode::Extended, hits) == 0);
}

TEST_CASE("intersect: a line piercing the plane exactly on the rim does meet") {
    Circle c{{0, 0, 0}, 5.0};
    Line l{{5, 0, -10}, {5, 0, 10}};

    IntersectionList hits;
    REQUIRE(intersect(l, c, IntersectMode::Bounded, hits) == 1);
    CHECK(near_equal(hits[0].point, Vec3{5, 0, 0}, 1e-7));
}

TEST_CASE("intersect: the circle parameter is the angle in its own plane") {
    Circle c{{0, 0, 0}, 5.0};
    Line l{{-10, 0, 0}, {10, 0, 0}};

    IntersectionList hits;
    REQUIRE(intersect(l, c, IntersectMode::Bounded, hits) == 2);
    for (const Intersection& h : hits) {
        // (5,0,0) is a quarter of no turn; (-5,0,0) is half a turn.
        const double expected = near_equal(h.point, Vec3{5, 0, 0}, 1e-7) ? 0.0 : 0.5;
        CHECK_NEAR(h.t1, expected, 1e-9);
        // A full circle is always within itself.
        CHECK(h.within1);
    }
}

// --- arcs -------------------------------------------------------------------

TEST_CASE("intersect: an arc is filtered to its sweep") {
    // The upper half of a circle of radius 5. A horizontal line through y=0
    // crosses the host circle at both ends of the diameter, but the arc's
    // endpoints are exactly there, so both remain.
    Arc upper{{0, 0, 0}, 5.0, 0.0, kPi};
    Line l{{-10, 0, 0}, {10, 0, 0}};

    IntersectionList hits;
    CHECK(intersect(l, upper, IntersectMode::Bounded, hits) == 2);
}

TEST_CASE("intersect: a line meeting only the missing half of an arc is declined") {
    // The upper half again, and a line across y = -3, which meets the host
    // circle only below the axis where the arc is not.
    Arc upper{{0, 0, 0}, 5.0, 0.0, kPi};
    Line l{{-10, -3, 0}, {10, -3, 0}};

    IntersectionList bounded;
    CHECK(intersect(l, upper, IntersectMode::Bounded, bounded) == 0);

    // Extended treats the arc as its whole circle, so the two points reappear
    // and are flagged as off the arc.
    IntersectionList extended;
    REQUIRE(intersect(l, upper, IntersectMode::Extended, extended) == 2);
    for (const Intersection& h : extended) {
        CHECK(h.within0);
        CHECK(!h.within1);
    }
}

TEST_CASE("intersect: extending finds the same point, not a different one") {
    // The invariant that makes Extended safe to use: it changes which answers
    // are reported, never where they are.
    Circle c{{0, 0, 0}, 5.0};
    Line short_line{{-10, 3, 0}, {-6, 3, 0}};  // stops before the circle

    IntersectionList bounded;
    CHECK(intersect(short_line, c, IntersectMode::Bounded, bounded) == 0);

    IntersectionList extended;
    REQUIRE(intersect(short_line, c, IntersectMode::Extended, extended) == 2);

    Line long_line{{-10, 3, 0}, {10, 3, 0}};
    IntersectionList reference;
    REQUIRE(intersect(long_line, c, IntersectMode::Bounded, reference) == 2);

    for (const Intersection& h : reference) {
        CHECK(has_point(extended, h.point));
    }
}

// --- circle / circle --------------------------------------------------------

TEST_CASE("intersect: two overlapping coplanar circles meet twice") {
    Circle a{{0, 0, 0}, 5.0};
    Circle b{{6, 0, 0}, 5.0};

    IntersectionList hits;
    REQUIRE(intersect(a, b, IntersectMode::Bounded, hits) == 2);
    CHECK(has_point(hits, {3, 4, 0}));
    CHECK(has_point(hits, {3, -4, 0}));
}

TEST_CASE("intersect: externally tangent circles touch once") {
    Circle a{{0, 0, 0}, 5.0};
    Circle b{{10, 0, 0}, 5.0};

    IntersectionList hits;
    REQUIRE(intersect(a, b, IntersectMode::Bounded, hits) == 1);
    CHECK(near_equal(hits[0].point, Vec3{5, 0, 0}, 1e-7));
}

TEST_CASE("intersect: circles that miss, nest or coincide give nothing") {
    Circle a{{0, 0, 0}, 5.0};

    IntersectionList hits;
    CHECK(intersect(a, Circle{{20, 0, 0}, 5.0}, IntersectMode::Bounded, hits) == 0);
    CHECK(intersect(a, Circle{{0.5, 0, 0}, 1.0}, IntersectMode::Bounded, hits) == 0);
    // Coincident circles meet everywhere, which is not a set of points.
    CHECK(intersect(a, Circle{{0, 0, 0}, 5.0}, IntersectMode::Bounded, hits) == 0);
}

TEST_CASE("intersect: circles in crossing planes meet where both rims agree") {
    // Radius 5 about the origin in XY and in YZ. Both pass through (0, +-5, 0).
    Circle a{{0, 0, 0}, 5.0};
    Circle b{{0, 0, 0}, 5.0, kWorldX};

    IntersectionList hits;
    REQUIRE(intersect(a, b, IntersectMode::Bounded, hits) == 2);
    CHECK(has_point(hits, {0, 5, 0}));
    CHECK(has_point(hits, {0, -5, 0}));
}

TEST_CASE("intersect: crossing planes are not enough on their own") {
    // The second circle's plane still cuts the first, but its rim is nowhere
    // near. Crossing the plane is necessary, not sufficient.
    Circle a{{0, 0, 0}, 5.0};
    Circle b{{0, 50, 0}, 5.0, kWorldX};

    IntersectionList hits;
    CHECK(intersect(a, b, IntersectMode::Bounded, hits) == 0);
}

TEST_CASE("intersect: circles in parallel but separated planes never meet") {
    Circle a{{0, 0, 0}, 5.0};
    Circle b{{0, 0, 3}, 5.0};

    IntersectionList hits;
    CHECK(intersect(a, b, IntersectMode::Extended, hits) == 0);
}

// --- polylines --------------------------------------------------------------

TEST_CASE("intersect: a line crossing a polyline meets each segment it crosses") {
    Polyline p;
    p.add({0, 0, 0});
    p.add({10, 0, 0});
    p.add({10, 10, 0});
    Line l{{-5, 5, 0}, {15, 5, 0}};

    IntersectionList hits;
    // Crosses only the vertical segment.
    REQUIRE(intersect(l, p, IntersectMode::Bounded, hits) == 1);
    CHECK(near_equal(hits[0].point, Vec3{10, 5, 0}, kTol));
}

TEST_CASE("intersect: a polyline parameter says which segment was hit") {
    Polyline p;
    p.add({0, 0, 0});
    p.add({10, 0, 0});
    p.add({10, 10, 0});
    Line l{{-5, 5, 0}, {15, 5, 0}};

    IntersectionList hits;
    REQUIRE(intersect(l, p, IntersectMode::Bounded, hits) == 1);
    // Two segments, so the second spans [0.5, 1.0]; halfway along it is 0.75.
    CHECK_NEAR(hits[0].t1, 0.75, 1e-9);
    CHECK(hits[0].within1);
}

TEST_CASE("intersect: a bulged polyline segment is intersected as the arc it is") {
    // The first segment bulges by 1.0 -- a half turn from (0,0) to (10,0),
    // arcing down through (5,-5). A vertical line at x=5 must meet it there,
    // which it would not if the segment were treated as its chord.
    Polyline p = square_with_arc();
    Line l{{5, -8, 0}, {5, -1, 0}};

    IntersectionList hits;
    REQUIRE(intersect(l, p, IntersectMode::Bounded, hits) == 1);
    CHECK(near_equal(hits[0].point, Vec3{5, -5, 0}, 1e-7));
}

TEST_CASE("intersect: a clockwise polyline segment is parameterised forwards") {
    // A negative bulge sweeps clockwise. The sub-curve's sweep has to be signed
    // to say so -- starting from the other end and sweeping positively instead
    // describes the same geometry but runs the parameter BACKWARDS against the
    // polyline's direction of travel, so every hit on such a segment reports a
    // mirrored parameter. Invisible until something cuts a curve there, which
    // is how BREAK found it.
    Polyline p;
    p.add({0, 0, 0}, -1.0);  // a half turn, clockwise: arcs ABOVE the chord
    p.add({10, 0, 0});

    // Parameter zero is the first vertex, not the second.
    Vec3 at_start{};
    REQUIRE(curve_point_at(p, 0.0, &at_start));
    CHECK(near_equal(at_start, Vec3{0, 0, 0}, 1e-7));

    Vec3 at_end{};
    REQUIRE(curve_point_at(p, 1.0, &at_end));
    CHECK(near_equal(at_end, Vec3{10, 0, 0}, 1e-7));

    // And the midpoint is above the chord, which is where a negative bulge puts
    // it -- the mirror image of the positive case.
    Vec3 middle{};
    REQUIRE(curve_point_at(p, 0.5, &middle));
    CHECK(middle.y > 0.0);
    CHECK(near_equal(middle, Vec3{5, 5, 0}, 1e-7));
}

TEST_CASE("intersect: a hit on a clockwise segment reports the parameter it is at") {
    Polyline p;
    p.add({0, 0, 0}, -1.0);
    p.add({10, 0, 0});

    // The line meets the arc at its topmost point, which is halfway along.
    Line l{{5, 1, 0}, {5, 8, 0}};

    IntersectionList hits;
    REQUIRE(intersect(l, p, IntersectMode::Bounded, hits) == 1);
    CHECK_NEAR(hits[0].t1, 0.5, 1e-7);

    // And the parameter evaluates back to the point it came with.
    Vec3 back{};
    REQUIRE(curve_point_at(p, hits[0].t1, &back));
    CHECK(near_equal(back, hits[0].point, 1e-7));
}

TEST_CASE("intersect: a polyline's own segments are never extended") {
    // A line that misses the polyline but would meet the carrier of one of its
    // segments. Extended must still decline, because a polyline has no carrier.
    Polyline p;
    p.add({0, 0, 0});
    p.add({10, 0, 0});
    Line l{{20, -5, 0}, {20, 5, 0}};

    IntersectionList hits;
    CHECK(intersect(p, l, IntersectMode::Extended, hits) == 0);
}

TEST_CASE("intersect: two polylines meet at every crossing") {
    Polyline a;
    a.add({0, 0, 0});
    a.add({10, 0, 0});
    a.add({10, 10, 0});

    Polyline b;
    b.add({-5, 5, 0});
    b.add({15, 5, 0});
    b.add({15, -5, 0});
    b.add({5, -5, 0});
    b.add({5, 15, 0});

    IntersectionList hits;
    const std::size_t n = intersect(a, b, IntersectMode::Bounded, hits);
    // b crosses a's vertical leg once and its horizontal leg once.
    CHECK(n == 2);
    CHECK(has_point(hits, {10, 5, 0}));
    CHECK(has_point(hits, {5, 0, 0}));
}

// --- entities with no curve -------------------------------------------------

TEST_CASE("intersect: entities with no geometry are skipped, not errors") {
    // A caller sweeping a selection set has to be able to ask about anything.
    Line l{{0, 0, 0}, {10, 0, 0}};
    PointEntity pt{{5, 0, 0}};
    Text t{{5, 0, 0}, "HELLO", 1.0};

    IntersectionList hits;
    CHECK(intersect(l, pt, IntersectMode::Extended, hits) == 0);
    CHECK(intersect(l, t, IntersectMode::Extended, hits) == 0);
}

// --- curve_point_at ---------------------------------------------------------

TEST_CASE("curve_point_at: a line is parameterised start to end") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    Vec3 p{};

    REQUIRE(curve_point_at(l, 0.0, &p));
    CHECK(near_equal(p, Vec3{0, 0, 0}, kTol));
    REQUIRE(curve_point_at(l, 0.25, &p));
    CHECK(near_equal(p, Vec3{2.5, 0, 0}, kTol));
    REQUIRE(curve_point_at(l, 1.0, &p));
    CHECK(near_equal(p, Vec3{10, 0, 0}, kTol));
}

TEST_CASE("curve_point_at: an arc is parameterised along its sweep") {
    Arc a{{0, 0, 0}, 5.0, 0.0, kPi};
    Vec3 p{};

    REQUIRE(curve_point_at(a, 0.0, &p));
    CHECK(near_equal(p, Vec3{5, 0, 0}, 1e-7));
    REQUIRE(curve_point_at(a, 0.5, &p));
    CHECK(near_equal(p, Vec3{0, 5, 0}, 1e-7));
    REQUIRE(curve_point_at(a, 1.0, &p));
    CHECK(near_equal(p, Vec3{-5, 0, 0}, 1e-7));
}

TEST_CASE("curve_point_at: a polyline parameter walks its segments in order") {
    Polyline poly;
    poly.add({0, 0, 0});
    poly.add({10, 0, 0});
    poly.add({10, 10, 0});
    Vec3 p{};

    REQUIRE(curve_point_at(poly, 0.5, &p));
    CHECK(near_equal(p, Vec3{10, 0, 0}, kTol));
    REQUIRE(curve_point_at(poly, 0.75, &p));
    CHECK(near_equal(p, Vec3{10, 5, 0}, kTol));
}

TEST_CASE("curve_point_at: round-trips against the parameters intersect reports") {
    // The property BREAK depends on: a parameter handed back by intersect must
    // evaluate to the point it came with, or splitting a curve at it lands
    // somewhere else.
    Circle c{{1, 2, 0}, 5.0};
    Line l{{-10, 3, 0}, {10, 3, 0}};

    IntersectionList hits;
    REQUIRE(intersect(l, c, IntersectMode::Bounded, hits) == 2);

    for (const Intersection& h : hits) {
        Vec3 on_line{};
        Vec3 on_circle{};
        REQUIRE(curve_point_at(l, h.t0, &on_line));
        REQUIRE(curve_point_at(c, h.t1, &on_circle));
        CHECK(near_equal(on_line, h.point, 1e-7));
        CHECK(near_equal(on_circle, h.point, 1e-7));
    }
}

TEST_CASE("curve_point_at: a polyline has no answer off either end") {
    Polyline poly;
    poly.add({0, 0, 0});
    poly.add({10, 0, 0});
    poly.add({10, 10, 0});
    Vec3 p{};

    CHECK(!curve_point_at(poly, -0.5, &p));
    CHECK(!curve_point_at(poly, 1.5, &p));
}

TEST_CASE("curve_point_at: an entity with no curve has no point") {
    PointEntity pt{{1, 2, 3}};
    Vec3 p{};
    CHECK(!curve_point_at(pt, 0.5, &p));
}

// --- tilted planes ----------------------------------------------------------

TEST_CASE("intersect: the whole thing works off the world plane") {
    // Everything above sits in XY, which is exactly where a bug in the
    // arbitrary-axis handling would hide. Same two circles, tilted.
    const Vec3 tilted = normalize(Vec3{1, 2, 3});
    Circle a{{1, 2, 3}, 5.0, tilted};

    // A second circle in the same tilted plane, offset within it.
    const Basis b = arbitrary_axis(tilted);
    const Vec3 offset = b.ax * 6.0;
    Circle c{Vec3{1, 2, 3} + offset, 5.0, tilted};

    IntersectionList hits;
    REQUIRE(intersect(a, c, IntersectMode::Bounded, hits) == 2);

    // Both answers must lie on both circles and in the plane.
    for (const Intersection& h : hits) {
        CHECK_NEAR(length(h.point - a.center()), 5.0, 1e-7);
        CHECK_NEAR(length(h.point - c.center()), 5.0, 1e-7);
        CHECK_NEAR(dot(h.point - Vec3{1, 2, 3}, tilted), 0.0, 1e-7);
    }
}
