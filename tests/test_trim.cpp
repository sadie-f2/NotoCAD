// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// TRIM and EXTEND, and the primitives underneath them.
//
// The properties that carry the weight:
//
//   The pick point is the argument. A line crossing three edges has four
//   pieces, and which one is removed is answered by where you pointed and by
//   nothing else. Every interesting case here is a different pick on the same
//   geometry.
//
//   Trimming past the outermost intersection removes the overshoot. That is
//   most of what TRIM is used for and it falls out of treating the curve's ends
//   as cuts -- which a closed curve does not have, hence its separate rule.
//
//   EXTEND reaches the FIRST boundary, not the furthest.
//
//   And the check that caught the BREAK bug, repeated here: sample the result
//   and assert every sample lies on the original curve.

#include "test.hpp"

#include "ncad/command.hpp"
#include "ncad/commands.hpp"
#include "ncad/curve_edit.hpp"
#include "ncad/database.hpp"
#include "ncad/entities.hpp"
#include "ncad/intersect.hpp"

#include <cmath>
#include <memory>
#include <numbers>

using namespace ncad;

namespace {

constexpr double kPi = std::numbers::pi;

// Every sample of `piece` must lie somewhere on `original`.
void check_lies_on(const Entity& piece, const Entity& original, double tol = 1e-6) {
    for (const double local : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        Vec3 on_piece{};
        if (!curve_point_at(piece, local, &on_piece)) {
            CHECK(false);
            return;
        }
        double t = 0.0;
        Vec3 on_original{};
        if (!curve_parameter_at(original, on_piece, &t) ||
            !curve_point_at(original, t, &on_original)) {
            CHECK(false);
            return;
        }
        CHECK(near_equal(on_piece, on_original, tol));
    }
}

}  // namespace

// --- trim_span --------------------------------------------------------------

TEST_CASE("trim_span: the ends of an open curve count as cuts") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    double lo = 0.0;
    double hi = 0.0;

    // One cut at the middle; a pick on the near side removes the near half.
    REQUIRE(trim_span(l, {0.5}, 0.2, &lo, &hi));
    CHECK_NEAR(lo, 0.0, 1e-12);
    CHECK_NEAR(hi, 0.5, 1e-12);

    // And a pick on the far side removes the far half.
    REQUIRE(trim_span(l, {0.5}, 0.8, &lo, &hi));
    CHECK_NEAR(lo, 0.5, 1e-12);
    CHECK_NEAR(hi, 1.0, 1e-12);
}

TEST_CASE("trim_span: a pick between two cuts removes only what is between") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    double lo = 0.0;
    double hi = 0.0;
    REQUIRE(trim_span(l, {0.25, 0.75}, 0.5, &lo, &hi));
    CHECK_NEAR(lo, 0.25, 1e-12);
    CHECK_NEAR(hi, 0.75, 1e-12);
}

TEST_CASE("trim_span: no cuts means nothing to trim") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    double lo = 0.0;
    double hi = 0.0;
    CHECK(!trim_span(l, {}, 0.5, &lo, &hi));
}

TEST_CASE("trim_span: coincident cuts collapse") {
    // Two cutting edges crossing at the same place is one cut, not a
    // zero-length stretch the pick can fall inside.
    Line l{{0, 0, 0}, {10, 0, 0}};
    double lo = 0.0;
    double hi = 0.0;
    REQUIRE(trim_span(l, {0.5, 0.5, 0.5}, 0.8, &lo, &hi));
    CHECK_NEAR(lo, 0.5, 1e-12);
    CHECK_NEAR(hi, 1.0, 1e-12);
}

TEST_CASE("trim_span: a closed curve needs two cuts") {
    Circle c{{0, 0, 0}, 5.0};
    double lo = 0.0;
    double hi = 0.0;
    // One cut leaves nowhere for the stretch to end.
    CHECK(!trim_span(c, {0.25}, 0.5, &lo, &hi));
    CHECK(trim_span(c, {0.25, 0.75}, 0.5, &lo, &hi));
}

TEST_CASE("trim_span: a closed curve's stretch can wrap through the start") {
    Circle c{{0, 0, 0}, 5.0};
    double lo = 0.0;
    double hi = 0.0;
    // Cuts at a quarter and three quarters; a pick just after the start is in
    // the stretch running from 0.75 round through 0 to 0.25.
    REQUIRE(trim_span(c, {0.25, 0.75}, 0.05, &lo, &hi));
    CHECK_NEAR(lo, 0.75, 1e-12);
    CHECK_NEAR(hi, 0.25, 1e-12);
}

// --- extend_curve -----------------------------------------------------------

TEST_CASE("extend_curve: a line grows along itself") {
    Line l{{0, 0, 0}, {10, 0, 0}};

    EntityPtr grown = extend_curve(l, 1.5);
    REQUIRE(grown != nullptr);
    const Line* got = static_cast<const Line*>(grown.get());
    CHECK(near_equal(got->start(), Vec3{0, 0, 0}, 1e-9));
    CHECK(near_equal(got->end(), Vec3{15, 0, 0}, 1e-9));
}

TEST_CASE("extend_curve: a negative parameter grows the other end") {
    Line l{{0, 0, 0}, {10, 0, 0}};

    EntityPtr grown = extend_curve(l, -0.5);
    REQUIRE(grown != nullptr);
    const Line* got = static_cast<const Line*>(grown.get());
    CHECK(near_equal(got->start(), Vec3{-5, 0, 0}, 1e-9));
    CHECK(near_equal(got->end(), Vec3{10, 0, 0}, 1e-9));
}

TEST_CASE("extend_curve: an arc grows its sweep") {
    Arc a{{0, 0, 0}, 5.0, 0.0, kPi / 2.0};

    EntityPtr grown = extend_curve(a, 2.0);
    REQUIRE(grown != nullptr);
    const Arc* got = static_cast<const Arc*>(grown.get());
    CHECK_NEAR(got->sweep(), kPi, 1e-9);
    CHECK_NEAR(got->radius(), 5.0, 1e-9);
    CHECK(near_equal(got->start_point(), Vec3{5, 0, 0}, 1e-7));
}

TEST_CASE("extend_curve: an arc will not swallow itself") {
    Arc a{{0, 0, 0}, 5.0, 0.0, kPi};
    // Growing to three times its sweep would pass a full turn.
    CHECK(extend_curve(a, 3.0) == nullptr);
}

TEST_CASE("extend_curve: a closed curve has no end to grow") {
    Circle c{{0, 0, 0}, 5.0};
    CHECK(extend_curve(c, 1.5) == nullptr);

    Polyline p;
    p.add({0, 0, 0});
    p.add({10, 0, 0});
    p.add({10, 10, 0});
    p.set_closed(true);
    CHECK(extend_curve(p, 1.5) == nullptr);
}

TEST_CASE("extend_curve: a parameter already on the curve grows nothing") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    CHECK(extend_curve(l, 0.5) == nullptr);
}

TEST_CASE("extend_curve: a polyline grows its terminal segment") {
    Polyline p;
    p.add({0, 0, 0});
    p.add({10, 0, 0});
    p.add({10, 10, 0});

    // Two segments, so 1.25 is a quarter past the end of the second.
    EntityPtr grown = extend_curve(p, 1.25);
    REQUIRE(grown != nullptr);
    const Polyline* got = static_cast<const Polyline*>(grown.get());
    REQUIRE(got->size() == 3);
    CHECK(near_equal(got->vertices()[2].pos, Vec3{10, 15, 0}, 1e-9));
    // The rest is untouched.
    CHECK(near_equal(got->vertices()[0].pos, Vec3{0, 0, 0}, 1e-9));
}

TEST_CASE("extend_curve: growing a curved terminal segment stays on its arc") {
    Polyline p;
    p.add({0, 0, 0}, 1.0);  // a half turn
    p.add({10, 0, 0});

    EntityPtr grown = extend_curve(p, 1.5);
    REQUIRE(grown != nullptr);
    const Polyline* got = static_cast<const Polyline*>(grown.get());

    // The arc had centre (5,0) and radius 5. Growing it by half again means
    // three quarters of a turn, which ends back up at (5,5).
    Vec3 end{};
    REQUIRE(curve_point_at(*got, 1.0, &end));
    CHECK_NEAR(length(end - Vec3{5, 0, 0}), 5.0, 1e-7);

    // And the original half is still where it was.
    Vec3 middle{};
    REQUIRE(curve_point_at(*got, 0.0, &middle));
    CHECK(near_equal(middle, Vec3{0, 0, 0}, 1e-7));
}

// --- TRIM -------------------------------------------------------------------

TEST_CASE("trim: a line is cut back to the edge it crosses") {
    Database db;
    const Handle edge = db.add(std::make_unique<Line>(Vec3{5, -5, 0}, Vec3{5, 5, 0}));
    const Handle target = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("TRIM"));
    engine.supply(InputValue::of_entity(edge));
    engine.supply(InputValue::none());  // done selecting edges
    // Point at the right-hand piece, which is the one to go.
    engine.supply(InputValue::of_picked_entity(target, Vec3{8, 0, 0}));
    engine.supply(InputValue::none());  // done picking

    REQUIRE(db.size() == 2);
    const Line* left = static_cast<const Line*>(db.get(target));
    REQUIRE(left != nullptr);
    CHECK(near_equal(left->start(), Vec3{0, 0, 0}, 1e-9));
    CHECK(near_equal(left->end(), Vec3{5, 0, 0}, 1e-9));
}

TEST_CASE("trim: the pick point decides which piece goes") {
    Database db;
    const Handle edge = db.add(std::make_unique<Line>(Vec3{5, -5, 0}, Vec3{5, 5, 0}));
    const Handle target = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("TRIM"));
    engine.supply(InputValue::of_entity(edge));
    engine.supply(InputValue::none());
    // The other side this time.
    engine.supply(InputValue::of_picked_entity(target, Vec3{2, 0, 0}));
    engine.supply(InputValue::none());

    const Line* right = static_cast<const Line*>(db.get(target));
    REQUIRE(right != nullptr);
    CHECK(near_equal(right->start(), Vec3{5, 0, 0}, 1e-9));
    CHECK(near_equal(right->end(), Vec3{10, 0, 0}, 1e-9));
}

TEST_CASE("trim: a piece between two edges leaves two pieces behind") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{3, -5, 0}, Vec3{3, 5, 0}));
    db.add(std::make_unique<Line>(Vec3{7, -5, 0}, Vec3{7, 5, 0}));
    const Handle target = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("TRIM"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_picked_entity(target, Vec3{5, 0, 0}));
    engine.supply(InputValue::none());

    // Two edges plus the two surviving pieces.
    REQUIRE(db.size() == 4);
    const Line* head = static_cast<const Line*>(db.get(target));
    REQUIRE(head != nullptr);
    CHECK(near_equal(head->end(), Vec3{3, 0, 0}, 1e-9));
}

TEST_CASE("trim: an object crossing nothing is left alone and the command carries on") {
    Database db;
    const Handle edge = db.add(std::make_unique<Line>(Vec3{5, -5, 0}, Vec3{5, 5, 0}));
    const Handle stray = db.add(std::make_unique<Line>(Vec3{0, 20, 0}, Vec3{10, 20, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("TRIM"));
    engine.supply(InputValue::of_entity(edge));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_picked_entity(stray, Vec3{5, 20, 0}));
    // Still running, not failed: picking something that misses is ordinary.
    CHECK(engine.active());
    engine.supply(InputValue::none());

    CHECK(db.size() == 2);
    CHECK_NEAR(static_cast<const Line*>(db.get(stray))->length(), 10.0, 1e-9);
}

TEST_CASE("trim: a circle cut twice keeps the arc the pick was not in") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{-10, 0, 0}, Vec3{10, 0, 0}));
    const Handle target = db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 5.0));

    CommandEngine engine(db);
    engine.begin(make_command("TRIM"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    // Point at the top half.
    engine.supply(InputValue::of_picked_entity(target, Vec3{0, 5, 0}));
    engine.supply(InputValue::none());

    const Entity* left = db.get(target);
    REQUIRE(left != nullptr);
    REQUIRE(left->type() == EntityType::Arc);
    // The bottom half survives.
    CHECK_NEAR(static_cast<const Arc*>(left)->sweep(), kPi, 1e-7);
    Vec3 mid{};
    REQUIRE(curve_point_at(*left, 0.5, &mid));
    CHECK(mid.y < 0.0);
}

TEST_CASE("trim: the result lies on the original curve") {
    // The check that caught the BREAK bug, on a bulged polyline this time.
    Database db;
    db.add(std::make_unique<Line>(Vec3{5, -20, 0}, Vec3{5, 20, 0}));

    auto p = std::make_unique<Polyline>();
    p->add({0, 0, 0}, -0.6);
    p->add({10, 0, 0}, 0.4);
    p->add({10, 10, 0});
    const Polyline original = *p;
    const Handle target = db.add(std::move(p));

    CommandEngine engine(db);
    engine.begin(make_command("TRIM"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_picked_entity(target, Vec3{1, 1, 0}));
    engine.supply(InputValue::none());

    const Entity* result = db.get(target);
    REQUIRE(result != nullptr);
    check_lies_on(*result, original);
}

TEST_CASE("trim: Undo puts the object back") {
    Database db;
    const Handle edge = db.add(std::make_unique<Line>(Vec3{5, -5, 0}, Vec3{5, 5, 0}));
    const Handle target = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("TRIM"));
    engine.supply(InputValue::of_entity(edge));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_picked_entity(target, Vec3{8, 0, 0}));
    CHECK_NEAR(static_cast<const Line*>(db.get(target))->length(), 5.0, 1e-9);

    engine.supply(InputValue::of_keyword("UNDO"));
    CHECK_NEAR(static_cast<const Line*>(db.get(target))->length(), 10.0, 1e-9);
    CHECK(engine.active());
    engine.supply(InputValue::none());
}

TEST_CASE("trim: Undo of a split removes the piece it created") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{3, -5, 0}, Vec3{3, 5, 0}));
    db.add(std::make_unique<Line>(Vec3{7, -5, 0}, Vec3{7, 5, 0}));
    const Handle target = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("TRIM"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_picked_entity(target, Vec3{5, 0, 0}));
    CHECK(db.size() == 4);

    engine.supply(InputValue::of_keyword("UNDO"));
    CHECK(db.size() == 3);
    CHECK_NEAR(static_cast<const Line*>(db.get(target))->length(), 10.0, 1e-9);
    engine.supply(InputValue::none());
}

TEST_CASE("trim: a pick with no location is refused rather than guessed") {
    Database db;
    const Handle edge = db.add(std::make_unique<Line>(Vec3{5, -5, 0}, Vec3{5, 5, 0}));
    const Handle target = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("TRIM"));
    engine.supply(InputValue::of_entity(edge));
    engine.supply(InputValue::none());
    const EngineStatus status = engine.supply(InputValue::of_entity(target));

    CHECK(status == EngineStatus::Failed);
    CHECK_NEAR(static_cast<const Line*>(db.get(target))->length(), 10.0, 1e-9);
}

TEST_CASE("trim: skew geometry does not trim, however it looks in plan") {
    // The 3-space rule, from the command's side.
    Database db;
    db.add(std::make_unique<Line>(Vec3{5, -5, 3}, Vec3{5, 5, 3}));
    const Handle target = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("TRIM"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_picked_entity(target, Vec3{8, 0, 0}));
    engine.supply(InputValue::none());

    CHECK_NEAR(static_cast<const Line*>(db.get(target))->length(), 10.0, 1e-9);
}

TEST_CASE("trim: with no cutting edges selected it refuses") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("TRIM"));
    const EngineStatus status = engine.supply(InputValue::none());
    CHECK(status == EngineStatus::Failed);
}

TEST_CASE("trim: the whole command is one step of the drawing's undo") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{3, -5, 0}, Vec3{3, 5, 0}));
    db.add(std::make_unique<Line>(Vec3{7, -5, 0}, Vec3{7, 5, 0}));
    const Handle a = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));
    const Handle b = db.add(std::make_unique<Line>(Vec3{0, 1, 0}, Vec3{10, 1, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("TRIM"));
    engine.supply(InputValue::of_entity(db.order()[0]));
    engine.supply(InputValue::of_entity(db.order()[1]));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_picked_entity(a, Vec3{9, 0, 0}));
    engine.supply(InputValue::of_picked_entity(b, Vec3{9, 1, 0}));
    engine.supply(InputValue::none());

    CHECK_NEAR(static_cast<const Line*>(db.get(a))->length(), 7.0, 1e-9);

    engine.begin(make_command("UNDO"));
    CHECK_NEAR(static_cast<const Line*>(db.get(a))->length(), 10.0, 1e-9);
    CHECK_NEAR(static_cast<const Line*>(db.get(b))->length(), 10.0, 1e-9);
}

// --- EXTEND -----------------------------------------------------------------

TEST_CASE("extend: a line grows to reach the boundary") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{20, -5, 0}, Vec3{20, 5, 0}));
    const Handle target = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("EXTEND"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    // Point near the far end, which is the end that grows.
    engine.supply(InputValue::of_picked_entity(target, Vec3{9, 0, 0}));
    engine.supply(InputValue::none());

    const Line* grown = static_cast<const Line*>(db.get(target));
    REQUIRE(grown != nullptr);
    CHECK(near_equal(grown->end(), Vec3{20, 0, 0}, 1e-7));
    CHECK(near_equal(grown->start(), Vec3{0, 0, 0}, 1e-9));
}

TEST_CASE("extend: the pick point decides which end grows") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{-20, -5, 0}, Vec3{-20, 5, 0}));
    const Handle target = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("EXTEND"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    // The near end this time.
    engine.supply(InputValue::of_picked_entity(target, Vec3{1, 0, 0}));
    engine.supply(InputValue::none());

    const Line* grown = static_cast<const Line*>(db.get(target));
    CHECK(near_equal(grown->start(), Vec3{-20, 0, 0}, 1e-7));
    CHECK(near_equal(grown->end(), Vec3{10, 0, 0}, 1e-9));
}

TEST_CASE("extend: it reaches the first boundary, not the furthest") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{15, -5, 0}, Vec3{15, 5, 0}));
    db.add(std::make_unique<Line>(Vec3{30, -5, 0}, Vec3{30, 5, 0}));
    const Handle target = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("EXTEND"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_picked_entity(target, Vec3{9, 0, 0}));
    engine.supply(InputValue::none());

    CHECK(near_equal(static_cast<const Line*>(db.get(target))->end(), Vec3{15, 0, 0}, 1e-7));
}

TEST_CASE("extend: a boundary the extension would miss is not reached") {
    // The boundary is real but too short: extending the line would pass above
    // it, so there is nothing to reach.
    Database db;
    db.add(std::make_unique<Line>(Vec3{20, 5, 0}, Vec3{20, 10, 0}));
    const Handle target = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("EXTEND"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_picked_entity(target, Vec3{9, 0, 0}));
    engine.supply(InputValue::none());

    CHECK_NEAR(static_cast<const Line*>(db.get(target))->length(), 10.0, 1e-9);
}

TEST_CASE("extend: an arc grows round to its boundary") {
    Database db;
    // A vertical line the arc will reach if it sweeps far enough.
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{0, 10, 0}));
    // A quarter circle from (5,0) going counterclockwise, stopping short.
    const Handle target = db.add(std::make_unique<Arc>(Vec3{0, 0, 0}, 5.0, 0.0, kPi / 4.0));

    CommandEngine engine(db);
    engine.begin(make_command("EXTEND"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_picked_entity(target, Vec3{3.5, 3.5, 0}));
    engine.supply(InputValue::none());

    const Arc* grown = static_cast<const Arc*>(db.get(target));
    REQUIRE(grown != nullptr);
    // Reaches (0,5), a quarter turn from the start.
    CHECK_NEAR(grown->sweep(), kPi / 2.0, 1e-6);
}

TEST_CASE("extend: Undo puts the object back") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{20, -5, 0}, Vec3{20, 5, 0}));
    const Handle target = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("EXTEND"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_picked_entity(target, Vec3{9, 0, 0}));
    CHECK_NEAR(static_cast<const Line*>(db.get(target))->length(), 20.0, 1e-7);

    engine.supply(InputValue::of_keyword("UNDO"));
    CHECK_NEAR(static_cast<const Line*>(db.get(target))->length(), 10.0, 1e-9);
    engine.supply(InputValue::none());
}

TEST_CASE("extend: a closed object cannot be extended") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{20, -5, 0}, Vec3{20, 5, 0}));
    const Handle target = db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 5.0));

    CommandEngine engine(db);
    engine.begin(make_command("EXTEND"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_picked_entity(target, Vec3{5, 0, 0}));
    engine.supply(InputValue::none());

    // Still a circle, untouched.
    CHECK(db.get(target)->type() == EntityType::Circle);
    CHECK_NEAR(static_cast<const Circle*>(db.get(target))->radius(), 5.0, 1e-9);
}

TEST_CASE("extend: a polyline grows its terminal segment to the boundary") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{5, 20, 0}, Vec3{15, 20, 0}));

    auto p = std::make_unique<Polyline>();
    p->add({0, 0, 0});
    p->add({10, 0, 0});
    p->add({10, 10, 0});
    const Handle target = db.add(std::move(p));

    CommandEngine engine(db);
    engine.begin(make_command("EXTEND"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_picked_entity(target, Vec3{10, 9, 0}));
    engine.supply(InputValue::none());

    const Polyline* grown = static_cast<const Polyline*>(db.get(target));
    REQUIRE(grown != nullptr);
    REQUIRE(grown->size() == 3);
    CHECK(near_equal(grown->vertices()[2].pos, Vec3{10, 20, 0}, 1e-7));
}
