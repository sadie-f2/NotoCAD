// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/entities.hpp"
#include "noto/osnap_derived.hpp"

#include <cmath>
#include <numbers>

using namespace noto;

namespace {

constexpr double pi = std::numbers::pi;
const Vec3 kTilted = normalize(Vec3{1.0, 2.0, 3.0});

// A tangent line touches the circle at exactly one point, so the radius to the
// touch point is perpendicular to the line from the reference point. Checking
// that property rather than a precomputed coordinate is what makes the tilted
// cases worth running at all.
bool is_tangent(const Vec3& center, double radius, const Vec3& ref, const Vec3& touch) {
    const Vec3 spoke = touch - center;
    if (std::abs(length(spoke) - radius) > 1e-9) return false;
    return std::abs(dot(spoke, ref - touch)) < 1e-9;
}

}  // namespace

// --- NEAREST -----------------------------------------------------------------

TEST_CASE("nearest: on a line it is the perpendicular foot, clamped to the segment") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    Vec3 p{};

    CHECK(nearest_point(l, {4.0, 5.0, 0.0}, &p));
    CHECK_VEC(p, 4.0, 0.0, 0.0, 1e-12);

    // Beyond the ends NEA stays on the entity, which is the whole difference
    // between it and PER.
    CHECK(nearest_point(l, {-20.0, 3.0, 0.0}, &p));
    CHECK_VEC(p, 0.0, 0.0, 0.0, 1e-12);
    CHECK(nearest_point(l, {99.0, -3.0, 0.0}, &p));
    CHECK_VEC(p, 10.0, 0.0, 0.0, 1e-12);
}

TEST_CASE("nearest: on a circle it is radial, from any distance off the plane") {
    Circle c{{0, 0, 0}, 5.0};
    Vec3 p{};
    // The reference is well off the circle's plane; the answer must still be
    // the radially nearest point, not something pulled toward the plane hit.
    CHECK(nearest_point(c, {20.0, 0.0, 17.0}, &p));
    CHECK_VEC(p, 5.0, 0.0, 0.0, 1e-12);
}

TEST_CASE("nearest: a reference on the axis is answered repeatably, not arbitrarily") {
    Circle c{{0, 0, 0}, 5.0, kTilted};
    Vec3 a{};
    Vec3 b{};
    // Every point on the circle ties. Two calls must still agree, or a cursor
    // sitting on the axis would make the marker flicker between points.
    CHECK(nearest_point(c, c.center() + kTilted * 9.0, &a));
    CHECK(nearest_point(c, c.center() + kTilted * 3.0, &b));
    CHECK(near_equal(a, b, 1e-12));
    CHECK_NEAR(length(a - c.center()), 5.0, 1e-12);
}

TEST_CASE("nearest: an arc falls back to the nearer endpoint outside its sweep") {
    // Quarter arc in the first quadrant; the reference is diametrically away.
    Arc a{{0, 0, 0}, 4.0, 0.0, pi / 2.0};
    Vec3 p{};
    CHECK(nearest_point(a, {-10.0, -1.0, 0.0}, &p));
    // Radially nearest would be (-4,0,0), which is not on the arc at all.
    CHECK_VEC(p, 0.0, 4.0, 0.0, 1e-9);
}

// --- PERPENDICULAR -----------------------------------------------------------

TEST_CASE("perpendicular: a line answers on its extension, unlike nearest") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    Vec3 per{};
    Vec3 nea{};
    CHECK(perpendicular_point(l, {25.0, 5.0, 0.0}, &per));
    CHECK(nearest_point(l, {25.0, 5.0, 0.0}, &nea));
    CHECK_VEC(per, 25.0, 0.0, 0.0, 1e-12);  // off the segment, on the line
    CHECK_VEC(nea, 10.0, 0.0, 0.0, 1e-12);  // clamped
}

TEST_CASE("perpendicular: on a tilted circle the spoke meets the radius") {
    Circle c{{1.0, 2.0, 3.0}, 6.0, kTilted};
    const Vec3 ref{20.0, -4.0, 11.0};
    Vec3 p{};
    CHECK(perpendicular_point(c, ref, &p));
    CHECK_NEAR(length(p - c.center()), 6.0, 1e-9);
    // The foot must lie in the circle's own plane.
    CHECK_NEAR(dot(p - c.center(), kTilted), 0.0, 1e-9);
}

TEST_CASE("perpendicular: an arc uses its far side when the near side is off sweep") {
    // Arc covering the second quadrant only. A reference out on +X has its
    // near radial foot at (4,0,0), which is not on the arc; (-4,0,0) is.
    Arc a{{0, 0, 0}, 4.0, pi / 2.0, pi};
    Vec3 p{};
    CHECK(perpendicular_point(a, {30.0, 0.0, 0.0}, &p));
    CHECK_VEC(p, -4.0, 0.0, 0.0, 1e-9);
}

// --- TANGENT -----------------------------------------------------------------

TEST_CASE("tangent: two from outside, and they really are tangent") {
    Circle c{{0, 0, 0}, 3.0};
    const Vec3 ref{10.0, 0.0, 0.0};
    Vec3 t[kMaxTangents];
    CHECK(tangent_points(c, ref, t) == 2);
    CHECK(is_tangent(c.center(), 3.0, ref, t[0]));
    CHECK(is_tangent(c.center(), 3.0, ref, t[1]));
    CHECK(!near_equal(t[0], t[1]));
}

TEST_CASE("tangent: the reference is projected into the entity's plane") {
    // A cursor is essentially never coplanar with what it points at, so this is
    // the normal case rather than an edge case.
    Circle c{{1.0, -2.0, 4.0}, 2.5, kTilted};
    const Vec3 ref = c.center() + Vec3{9.0, 1.0, -3.0};
    Vec3 t[kMaxTangents];
    const int n = tangent_points(c, ref, t);
    CHECK(n == 2);
    for (int i = 0; i < n; ++i) {
        CHECK_NEAR(length(t[i] - c.center()), 2.5, 1e-9);
        CHECK_NEAR(dot(t[i] - c.center(), kTilted), 0.0, 1e-9);
        // Tangency is measured against the projected reference, since that is
        // the point the construction is actually from.
        const Vec3 flat = ref - kTilted * dot(ref - c.center(), kTilted);
        CHECK(is_tangent(c.center(), 2.5, flat, t[i]));
    }
}

TEST_CASE("tangent: none from inside, one from on the circle") {
    Circle c{{0, 0, 0}, 5.0};
    Vec3 t[kMaxTangents];
    CHECK(tangent_points(c, {1.0, 0.0, 0.0}, t) == 0);
    CHECK(tangent_points(c, {0.0, 0.0, 0.0}, t) == 0);
    CHECK(tangent_points(c, {5.0, 0.0, 0.0}, t) == 1);
    CHECK_VEC(t[0], 5.0, 0.0, 0.0, 1e-9);
}

TEST_CASE("tangent: a line has no tangent construction") {
    Line l{{0, 0, 0}, {1, 0, 0}};
    Vec3 t[kMaxTangents];
    CHECK(tangent_points(l, {0.0, 5.0, 0.0}, t) == 0);
}

TEST_CASE("tangent: an arc reports only the touches on its sweep") {
    // Tangents from far out on +X touch near (±) symmetric points; half the
    // circle is missing, so only one survives.
    Arc a{{0, 0, 0}, 3.0, 0.0, pi};  // upper half
    Vec3 t[kMaxTangents];
    CHECK(tangent_points(a, {10.0, 0.0, 0.0}, t) == 1);
    CHECK(t[0].y > 0.0);
}

// --- INTERSECTION ------------------------------------------------------------

TEST_CASE("intersection: two crossing segments meet once") {
    Line a{{0, 0, 0}, {10, 0, 0}};
    Line b{{4, -5, 0}, {4, 5, 0}};
    Vec3 p[kMaxIntersections];
    CHECK(intersect_entities(a, b, p) == 1);
    CHECK_VEC(p[0], 4.0, 0.0, 0.0, 1e-9);
}

TEST_CASE("intersection: segments that would cross only if extended do not") {
    // The distinction INT rests on: where the entities meet, not where their
    // infinite hosts would.
    Line a{{0, 0, 0}, {10, 0, 0}};
    Line b{{20, -5, 0}, {20, 5, 0}};
    Vec3 p[kMaxIntersections];
    CHECK(intersect_entities(a, b, p) == 0);
}

TEST_CASE("intersection: skew lines pass without meeting") {
    // They cross in plan view but never touch. An apparent intersection is
    // APPINT's business and needs a viewport; this is the true one.
    Line a{{0, 0, 0}, {10, 0, 0}};
    Line b{{5, -5, 3}, {5, 5, 3}};
    Vec3 p[kMaxIntersections];
    CHECK(intersect_entities(a, b, p) == 0);
}

TEST_CASE("intersection: parallel and collinear lines report none") {
    Line a{{0, 0, 0}, {10, 0, 0}};
    Line parallel{{0, 1, 0}, {10, 1, 0}};
    Line collinear{{5, 0, 0}, {15, 0, 0}};
    Vec3 p[kMaxIntersections];
    CHECK(intersect_entities(a, parallel, p) == 0);
    CHECK(intersect_entities(a, collinear, p) == 0);
}

TEST_CASE("intersection: a chord meets a circle twice") {
    Circle c{{0, 0, 0}, 5.0};
    Line chord{{-10, 3, 0}, {10, 3, 0}};
    Vec3 p[kMaxIntersections];
    CHECK(intersect_entities(chord, c, p) == 2);
    for (int i = 0; i < 2; ++i) {
        CHECK_NEAR(length(p[i]), 5.0, 1e-9);
        CHECK_NEAR(p[i].y, 3.0, 1e-9);
    }
    // Order of the operands must not change the answer.
    Vec3 q[kMaxIntersections];
    CHECK(intersect_entities(c, chord, q) == 2);
}

TEST_CASE("intersection: a tangent line touches a circle once") {
    Circle c{{0, 0, 0}, 5.0};
    Line t{{-10, 5, 0}, {10, 5, 0}};
    Vec3 p[kMaxIntersections];
    CHECK(intersect_entities(t, c, p) == 1);
    CHECK_VEC(p[0], 0.0, 5.0, 0.0, 1e-7);
}

TEST_CASE("intersection: a line out of the circle's plane misses it") {
    Circle c{{0, 0, 0}, 5.0};
    Line above{{-10, 3, 2}, {10, 3, 2}};
    Vec3 p[kMaxIntersections];
    CHECK(intersect_entities(above, c, p) == 0);
}

TEST_CASE("intersection: a line piercing the plane exactly on the circle counts") {
    Circle c{{0, 0, 0}, 5.0};
    Line spike{{5, 0, -3}, {5, 0, 3}};  // pierces at (5,0,0), which is on the circle
    Vec3 p[kMaxIntersections];
    CHECK(intersect_entities(spike, c, p) == 1);
    CHECK_VEC(p[0], 5.0, 0.0, 0.0, 1e-9);
}

TEST_CASE("intersection: two overlapping circles meet twice") {
    Circle a{{0, 0, 0}, 5.0};
    Circle b{{6, 0, 0}, 5.0};
    Vec3 p[kMaxIntersections];
    CHECK(intersect_entities(a, b, p) == 2);
    for (int i = 0; i < 2; ++i) {
        CHECK_NEAR(length(p[i] - a.center()), 5.0, 1e-9);
        CHECK_NEAR(length(p[i] - b.center()), 5.0, 1e-9);
    }
}

TEST_CASE("intersection: circles that touch, miss, nest or coincide") {
    Circle a{{0, 0, 0}, 5.0};
    Vec3 p[kMaxIntersections];
    CHECK(intersect_entities(a, Circle{{10, 0, 0}, 5.0}, p) == 1);  // externally tangent
    CHECK(intersect_entities(a, Circle{{20, 0, 0}, 5.0}, p) == 0);  // too far
    CHECK(intersect_entities(a, Circle{{0.5, 0, 0}, 1.0}, p) == 0);  // nested
    CHECK(intersect_entities(a, Circle{{0, 0, 0}, 5.0}, p) == 0);   // concentric
}

TEST_CASE("intersection: non-coplanar circles are declined, as documented") {
    Circle a{{0, 0, 0}, 5.0};
    Circle b{{0, 0, 0}, 5.0, kWorldX};
    Vec3 p[kMaxIntersections];
    CHECK(intersect_entities(a, b, p) == 0);
}

TEST_CASE("intersection: two tilted coplanar circles still meet") {
    // Same plane, not the world plane: the case the arbitrary axis code has to
    // get right for INT to work anywhere but plan view.
    const Vec3 offset = normalize(cross(kTilted, kWorldX)) * 6.0;
    Circle a{{1, 2, 3}, 5.0, kTilted};
    Circle b{Vec3{1, 2, 3} + offset, 5.0, kTilted};
    Vec3 p[kMaxIntersections];
    CHECK(intersect_entities(a, b, p) == 2);
    for (int i = 0; i < 2; ++i) {
        CHECK_NEAR(length(p[i] - a.center()), 5.0, 1e-9);
        CHECK_NEAR(length(p[i] - b.center()), 5.0, 1e-9);
        CHECK_NEAR(dot(p[i] - a.center(), kTilted), 0.0, 1e-9);
    }
}

TEST_CASE("intersection: arcs are filtered to their sweep") {
    // Two full circles would meet twice, symmetric about the centre line. Half
    // an arc keeps only the half that is drawn.
    Arc a{{0, 0, 0}, 5.0, 0.0, pi};  // upper half
    Circle b{{6, 0, 0}, 5.0};
    Vec3 p[kMaxIntersections];
    CHECK(intersect_entities(a, b, p) == 1);
    CHECK(p[0].y > 0.0);
}
