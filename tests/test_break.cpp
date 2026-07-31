// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// BREAK, and the curve-editing primitives underneath it.
//
// The properties that carry the weight:
//
//   Cutting a bulged polyline segment RECOMPUTES its bulge. A bulge is the
//   quarter-angle tangent of an included angle, so half a segment is not half a
//   bulge. Getting this wrong is invisible on a straight polyline and obvious
//   on a curved one.
//
//   A closed curve gives one piece, an open curve two. And on a closed curve
//   the ORDER of the two points is the answer rather than an accident, because
//   which piece survives depends on which way round the loop was cut.
//
//   Parameters round-trip. curve_parameter_at and curve_point_at are inverses,
//   which is what lets a picked coordinate become a cut.

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

const Entity* at(const Database& db, std::size_t i) {
    return i < db.order().size() ? db.get(db.order()[i]) : nullptr;
}

}  // namespace

// --- curve_parameter_at -----------------------------------------------------

TEST_CASE("curve_parameter_at: a line is parameterised start to end") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    double t = 0.0;

    REQUIRE(curve_parameter_at(l, Vec3{2.5, 0, 0}, &t));
    CHECK_NEAR(t, 0.25, 1e-9);
    REQUIRE(curve_parameter_at(l, Vec3{10, 0, 0}, &t));
    CHECK_NEAR(t, 1.0, 1e-9);
}

TEST_CASE("curve_parameter_at: a point off the curve is projected onto it") {
    // A pick is never exactly on the line, which is the whole reason this
    // projects rather than requiring incidence.
    Line l{{0, 0, 0}, {10, 0, 0}};
    double t = 0.0;
    REQUIRE(curve_parameter_at(l, Vec3{5, 3, 0}, &t));
    CHECK_NEAR(t, 0.5, 1e-9);
}

TEST_CASE("curve_parameter_at: past the end clamps to the end") {
    // What makes BREAK's "second point beyond the endpoint" shorten the line
    // rather than fail.
    Line l{{0, 0, 0}, {10, 0, 0}};
    double t = 0.0;
    REQUIRE(curve_parameter_at(l, Vec3{50, 0, 0}, &t));
    CHECK_NEAR(t, 1.0, 1e-9);
    REQUIRE(curve_parameter_at(l, Vec3{-50, 0, 0}, &t));
    CHECK_NEAR(t, 0.0, 1e-9);
}

TEST_CASE("curve_parameter_at: inverts curve_point_at on a circle") {
    Circle c{{1, 2, 0}, 5.0};
    for (const double t : {0.0, 0.125, 0.4, 0.75, 0.99}) {
        Vec3 p{};
        REQUIRE(curve_point_at(c, t, &p));
        double back = 0.0;
        REQUIRE(curve_parameter_at(c, p, &back));
        CHECK_NEAR(back, t, 1e-9);
    }
}

TEST_CASE("curve_parameter_at: inverts curve_point_at on a bulged polyline") {
    Polyline p;
    p.add({0, 0, 0}, 0.5);
    p.add({10, 0, 0});
    p.add({10, 10, 0});

    for (const double t : {0.1, 0.25, 0.5, 0.8}) {
        Vec3 point{};
        REQUIRE(curve_point_at(p, t, &point));
        double back = 0.0;
        REQUIRE(curve_parameter_at(p, point, &back));
        CHECK_NEAR(back, t, 1e-7);
    }
}

TEST_CASE("curve_is_closed: a circle always, a polyline when it says so") {
    Circle c{{0, 0, 0}, 1.0};
    CHECK(curve_is_closed(c));

    Polyline open;
    open.add({0, 0, 0});
    open.add({1, 0, 0});
    CHECK(!curve_is_closed(open));

    open.set_closed(true);
    CHECK(curve_is_closed(open));

    Arc a{{0, 0, 0}, 1.0, 0.0, kPi};
    CHECK(!curve_is_closed(a));
}

// --- extract_curve_span -----------------------------------------------------

TEST_CASE("extract_curve_span: a piece of a line is a shorter line") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    EntityPtr piece = extract_curve_span(l, 0.25, 0.75);
    REQUIRE(piece != nullptr);
    REQUIRE(piece->type() == EntityType::Line);

    const Line* got = static_cast<const Line*>(piece.get());
    CHECK(near_equal(got->start(), Vec3{2.5, 0, 0}, 1e-9));
    CHECK(near_equal(got->end(), Vec3{7.5, 0, 0}, 1e-9));
}

TEST_CASE("extract_curve_span: a piece of a circle is an arc") {
    Circle c{{0, 0, 0}, 5.0};
    EntityPtr piece = extract_curve_span(c, 0.0, 0.25);
    REQUIRE(piece != nullptr);
    REQUIRE(piece->type() == EntityType::Arc);

    const Arc* got = static_cast<const Arc*>(piece.get());
    CHECK_NEAR(got->radius(), 5.0, 1e-9);
    CHECK_NEAR(got->sweep(), kPi / 2.0, 1e-9);
    CHECK(near_equal(got->start_point(), Vec3{5, 0, 0}, 1e-7));
    CHECK(near_equal(got->end_point(), Vec3{0, 5, 0}, 1e-7));
}

TEST_CASE("extract_curve_span: a wrapping span on a circle goes the long way") {
    Circle c{{0, 0, 0}, 5.0};
    // From three quarters round, back to one quarter: half the circle, through
    // the start point.
    EntityPtr piece = extract_curve_span(c, 0.75, 0.25);
    REQUIRE(piece != nullptr);
    const Arc* got = static_cast<const Arc*>(piece.get());
    CHECK_NEAR(got->sweep(), kPi, 1e-9);
}

TEST_CASE("extract_curve_span: properties carry over") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    l.props().color = 5;
    l.props().layer = 3;

    EntityPtr piece = extract_curve_span(l, 0.1, 0.9);
    REQUIRE(piece != nullptr);
    CHECK(piece->props().color == 5);
    CHECK(piece->props().layer == 3);
}

TEST_CASE("extract_curve_span: cutting a bulged segment recomputes the bulge") {
    // One half-turn segment. Half of it is a quarter turn, whose bulge is
    // tan(90/4 degrees) -- NOT half of the half-turn's bulge of 1.
    Polyline p;
    p.add({0, 0, 0}, 1.0);
    p.add({10, 0, 0});

    EntityPtr piece = extract_curve_span(p, 0.0, 0.5);
    REQUIRE(piece != nullptr);
    const Polyline* got = static_cast<const Polyline*>(piece.get());
    REQUIRE(got->size() >= 2);
    CHECK_NEAR(got->vertices()[0].bulge, std::tan(kPi * 0.125), 1e-9);
    // Emphatically not 0.5.
    CHECK(std::abs(got->vertices()[0].bulge - 0.5) > 0.05);
}

TEST_CASE("extract_curve_span: the piece of a polyline really follows the original") {
    // The strongest check available without a second implementation: points
    // sampled along the extracted piece must lie on the original curve.
    Polyline p;
    p.add({0, 0, 0}, 0.7);
    p.add({10, 0, 0}, -0.3);
    p.add({10, 10, 0});

    EntityPtr piece = extract_curve_span(p, 0.2, 0.8);
    REQUIRE(piece != nullptr);

    for (const double local : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        Vec3 on_piece{};
        REQUIRE(curve_point_at(*piece, local, &on_piece));

        // Its nearest point on the original must be itself.
        double t = 0.0;
        REQUIRE(curve_parameter_at(p, on_piece, &t));
        Vec3 on_original{};
        REQUIRE(curve_point_at(p, t, &on_original));
        CHECK(near_equal(on_piece, on_original, 1e-6));
    }
}

// --- break_curve ------------------------------------------------------------

TEST_CASE("break_curve: an open curve gives two pieces") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    std::vector<EntityPtr> pieces;
    CHECK(break_curve(l, 0.3, 0.7, pieces) == 2);

    CHECK(near_equal(static_cast<const Line*>(pieces[0].get())->end(), Vec3{3, 0, 0}, 1e-9));
    CHECK(near_equal(static_cast<const Line*>(pieces[1].get())->start(), Vec3{7, 0, 0}, 1e-9));
}

TEST_CASE("break_curve: the two points may arrive in either order") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    std::vector<EntityPtr> forward;
    std::vector<EntityPtr> backward;
    break_curve(l, 0.3, 0.7, forward);
    break_curve(l, 0.7, 0.3, backward);

    REQUIRE(forward.size() == 2);
    REQUIRE(backward.size() == 2);
    CHECK(near_equal(static_cast<const Line*>(forward[0].get())->end(),
                     static_cast<const Line*>(backward[0].get())->end(), 1e-12));
}

TEST_CASE("break_curve: breaking at an end shortens rather than leaving a stub") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    std::vector<EntityPtr> pieces;
    CHECK(break_curve(l, 0.0, 0.4, pieces) == 1);
    CHECK(near_equal(static_cast<const Line*>(pieces[0].get())->start(), Vec3{4, 0, 0}, 1e-9));
}

TEST_CASE("break_curve: breaking the whole span leaves nothing") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    std::vector<EntityPtr> pieces;
    CHECK(break_curve(l, 0.0, 1.0, pieces) == 0);
}

TEST_CASE("break_curve: equal parameters split without removing anything") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    std::vector<EntityPtr> pieces;
    REQUIRE(break_curve(l, 0.5, 0.5, pieces) == 2);

    const Line* a = static_cast<const Line*>(pieces[0].get());
    const Line* b = static_cast<const Line*>(pieces[1].get());
    CHECK(near_equal(a->end(), Vec3{5, 0, 0}, 1e-9));
    CHECK(near_equal(b->start(), Vec3{5, 0, 0}, 1e-9));
    // Together they still span the original.
    CHECK_NEAR(a->length() + b->length(), 10.0, 1e-9);
}

TEST_CASE("break_curve: a closed curve gives exactly one piece") {
    Circle c{{0, 0, 0}, 5.0};
    std::vector<EntityPtr> pieces;
    REQUIRE(break_curve(c, 0.0, 0.25, pieces) == 1);
    CHECK(pieces[0]->type() == EntityType::Arc);
    // A quarter removed leaves three quarters.
    CHECK_NEAR(static_cast<const Arc*>(pieces[0].get())->sweep(), kPi * 1.5, 1e-9);
}

TEST_CASE("break_curve: on a closed curve the order decides which piece survives") {
    // The asymmetry that makes picking two points on a circle predictable.
    Circle c{{0, 0, 0}, 5.0};

    std::vector<EntityPtr> forward;
    std::vector<EntityPtr> backward;
    REQUIRE(break_curve(c, 0.0, 0.25, forward) == 1);
    REQUIRE(break_curve(c, 0.25, 0.0, backward) == 1);

    const double a = static_cast<const Arc*>(forward[0].get())->sweep();
    const double b = static_cast<const Arc*>(backward[0].get())->sweep();
    CHECK_NEAR(a, kPi * 1.5, 1e-9);
    CHECK_NEAR(b, kPi * 0.5, 1e-9);
    // Between them they account for the whole circle.
    CHECK_NEAR(a + b, kPi * 2.0, 1e-9);
}

TEST_CASE("break_curve: a closed curve cannot be split at a single point") {
    Circle c{{0, 0, 0}, 5.0};
    std::vector<EntityPtr> pieces;
    CHECK(break_curve(c, 0.5, 0.5, pieces) == 0);
}

TEST_CASE("break_curve: a broken closed polyline comes back open") {
    Polyline p;
    p.add({0, 0, 0});
    p.add({10, 0, 0});
    p.add({10, 10, 0});
    p.add({0, 10, 0});
    p.set_closed(true);

    std::vector<EntityPtr> pieces;
    REQUIRE(break_curve(p, 0.1, 0.2, pieces) == 1);
    const Polyline* got = static_cast<const Polyline*>(pieces[0].get());
    CHECK(!got->closed());
}

// --- the command ------------------------------------------------------------

TEST_CASE("break: the picked point is the first break point") {
    Database db;
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("BREAK"));
    engine.supply(InputValue::of_picked_entity(h, Vec3{3, 0, 0}));
    engine.supply(InputValue::of_point({7, 0, 0}));

    REQUIRE(db.size() == 2);
    CHECK(near_equal(static_cast<const Line*>(at(db, 0))->end(), Vec3{3, 0, 0}, 1e-9));
    CHECK(near_equal(static_cast<const Line*>(at(db, 1))->start(), Vec3{7, 0, 0}, 1e-9));
}

TEST_CASE("break: the first piece keeps the original handle") {
    // So an ename held by AutoLISP still refers to something recognisable.
    Database db;
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("BREAK"));
    engine.supply(InputValue::of_picked_entity(h, Vec3{3, 0, 0}));
    engine.supply(InputValue::of_point({7, 0, 0}));

    const Entity* kept = db.get(h);
    REQUIRE(kept != nullptr);
    CHECK(near_equal(static_cast<const Line*>(kept)->end(), Vec3{3, 0, 0}, 1e-9));
}

TEST_CASE("break: First re-asks for the first point") {
    Database db;
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("BREAK"));
    engine.supply(InputValue::of_picked_entity(h, Vec3{9, 0, 0}));  // a careless pick
    engine.supply(InputValue::of_keyword("FIRST"));
    engine.supply(InputValue::of_point({2, 0, 0}));
    engine.supply(InputValue::of_point({4, 0, 0}));

    REQUIRE(db.size() == 2);
    CHECK(near_equal(static_cast<const Line*>(at(db, 0))->end(), Vec3{2, 0, 0}, 1e-9));
    CHECK(near_equal(static_cast<const Line*>(at(db, 1))->start(), Vec3{4, 0, 0}, 1e-9));
}

TEST_CASE("break: a typed handle carries no location, so it asks for the first point") {
    // A LISP ename or a typed handle has no pick point. Breaking at wherever
    // the origin happens to project to would be worse than asking.
    Database db;
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("BREAK"));
    engine.supply(InputValue::of_entity(h));
    CHECK(engine.active());
    engine.supply(InputValue::of_point({2, 0, 0}));
    engine.supply(InputValue::of_point({6, 0, 0}));

    REQUIRE(db.size() == 2);
    CHECK(near_equal(static_cast<const Line*>(at(db, 1))->start(), Vec3{6, 0, 0}, 1e-9));
}

TEST_CASE("break: @ at the second prompt splits without removing") {
    Database db;
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("BREAK"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_point({5, 0, 0}));
    engine.supply(InputValue::of_keyword("AT"));

    REQUIRE(db.size() == 2);
    CHECK(near_equal(static_cast<const Line*>(at(db, 0))->end(), Vec3{5, 0, 0}, 1e-9));
    CHECK(near_equal(static_cast<const Line*>(at(db, 1))->start(), Vec3{5, 0, 0}, 1e-9));
}

TEST_CASE("break: a circle becomes an arc") {
    Database db;
    const Handle h = db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 5.0));

    CommandEngine engine(db);
    engine.begin(make_command("BREAK"));
    engine.supply(InputValue::of_picked_entity(h, Vec3{5, 0, 0}));
    engine.supply(InputValue::of_point({0, 5, 0}));

    REQUIRE(db.size() == 1);
    const Entity* e = at(db, 0);
    REQUIRE(e->type() == EntityType::Arc);
    CHECK_NEAR(static_cast<const Arc*>(e)->sweep(), kPi * 1.5, 1e-7);
}

TEST_CASE("break: breaking an arc leaves the two ends") {
    Database db;
    // The upper half of a circle.
    const Handle h = db.add(std::make_unique<Arc>(Vec3{0, 0, 0}, 5.0, 0.0, kPi));

    CommandEngine engine(db);
    engine.begin(make_command("BREAK"));
    engine.supply(InputValue::of_picked_entity(h, Vec3{5, 0, 0}));
    engine.supply(InputValue::of_point({0, 5, 0}));

    // Picked at the start and broken to the top: the first piece is empty, so
    // only the far half survives.
    REQUIRE(db.size() == 1);
    CHECK_NEAR(static_cast<const Arc*>(at(db, 0))->sweep(), kPi / 2.0, 1e-7);
}

TEST_CASE("break: a polyline splits into two polylines") {
    Database db;
    auto p = std::make_unique<Polyline>();
    p->add({0, 0, 0});
    p->add({10, 0, 0});
    p->add({10, 10, 0});
    const Handle h = db.add(std::move(p));

    CommandEngine engine(db);
    engine.begin(make_command("BREAK"));
    engine.supply(InputValue::of_picked_entity(h, Vec3{4, 0, 0}));
    engine.supply(InputValue::of_point({10, 4, 0}));

    REQUIRE(db.size() == 2);
    CHECK(at(db, 0)->type() == EntityType::Polyline);
    CHECK(at(db, 1)->type() == EntityType::Polyline);
    CHECK(near_equal(static_cast<const Polyline*>(at(db, 0))->vertices().back().pos,
                     Vec3{4, 0, 0}, 1e-7));
}

TEST_CASE("break: refuses something with no curve") {
    Database db;
    const Handle h = db.add(std::make_unique<PointEntity>(Vec3{0, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("BREAK"));
    const EngineStatus status = engine.supply(InputValue::of_picked_entity(h, Vec3{0, 0, 0}));
    CHECK(status == EngineStatus::Failed);
    CHECK(db.size() == 1);
}

TEST_CASE("break: a closed object refuses a single break point") {
    Database db;
    const Handle h = db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 5.0));

    CommandEngine engine(db);
    engine.begin(make_command("BREAK"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_point({5, 0, 0}));
    const EngineStatus status = engine.supply(InputValue::of_keyword("AT"));

    CHECK(status == EngineStatus::Failed);
    // Still a circle, untouched.
    CHECK(at(db, 0)->type() == EntityType::Circle);
}

TEST_CASE("break: one UNDO restores the original") {
    Database db;
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("BREAK"));
    engine.supply(InputValue::of_picked_entity(h, Vec3{3, 0, 0}));
    engine.supply(InputValue::of_point({7, 0, 0}));
    CHECK(db.size() == 2);

    engine.begin(make_command("UNDO"));
    REQUIRE(db.size() == 1);
    const Line* back = static_cast<const Line*>(db.get(h));
    REQUIRE(back != nullptr);
    CHECK(near_equal(back->end(), Vec3{10, 0, 0}, 1e-9));
}
