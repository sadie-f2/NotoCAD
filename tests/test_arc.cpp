// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// ARC's option set. The entity has been here since the first commit; these test
// the eleven ways R12 lets you describe one.
//
// Assertions are on the GEOMETRY -- centre, radius, and which points the arc
// actually passes through -- rather than on groups 50 and 51. Arc always stores
// a counterclockwise span, so an arc described clockwise comes back with its
// ends swapped; that is correct and invisible in the drawing, and a test reading
// raw angles would pin the bookkeeping instead of the curve.

#include "test.hpp"

#include "noto/commands.hpp"
#include "noto/database.hpp"
#include "noto/entities.hpp"

#include <cmath>
#include <memory>
#include <numbers>

using namespace noto;

namespace {

constexpr double kPi = std::numbers::pi;

const Arc* last_arc(const Database& db) {
    if (db.order().empty()) return nullptr;
    const Entity* e = db.get(db.order().back());
    if (e == nullptr || e->type() != EntityType::Arc) return nullptr;
    return static_cast<const Arc*>(e);
}

// Whether the arc runs through `p` -- on the circle, and inside the sweep.
bool passes_through(const Arc& a, const Vec3& p, double eps = 1e-9) {
    if (std::abs(length(p - a.center()) - a.radius()) > eps) return false;

    const Vec3 r = p - a.center();
    const Basis b = arbitrary_axis(a.props().normal);
    double at = std::atan2(dot(r, b.ay), dot(r, b.ax));

    double from = a.start_angle();
    double d = at - from;
    while (d < -eps) d += 2.0 * kPi;
    return d <= a.sweep() + eps;
}

bool is_an_end(const Arc& a, const Vec3& p, double eps = 1e-9) {
    return near_equal(a.start_point(), p, eps) || near_equal(a.end_point(), p, eps);
}

}  // namespace

TEST_CASE("arc: three points, taken in the order given") {
    Database db;
    CommandEngine engine(db);

    // Up and over: (0,0) -> (1,1) -> (2,0) is a clockwise half turn.
    engine.begin(make_command("ARC"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({1, 1, 0}));
    engine.supply(InputValue::of_point({2, 0, 0}));

    const Arc* a = last_arc(db);
    REQUIRE(a != nullptr);
    CHECK_VEC(a->center(), 1.0, 0.0, 0.0, 1e-12);
    CHECK_NEAR(a->radius(), 1.0, 1e-12);
    CHECK(is_an_end(*a, Vec3{0, 0, 0}));
    CHECK(is_an_end(*a, Vec3{2, 0, 0}));
    CHECK(passes_through(*a, Vec3{1, 1, 0}));
    // And not through the other half, which is the whole point of the middle
    // point: the same three-point circle has two arcs and only one is meant.
    CHECK(!passes_through(*a, Vec3{1, -1, 0}));
}

TEST_CASE("arc: the same three points the other way round sweep the other way") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("ARC"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({1, -1, 0}));
    engine.supply(InputValue::of_point({2, 0, 0}));

    const Arc* a = last_arc(db);
    REQUIRE(a != nullptr);
    CHECK_VEC(a->center(), 1.0, 0.0, 0.0, 1e-12);
    CHECK(passes_through(*a, Vec3{1, -1, 0}));
    CHECK(!passes_through(*a, Vec3{1, 1, 0}));
}

TEST_CASE("arc: three collinear points are refused") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("ARC"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({1, 0, 0}));
    engine.supply(InputValue::of_point({2, 0, 0}));

    CHECK(engine.status() == EngineStatus::Failed);
    CHECK(db.empty());
}

TEST_CASE("arc: start, centre, end -- the end point gives a direction, not a radius") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("ARC"));
    engine.supply(InputValue::of_point({1, 0, 0}));   // start
    engine.supply(InputValue::of_keyword("CENTER"));
    engine.supply(InputValue::of_point({0, 0, 0}));   // centre
    // Miles away, and only its bearing is used: R12 projects onto the circle
    // the centre and start have already fixed.
    engine.supply(InputValue::of_point({0, 50, 0}));

    const Arc* a = last_arc(db);
    REQUIRE(a != nullptr);
    CHECK_NEAR(a->radius(), 1.0, 1e-12);
    CHECK_NEAR(a->sweep(), kPi * 0.5, 1e-9);
    CHECK(is_an_end(*a, Vec3{1, 0, 0}));
    CHECK(is_an_end(*a, Vec3{0, 1, 0}));
}

TEST_CASE("arc: start, centre, included angle -- and a negative one goes the other way") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("ARC"));
    engine.supply(InputValue::of_point({1, 0, 0}));
    engine.supply(InputValue::of_keyword("CENTER"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_keyword("ANGLE"));
    engine.supply(InputValue::of_real(90.0));

    const Arc* a = last_arc(db);
    REQUIRE(a != nullptr);
    CHECK_NEAR(a->sweep(), kPi * 0.5, 1e-9);
    CHECK(passes_through(*a, Vec3{0, 1, 0}));

    engine.begin(make_command("ARC"));
    engine.supply(InputValue::of_point({1, 0, 0}));
    engine.supply(InputValue::of_keyword("CENTER"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_keyword("ANGLE"));
    engine.supply(InputValue::of_real(-90.0));

    const Arc* b = last_arc(db);
    REQUIRE(b != nullptr);
    CHECK_NEAR(b->sweep(), kPi * 0.5, 1e-9);
    // Clockwise from the start, so it reaches (0,-1) instead.
    CHECK(passes_through(*b, Vec3{0, -1, 0}));
    CHECK(!passes_through(*b, Vec3{0, 1, 0}));
}

TEST_CASE("arc: start, centre, chord length -- negative asks for the major arc") {
    Database db;
    CommandEngine engine(db);

    // Radius 1, chord sqrt(2): a quarter turn.
    engine.begin(make_command("ARC"));
    engine.supply(InputValue::of_point({1, 0, 0}));
    engine.supply(InputValue::of_keyword("CENTER"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_keyword("LENGTH"));
    engine.supply(InputValue::of_real(std::sqrt(2.0)));

    const Arc* minor = last_arc(db);
    REQUIRE(minor != nullptr);
    CHECK_NEAR(minor->sweep(), kPi * 0.5, 1e-9);

    engine.begin(make_command("ARC"));
    engine.supply(InputValue::of_point({1, 0, 0}));
    engine.supply(InputValue::of_keyword("CENTER"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_keyword("LENGTH"));
    engine.supply(InputValue::of_real(-std::sqrt(2.0)));

    const Arc* major = last_arc(db);
    REQUIRE(major != nullptr);
    // The same chord, the long way round: 2*pi minus the quarter turn.
    CHECK_NEAR(major->sweep(), kPi * 1.5, 1e-9);
}

TEST_CASE("arc: start, end, included angle") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("ARC"));
    engine.supply(InputValue::of_point({1, 0, 0}));
    engine.supply(InputValue::of_keyword("END"));
    engine.supply(InputValue::of_point({0, 1, 0}));
    engine.supply(InputValue::of_keyword("ANGLE"));
    engine.supply(InputValue::of_real(90.0));

    const Arc* a = last_arc(db);
    REQUIRE(a != nullptr);
    // A quarter turn between those two points puts the centre at the origin.
    CHECK_VEC(a->center(), 0.0, 0.0, 0.0, 1e-9);
    CHECK_NEAR(a->radius(), 1.0, 1e-9);
    CHECK(is_an_end(*a, Vec3{1, 0, 0}));
    CHECK(is_an_end(*a, Vec3{0, 1, 0}));
}

TEST_CASE("arc: start, end, radius -- negative asks for the major arc") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("ARC"));
    engine.supply(InputValue::of_point({1, 0, 0}));
    engine.supply(InputValue::of_keyword("END"));
    engine.supply(InputValue::of_point({0, 1, 0}));
    engine.supply(InputValue::of_keyword("RADIUS"));
    engine.supply(InputValue::of_real(1.0));

    const Arc* minor = last_arc(db);
    REQUIRE(minor != nullptr);
    CHECK_NEAR(minor->radius(), 1.0, 1e-9);
    CHECK_NEAR(minor->sweep(), kPi * 0.5, 1e-9);

    engine.begin(make_command("ARC"));
    engine.supply(InputValue::of_point({1, 0, 0}));
    engine.supply(InputValue::of_keyword("END"));
    engine.supply(InputValue::of_point({0, 1, 0}));
    engine.supply(InputValue::of_keyword("RADIUS"));
    engine.supply(InputValue::of_real(-1.0));

    const Arc* major = last_arc(db);
    REQUIRE(major != nullptr);
    CHECK_NEAR(major->sweep(), kPi * 1.5, 1e-9);
}

TEST_CASE("arc: a radius too short to reach the end point is refused") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("ARC"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_keyword("END"));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_keyword("RADIUS"));
    engine.supply(InputValue::of_real(1.0));  // needs at least 5

    CHECK(engine.status() == EngineStatus::Failed);
    CHECK(db.empty());
}

TEST_CASE("arc: start, end, direction leaves along the bearing given") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("ARC"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_keyword("END"));
    engine.supply(InputValue::of_point({2, 0, 0}));
    engine.supply(InputValue::of_keyword("DIRECTION"));
    engine.supply(InputValue::of_real(90.0));  // straight up out of the start

    const Arc* a = last_arc(db);
    REQUIRE(a != nullptr);
    // Leaving (0,0) upward and arriving at (2,0) is the half turn over the top.
    CHECK_VEC(a->center(), 1.0, 0.0, 0.0, 1e-9);
    CHECK_NEAR(a->sweep(), kPi, 1e-9);
    CHECK(passes_through(*a, Vec3{1, 1, 0}));
}

TEST_CASE("arc: centre first, then start and end") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("ARC"));
    engine.supply(InputValue::of_keyword("CENTER"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({2, 0, 0}));  // start
    engine.supply(InputValue::of_point({0, 9, 0}));  // end, by bearing

    const Arc* a = last_arc(db);
    REQUIRE(a != nullptr);
    CHECK_VEC(a->center(), 0.0, 0.0, 0.0, 1e-12);
    CHECK_NEAR(a->radius(), 2.0, 1e-12);
    CHECK_NEAR(a->sweep(), kPi * 0.5, 1e-9);
}

TEST_CASE("arc: Continue picks up the last line's end and direction") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({2, 0, 0}));
    engine.supply(InputValue::none());

    // Enter at the first prompt is Continue.
    engine.begin(make_command("ARC"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({4, 2, 0}));

    const Arc* a = last_arc(db);
    REQUIRE(a != nullptr);
    // Tangent to the line at its end means the centre is square to it.
    CHECK(is_an_end(*a, Vec3{2, 0, 0}));
    CHECK(is_an_end(*a, Vec3{4, 2, 0}));
    CHECK_NEAR(a->center().x, 2.0, 1e-9);
}

TEST_CASE("arc: Continue from an arc leaves it tangentially too") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("ARC"));
    engine.supply(InputValue::of_point({1, 0, 0}));
    engine.supply(InputValue::of_keyword("CENTER"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_keyword("ANGLE"));
    engine.supply(InputValue::of_real(90.0));

    const Arc* first = last_arc(db);
    REQUIRE(first != nullptr);
    const Vec3 join = first->end_point();

    engine.begin(make_command("ARC"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({-2, 2, 0}));

    const Arc* second = last_arc(db);
    REQUIRE(second != nullptr);
    CHECK(second != first);
    CHECK(is_an_end(*second, join));
    CHECK(is_an_end(*second, Vec3{-2, 2, 0}));
}

TEST_CASE("arc: Continue with nothing to continue from fails rather than guesses") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("ARC"));
    engine.supply(InputValue::none());
    CHECK(engine.status() == EngineStatus::Failed);
    CHECK(db.empty());

    // A circle is not a thing you can continue from, and saying so beats
    // silently starting somewhere.
    db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 5.0));
    engine.begin(make_command("ARC"));
    engine.supply(InputValue::none());
    CHECK(engine.status() == EngineStatus::Failed);
}

TEST_CASE("arc: A is the R12 alias") {
    CHECK(resolve_command_name("A").name == "ARC");
    CHECK(resolve_command_name("arc").name == "ARC");
}
