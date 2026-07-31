// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// PEDIT. Close/Open and Width are bookkeeping; Join is where the work is,
// because a segment can arrive pointing either way and an arc's bulge has to
// survive being reversed.

#include "test.hpp"

#include "ncad/command.hpp"
#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/entities.hpp"

#include <cmath>
#include <memory>
#include <numbers>

using namespace ncad;

namespace {

constexpr double kPi = std::numbers::pi;

// An open polyline from (0,0) to (10,0).
Handle add_base(Database& db) {
    auto p = std::make_unique<Polyline>();
    p->add({0, 0, 0});
    p->add({10, 0, 0});
    return db.add(std::move(p));
}

const Polyline* poly(const Database& db, Handle h) {
    const Entity* e = db.get(h);
    if (!e || e->type() != EntityType::Polyline) return nullptr;
    return static_cast<const Polyline*>(e);
}

}  // namespace

TEST_CASE("pedit: refuses anything that is not a polyline") {
    Database db;
    const Handle line = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("PEDIT"));
    const EngineStatus status = engine.supply(InputValue::of_entity(line));
    CHECK(status == EngineStatus::Failed);
}

TEST_CASE("pedit: Close sets the flag, Open clears it again") {
    Database db;
    auto p = std::make_unique<Polyline>();
    p->add({0, 0, 0});
    p->add({10, 0, 0});
    p->add({10, 10, 0});
    const Handle h = db.add(std::move(p));

    CommandEngine engine(db);
    engine.begin(make_command("PEDIT"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_keyword("CLOSE"));
    REQUIRE(poly(db, h) != nullptr);
    CHECK(poly(db, h)->closed());

    engine.supply(InputValue::of_keyword("OPEN"));
    CHECK(!poly(db, h)->closed());

    engine.supply(InputValue::none());  // eXit
}

TEST_CASE("pedit: a two-vertex polyline cannot be closed") {
    Database db;
    const Handle h = add_base(db);

    CommandEngine engine(db);
    engine.begin(make_command("PEDIT"));
    engine.supply(InputValue::of_entity(h));
    const EngineStatus status = engine.supply(InputValue::of_keyword("CLOSE"));
    CHECK(status == EngineStatus::Failed);
    CHECK(!poly(db, h)->closed());
}

TEST_CASE("pedit: Width sets every segment at once") {
    Database db;
    auto p = std::make_unique<Polyline>();
    p->add({0, 0, 0});
    p->add({10, 0, 0});
    p->add({20, 0, 0});
    const Handle h = db.add(std::move(p));

    CommandEngine engine(db);
    engine.begin(make_command("PEDIT"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_keyword("WIDTH"));
    engine.supply(InputValue::of_real(1.5));
    engine.supply(InputValue::none());

    const Polyline* result = poly(db, h);
    REQUIRE(result != nullptr);
    for (const PolyVertex& v : result->vertices()) {
        CHECK_NEAR(v.start_width, 1.5, 1e-12);
        CHECK_NEAR(v.end_width, 1.5, 1e-12);
    }
}

// --- Join -------------------------------------------------------------------

TEST_CASE("pedit: Join appends a line that meets the tail") {
    Database db;
    const Handle h = add_base(db);
    const Handle line = db.add(std::make_unique<Line>(Vec3{10, 0, 0}, Vec3{10, 10, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("PEDIT"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_keyword("JOIN"));
    engine.supply(InputValue::of_entity(line));
    engine.supply(InputValue::none());  // done selecting
    engine.supply(InputValue::none());  // eXit

    const Polyline* result = poly(db, h);
    REQUIRE(result != nullptr);
    REQUIRE(result->size() == 3);
    CHECK(near_equal(result->vertices()[2].pos, Vec3{10, 10, 0}, 1e-12));
    // The joined entity is consumed, not left lying underneath.
    CHECK(db.get(line) == nullptr);
    CHECK(db.size() == 1);
}

TEST_CASE("pedit: Join reverses a line that arrives backwards") {
    Database db;
    const Handle h = add_base(db);
    // Runs INTO the tail rather than out of it.
    const Handle line = db.add(std::make_unique<Line>(Vec3{10, 10, 0}, Vec3{10, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("PEDIT"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_keyword("JOIN"));
    engine.supply(InputValue::of_entity(line));
    engine.supply(InputValue::none());
    engine.supply(InputValue::none());

    const Polyline* result = poly(db, h);
    REQUIRE(result != nullptr);
    REQUIRE(result->size() == 3);
    CHECK(near_equal(result->vertices()[2].pos, Vec3{10, 10, 0}, 1e-12));
}

TEST_CASE("pedit: Join prepends a line that meets the head") {
    Database db;
    const Handle h = add_base(db);
    const Handle line = db.add(std::make_unique<Line>(Vec3{-5, 0, 0}, Vec3{0, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("PEDIT"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_keyword("JOIN"));
    engine.supply(InputValue::of_entity(line));
    engine.supply(InputValue::none());
    engine.supply(InputValue::none());

    const Polyline* result = poly(db, h);
    REQUIRE(result != nullptr);
    REQUIRE(result->size() == 3);
    CHECK(near_equal(result->vertices()[0].pos, Vec3{-5, 0, 0}, 1e-12));
    CHECK(near_equal(result->vertices()[2].pos, Vec3{10, 0, 0}, 1e-12));
}

TEST_CASE("pedit: Join leaves a segment that touches nothing alone") {
    Database db;
    const Handle h = add_base(db);
    const Handle stray = db.add(std::make_unique<Line>(Vec3{50, 50, 0}, Vec3{60, 60, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("PEDIT"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_keyword("JOIN"));
    engine.supply(InputValue::of_entity(stray));
    engine.supply(InputValue::none());
    engine.supply(InputValue::none());

    CHECK(poly(db, h)->size() == 2);
    // Not consumed, because it was not joined.
    CHECK(db.get(stray) != nullptr);
}

TEST_CASE("pedit: Join takes several passes when segments arrive out of order") {
    // The far segment can only join once the near one has, which is what the
    // repeated passes are for.
    Database db;
    const Handle h = add_base(db);
    const Handle far = db.add(std::make_unique<Line>(Vec3{20, 0, 0}, Vec3{30, 0, 0}));
    const Handle near = db.add(std::make_unique<Line>(Vec3{10, 0, 0}, Vec3{20, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("PEDIT"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_keyword("JOIN"));
    engine.supply(InputValue::of_entity(far));
    engine.supply(InputValue::of_entity(near));
    engine.supply(InputValue::none());
    engine.supply(InputValue::none());

    const Polyline* result = poly(db, h);
    REQUIRE(result != nullptr);
    REQUIRE(result->size() == 4);
    CHECK(near_equal(result->vertices()[3].pos, Vec3{30, 0, 0}, 1e-12));
}

TEST_CASE("pedit: joining an arc brings its bulge with it") {
    Database db;
    const Handle h = add_base(db);
    // A quarter turn counterclockwise from (10,0) about (10,10): starts where
    // the polyline ends.
    auto arc = std::make_unique<Arc>(Vec3{10, 10, 0}, 10.0, -kPi / 2.0, 0.0);
    const Handle ah = db.add(std::move(arc));

    CommandEngine engine(db);
    engine.begin(make_command("PEDIT"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_keyword("JOIN"));
    engine.supply(InputValue::of_entity(ah));
    engine.supply(InputValue::none());
    engine.supply(InputValue::none());

    const Polyline* result = poly(db, h);
    REQUIRE(result != nullptr);
    REQUIRE(result->size() == 3);
    // A quarter turn is tan(90/4 degrees).
    CHECK_NEAR(result->vertices()[1].bulge, std::tan(kPi * 0.125), 1e-9);

    // And the bulge really does describe an arc back at the original centre.
    Vec3 centre{};
    double radius = 0.0;
    double a0 = 0.0;
    double a1 = 0.0;
    REQUIRE(result->segment_arc(1, &centre, &radius, &a0, &a1));
    CHECK(near_equal(centre, Vec3{10, 10, 0}, 1e-6));
    CHECK_NEAR(radius, 10.0, 1e-6);
}

TEST_CASE("pedit: a reversed arc joins with the opposite bulge") {
    // Same arc, but the polyline meets its END rather than its start, so the
    // chain is reversed and the sweep is travelled the other way.
    Database db;
    auto base = std::make_unique<Polyline>();
    base->add({0, 10, 0});
    base->add({20, 10, 0});  // the arc's END point, so the chain must reverse
    const Handle h = db.add(std::move(base));

    auto arc = std::make_unique<Arc>(Vec3{10, 10, 0}, 10.0, -kPi / 2.0, 0.0);
    const Handle ah = db.add(std::move(arc));

    CommandEngine engine(db);
    engine.begin(make_command("PEDIT"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_keyword("JOIN"));
    engine.supply(InputValue::of_entity(ah));
    engine.supply(InputValue::none());
    engine.supply(InputValue::none());

    const Polyline* result = poly(db, h);
    REQUIRE(result != nullptr);
    REQUIRE(result->size() == 3);
    // Travelled backwards, so the polyline now ends at the arc's start point.
    CHECK(near_equal(result->vertices()[2].pos, Vec3{10, 0, 0}, 1e-9));
    // Reversed, so the bulge is the negative of the forward case.
    CHECK_NEAR(result->vertices()[1].bulge, -std::tan(kPi * 0.125), 1e-9);

    Vec3 centre{};
    double radius = 0.0;
    double a0 = 0.0;
    double a1 = 0.0;
    REQUIRE(result->segment_arc(1, &centre, &radius, &a0, &a1));
    CHECK(near_equal(centre, Vec3{10, 10, 0}, 1e-6));
}

TEST_CASE("pedit: two polylines join into one") {
    Database db;
    const Handle h = add_base(db);
    auto other = std::make_unique<Polyline>();
    other->add({10, 0, 0});
    other->add({10, 5, 0});
    other->add({15, 5, 0});
    const Handle oh = db.add(std::move(other));

    CommandEngine engine(db);
    engine.begin(make_command("PEDIT"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_keyword("JOIN"));
    engine.supply(InputValue::of_entity(oh));
    engine.supply(InputValue::none());
    engine.supply(InputValue::none());

    const Polyline* result = poly(db, h);
    REQUIRE(result != nullptr);
    // Four vertices, not five: the shared point is not duplicated.
    REQUIRE(result->size() == 4);
    CHECK(near_equal(result->vertices()[3].pos, Vec3{15, 5, 0}, 1e-12));
    CHECK(db.size() == 1);
}

TEST_CASE("pedit: a closed polyline has no ends to join to") {
    Database db;
    auto p = std::make_unique<Polyline>();
    p->add({0, 0, 0});
    p->add({10, 0, 0});
    p->add({10, 10, 0});
    p->set_closed(true);
    const Handle h = db.add(std::move(p));
    db.add(std::make_unique<Line>(Vec3{10, 10, 0}, Vec3{20, 20, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("PEDIT"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_keyword("JOIN"));
    engine.supply(InputValue::of_keyword("ALL"));
    const EngineStatus status = engine.supply(InputValue::none());
    CHECK(status == EngineStatus::Failed);
}

// --- PEDIT's own Undo -------------------------------------------------------

TEST_CASE("pedit: Undo steps back one edit without leaving the command") {
    Database db;
    auto p = std::make_unique<Polyline>();
    p->add({0, 0, 0});
    p->add({10, 0, 0});
    p->add({10, 10, 0});
    const Handle h = db.add(std::move(p));

    CommandEngine engine(db);
    engine.begin(make_command("PEDIT"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_keyword("WIDTH"));
    engine.supply(InputValue::of_real(2.0));
    CHECK(poly(db, h)->has_width());

    engine.supply(InputValue::of_keyword("UNDO"));
    CHECK(!poly(db, h)->has_width());
    // Still inside PEDIT, still able to take another option.
    CHECK(engine.active());

    engine.supply(InputValue::none());
}

TEST_CASE("pedit: Undo with nothing done fails rather than corrupting anything") {
    Database db;
    const Handle h = add_base(db);

    CommandEngine engine(db);
    engine.begin(make_command("PEDIT"));
    engine.supply(InputValue::of_entity(h));
    const EngineStatus status = engine.supply(InputValue::of_keyword("UNDO"));
    CHECK(status == EngineStatus::Failed);
    CHECK(poly(db, h)->size() == 2);
}

TEST_CASE("pedit: the whole session is one step of the drawing's undo") {
    Database db;
    auto p = std::make_unique<Polyline>();
    p->add({0, 0, 0});
    p->add({10, 0, 0});
    p->add({10, 10, 0});
    const Handle h = db.add(std::move(p));

    CommandEngine engine(db);
    engine.begin(make_command("PEDIT"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_keyword("WIDTH"));
    engine.supply(InputValue::of_real(2.0));
    engine.supply(InputValue::of_keyword("CLOSE"));
    engine.supply(InputValue::none());  // eXit

    REQUIRE(poly(db, h) != nullptr);
    CHECK(poly(db, h)->closed());
    CHECK(poly(db, h)->has_width());

    // One UNDO undoes the PEDIT, not just its last option.
    engine.begin(make_command("UNDO"));
    CHECK(!poly(db, h)->closed());
    CHECK(!poly(db, h)->has_width());
}
