// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// In-flight geometry: what a command would do if you clicked now.
//
// The test that earns its keep here is the drift test. A preview is a second
// caller of the same derivation, and the way this feature rots everywhere it is
// built is that the two paths separate -- the ghost shows one thing, the commit
// does another, and nothing ever compares them because each is tested alone. So
// every command below is driven to its last prompt, asked what it would do, then
// told to do it, and the two are checked against each other.

#include "test.hpp"

#include "noto/commands.hpp"
#include "noto/database.hpp"
#include "noto/entities.hpp"
#include "noto/inflight.hpp"

#include <memory>
#include <vector>

using namespace noto;

namespace {

const Line* line_at(const Database& db, std::size_t i) {
    return static_cast<const Line*>(db.get(db.order()[i]));
}

const Line* as_line(const EntityPtr& e) { return static_cast<const Line*>(e.get()); }

// Select everything and answer the "Select objects" prompt.
void select_all(CommandEngine& engine, const char* command) {
    engine.begin(make_command(command));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
}

}  // namespace

TEST_CASE("inflight: MOVE shows what it will commit, and then commits it") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 5, 0}, Vec3{10, 5, 0}));

    select_all(engine, "MOVE");
    engine.supply(InputValue::of_point({0, 0, 0}));  // base point

    InFlight f;
    REQUIRE(engine.preview(InputValue::of_point({3, 4, 0}), f));
    REQUIRE(f.ghosts.size() == 2);

    // MOVE stands in for its originals, so they must be hidden while the ghosts
    // are drawn or the selection appears twice.
    CHECK(f.suppressed.size() == 2);

    const Vec3 g0 = as_line(f.ghosts[0])->start();
    const Vec3 g1 = as_line(f.ghosts[1])->end();
    CHECK_VEC(g0, 3.0, 4.0, 0.0, 1e-12);
    CHECK_VEC(g1, 13.0, 9.0, 0.0, 1e-12);

    // Nothing has happened to the drawing yet. This is the invariant the whole
    // construct rests on: a mouse-move must not reach the database.
    CHECK(db.size() == 2);
    CHECK_VEC(line_at(db, 0)->start(), 0.0, 0.0, 0.0, 1e-12);

    // Now commit the same value, and the drawing must agree with what was shown.
    engine.supply(InputValue::of_point({3, 4, 0}));
    CHECK(engine.status() == EngineStatus::Finished);
    REQUIRE(db.size() == 2);
    CHECK_VEC(line_at(db, 0)->start(), g0.x, g0.y, g0.z, 1e-12);
    CHECK_VEC(line_at(db, 1)->end(), g1.x, g1.y, g1.z, 1e-12);
}

TEST_CASE("inflight: COPY suppresses nothing, because the originals stay") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    select_all(engine, "COPY");
    engine.supply(InputValue::of_point({0, 0, 0}));

    InFlight f;
    REQUIRE(engine.preview(InputValue::of_point({0, 7, 0}), f));
    CHECK(f.ghosts.size() == 1);
    // The whole difference between MOVE and COPY, as far as the viewport is
    // concerned.
    CHECK(f.suppressed.empty());
    CHECK_VEC(as_line(f.ghosts[0])->start(), 0.0, 7.0, 0.0, 1e-12);

    engine.supply(InputValue::of_point({0, 7, 0}));
    REQUIRE(db.size() == 2);
    CHECK_VEC(line_at(db, 0)->start(), 0.0, 0.0, 0.0, 1e-12);  // original untouched
    CHECK_VEC(line_at(db, 1)->start(), 0.0, 7.0, 0.0, 1e-12);
}

TEST_CASE("inflight: nothing to show before the command has a base point") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    InFlight f;

    // Idle.
    CHECK(!engine.preview(InputValue::of_point({1, 1, 0}), f));

    // Selecting: the cursor is choosing what to act on, not where to put it.
    engine.begin(make_command("MOVE"));
    CHECK(!engine.preview(InputValue::of_point({1, 1, 0}), f));

    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());

    // At the base point prompt: still nothing, because there is no vector yet.
    CHECK(!engine.preview(InputValue::of_point({1, 1, 0}), f));

    engine.supply(InputValue::of_point({0, 0, 0}));
    CHECK(engine.preview(InputValue::of_point({1, 1, 0}), f));

    // And a value of the wrong shape answers nothing.
    InFlight g;
    CHECK(!engine.preview(InputValue::of_keyword("MULTIPLE"), g));
    CHECK(g.empty());
}

TEST_CASE("inflight: ROTATE previews the same matrix it commits") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    select_all(engine, "ROTATE");
    engine.supply(InputValue::of_point({0, 0, 0}));  // base point

    // A point answers an angle prompt as the direction to it: straight up is a
    // quarter turn.
    InFlight f;
    REQUIRE(engine.preview(InputValue::of_point({0, 1, 0}), f));
    REQUIRE(f.ghosts.size() == 1);
    const Vec3 tip = as_line(f.ghosts[0])->end();
    CHECK_VEC(tip, 0.0, 10.0, 0.0, 1e-9);
    CHECK(f.suppressed.size() == 1);  // rotation replaces in place

    engine.supply(InputValue::of_point({0, 1, 0}));
    CHECK_VEC(line_at(db, 0)->end(), tip.x, tip.y, tip.z, 1e-12);
}

TEST_CASE("inflight: MIRROR shows the copy alongside, since R12 keeps the original") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{1, 0, 0}, Vec3{4, 0, 0}));

    select_all(engine, "MIRROR");
    engine.supply(InputValue::of_point({0, 0, 0}));  // first point of the line

    // Mirror about the Y axis.
    InFlight f;
    REQUIRE(engine.preview(InputValue::of_point({0, 1, 0}), f));
    REQUIRE(f.ghosts.size() == 1);
    CHECK_VEC(as_line(f.ghosts[0])->start(), -1.0, 0.0, 0.0, 1e-9);
    // Whether the originals go is not asked until the next prompt, and Enter
    // there means No -- so nothing is suppressed.
    CHECK(f.suppressed.empty());
}

TEST_CASE("inflight: STRETCH previews the grip move, not a whole-entity transform") {
    Database db;
    CommandEngine engine(db);
    // Only the right-hand end will fall inside the crossing box.
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    engine.begin(make_command("STRETCH"));
    engine.supply(InputValue::of_keyword("CROSSING"));
    engine.supply(InputValue::of_point({8, -1, 0}));
    engine.supply(InputValue::of_point({12, 1, 0}));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 0, 0}));  // base point

    InFlight f;
    REQUIRE(engine.preview(InputValue::of_point({0, 3, 0}), f));
    REQUIRE(f.ghosts.size() == 1);

    // The caught end moved and the other did not -- which is the case that
    // cannot be expressed as a matrix, and the reason InFlight asks the command
    // for a result rather than for a transform.
    const Line* ghost = as_line(f.ghosts[0]);
    CHECK_VEC(ghost->start(), 0.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(ghost->end(), 10.0, 3.0, 0.0, 1e-12);

    const Vec3 a = ghost->start();
    const Vec3 b = ghost->end();
    engine.supply(InputValue::of_point({0, 3, 0}));
    CHECK_VEC(line_at(db, 0)->start(), a.x, a.y, a.z, 1e-12);
    CHECK_VEC(line_at(db, 0)->end(), b.x, b.y, b.z, 1e-12);
}

TEST_CASE("inflight: previewing ROTATE3D does not record the Last axis") {
    // apply() writes the axis into CommandMemory for ROTATE3D's Last option. A
    // preview that did the same would make Last depend on where the mouse had
    // been -- exactly the class of side effect the tentative-value rule forbids.
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    select_all(engine, "ROTATE3D");
    engine.supply(InputValue::of_point({0, 0, 0}));  // two points define the axis
    engine.supply(InputValue::of_point({1, 0, 0}));

    InFlight f;
    REQUIRE(engine.preview(InputValue::of_point({0, 1, 0}), f));
    CHECK(f.ghosts.size() == 1);
    CHECK(!engine.memory().has_last_axis);

    engine.supply(InputValue::of_real(90.0));
    CHECK(engine.memory().has_last_axis);
}

TEST_CASE("inflight: repeated previews leave no trace on the drawing") {
    // Derived, never stored: the viewport rebuilds this on every paint, so a
    // thousand of them must be indistinguishable from none.
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    select_all(engine, "MOVE");
    engine.supply(InputValue::of_point({0, 0, 0}));

    for (int i = 0; i < 100; ++i) {
        InFlight f;
        engine.preview(InputValue::of_point({static_cast<double>(i), 1, 0}), f);
    }

    CHECK(db.size() == 1);
    CHECK_VEC(line_at(db, 0)->start(), 0.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(line_at(db, 0)->end(), 10.0, 0.0, 0.0, 1e-12);

    // And the command is still exactly where it was, so the next real answer
    // behaves as though none of it happened.
    CHECK(engine.status() == EngineStatus::Waiting);
    engine.supply(InputValue::of_point({0, 1, 0}));
    CHECK_VEC(line_at(db, 0)->start(), 0.0, 1.0, 0.0, 1e-12);
}

TEST_CASE("measuregeom: reports a distance and leaves the drawing alone") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("MEASUREGEOM"));
    engine.supply(InputValue::none());  // Distance is the default
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({3, 4, 0}));

    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(engine.message().find("Distance = 5.0000") != std::string::npos);
    // The whole point of the command: nothing to erase afterwards.
    CHECK(db.empty());
}

TEST_CASE("measuregeom: the ghost is a dimension, and it is only a ghost") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("MEASUREGEOM"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 0, 0}));

    InFlight f;
    REQUIRE(engine.preview(InputValue::of_point({10, 0, 0}), f));

    // Two extension lines, a dimension line, and four arrow barbs.
    CHECK(f.ghosts.size() == 7);
    CHECK(f.suppressed.empty());
    for (const EntityPtr& e : f.ghosts) CHECK(e->type() == EntityType::Line);

    // The dimension line is offset from what is being measured, and parallel to
    // it -- that is what makes it read as a dimension rather than as a chord.
    const Line* dim = static_cast<const Line*>(f.ghosts[2].get());
    CHECK_NEAR(dim->start().x, 0.0, 1e-9);
    CHECK_NEAR(dim->end().x, 10.0, 1e-9);
    CHECK_NEAR(dim->start().y, dim->end().y, 1e-12);
    CHECK(std::abs(dim->start().y) > 1e-6);

    CHECK(db.empty());

    // And committing the same point still leaves nothing behind.
    engine.supply(InputValue::of_point({10, 0, 0}));
    CHECK(db.empty());
}

TEST_CASE("measuregeom: nothing to show before the first point is given") {
    Database db;
    CommandEngine engine(db);

    InFlight f;
    engine.begin(make_command("MEASUREGEOM"));
    CHECK(!engine.preview(InputValue::of_point({1, 1, 0}), f));

    engine.supply(InputValue::none());
    CHECK(!engine.preview(InputValue::of_point({1, 1, 0}), f));

    engine.supply(InputValue::of_point({0, 0, 0}));
    CHECK(engine.preview(InputValue::of_point({1, 1, 0}), f));

    // A zero-length measurement has no direction to offset the ghost along, so
    // it shows nothing rather than a degenerate smear.
    InFlight g;
    CHECK(!engine.preview(InputValue::of_point({0, 0, 0}), g));
}

TEST_CASE("measuregeom: Radius reports an arc or circle and refuses anything else") {
    Database db;
    CommandEngine engine(db);
    const Handle c = db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 7.0));
    const Handle l = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));

    engine.begin(make_command("MEASUREGEOM"));
    engine.supply(InputValue::of_keyword("RADIUS"));
    engine.supply(InputValue::of_entity(c));
    CHECK(engine.message().find("Radius = 7.0000") != std::string::npos);
    CHECK(engine.message().find("Diameter = 14.0000") != std::string::npos);

    engine.begin(make_command("MEASUREGEOM"));
    engine.supply(InputValue::of_keyword("RADIUS"));
    engine.supply(InputValue::of_entity(l));
    CHECK(engine.status() == EngineStatus::Failed);

    CHECK(db.size() == 2);
}
