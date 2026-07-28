// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/commands.hpp"
#include "noto/database.hpp"
#include "noto/entities.hpp"

#include <memory>

using namespace noto;

namespace {

const Line* line_at(const Database& db, std::size_t i) {
    return static_cast<const Line*>(db.get(db.order()[i]));
}

}  // namespace

TEST_CASE("move: select, base point, second point") {
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    engine.begin(make_command("MOVE"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());          // done selecting
    engine.supply(InputValue::of_point({0, 0, 0}));   // base
    engine.supply(InputValue::of_point({3, 4, 0}));   // second
    CHECK(engine.status() == EngineStatus::Finished);

    CHECK(db.size() == 1);
    CHECK_VEC(line_at(db, 0)->start(), 3.0, 4.0, 0.0, 1e-12);
    CHECK_VEC(line_at(db, 0)->end(), 13.0, 4.0, 0.0, 1e-12);

    // The handle survives: AutoLISP may be holding it.
    CHECK(db.get(h) != nullptr);
}

TEST_CASE("move: Enter at the second point means the first was the displacement") {
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    engine.begin(make_command("MOVE"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({5, 5, 0}));  // the vector itself
    engine.supply(InputValue::none());               // R12's <displacement>

    CHECK_VEC(line_at(db, 0)->start(), 5.0, 5.0, 0.0, 1e-12);
}

TEST_CASE("move: the whole selection moves together") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 5, 0}, Vec3{1, 5, 0}));

    engine.begin(make_command("MOVE"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({0, 0, 2}));

    CHECK(db.size() == 2);
    CHECK_VEC(line_at(db, 0)->start(), 0.0, 0.0, 2.0, 1e-12);
    CHECK_VEC(line_at(db, 1)->start(), 0.0, 5.0, 2.0, 1e-12);
}

TEST_CASE("copy: the original stays and a new entity appears") {
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    engine.begin(make_command("COPY"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({0, 7, 0}));

    CHECK(db.size() == 2);
    CHECK_VEC(line_at(db, 0)->start(), 0.0, 0.0, 0.0, 1e-12);  // original untouched
    CHECK_VEC(line_at(db, 1)->start(), 0.0, 7.0, 0.0, 1e-12);

    // The copy is a new entity with its own handle.
    CHECK(db.order()[1] != h);
}

TEST_CASE("copy: copying a whole selection does not copy the copies") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 5, 0}, Vec3{1, 5, 0}));

    engine.begin(make_command("COPY"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));

    // Two in, two out -- not four, which is what iterating the live database
    // while adding to it would give.
    CHECK(db.size() == 4);
}

TEST_CASE("move: a move is one undo step") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 5, 0}, Vec3{1, 5, 0}));

    engine.begin(make_command("MOVE"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({100, 0, 0}));
    CHECK_VEC(line_at(db, 0)->start(), 100.0, 0.0, 0.0, 1e-12);

    CHECK(db.journal().undo(db));
    CHECK_VEC(line_at(db, 0)->start(), 0.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(line_at(db, 1)->start(), 0.0, 5.0, 0.0, 1e-12);
}

TEST_CASE("copy: a copy is one undo step") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));

    engine.begin(make_command("COPY"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    CHECK(db.size() == 2);

    CHECK(db.journal().undo(db));
    CHECK(db.size() == 1);
}

TEST_CASE("move: selecting nothing is a no-op, not an error") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("MOVE"));
    engine.supply(InputValue::none());
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(engine.message() == "Nothing selected");
}

TEST_CASE("move: MOVE Previous reuses the last selection") {
    Database db;
    CommandEngine engine(db);
    const Handle a = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 5, 0}, Vec3{1, 5, 0}));

    // Build a selection with ERASE and escape out of it.
    engine.begin(make_command("ERASE"));
    engine.supply(InputValue::of_entity(a));
    engine.supply(InputValue::cancel());

    // The point of Previous: select once, act more than once.
    engine.begin(make_command("MOVE"));
    engine.supply(InputValue::of_keyword("PREVIOUS"));
    CHECK(engine.selection().size() == 1);
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({0, 0, 9}));

    CHECK_VEC(line_at(db, 0)->start(), 0.0, 0.0, 9.0, 1e-12);
    CHECK_VEC(line_at(db, 1)->start(), 0.0, 5.0, 0.0, 1e-12);  // untouched
}

TEST_CASE("move: M and CP are the aliases") {
    CHECK(resolve_command_name("M").name == "MOVE");
    CHECK(resolve_command_name("CP").name == "COPY");
}

TEST_CASE("move: a crossing box selects and then moves") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 100, 0}, Vec3{10, 100, 0}));

    engine.begin(make_command("MOVE"));
    engine.supply(InputValue::of_keyword("CROSSING"));
    engine.supply(InputValue::of_point({4, -5, 0}));
    engine.supply(InputValue::of_point({6, 5, 0}));
    CHECK(engine.selection().size() == 1);
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({0, -20, 0}));

    CHECK_VEC(line_at(db, 0)->start(), 0.0, -20.0, 0.0, 1e-12);
    CHECK_VEC(line_at(db, 1)->start(), 0.0, 100.0, 0.0, 1e-12);
}

TEST_CASE("copy: Multiple places one at each point until Enter") {
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));

    engine.begin(make_command("COPY"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_keyword("MULTIPLE"));
    engine.supply(InputValue::of_point({0, 0, 0}));  // base

    engine.supply(InputValue::of_point({10, 0, 0}));
    CHECK(db.size() == 2);
    CHECK(engine.active());  // still asking

    engine.supply(InputValue::of_point({20, 0, 0}));
    engine.supply(InputValue::of_point({30, 0, 0}));
    CHECK(db.size() == 4);

    engine.supply(InputValue::none());
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(engine.message() == "3 copied");
}

TEST_CASE("copy: Multiple measures every copy from the same base") {
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));

    engine.begin(make_command("COPY"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_keyword("MULTIPLE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_point({20, 0, 0}));
    engine.supply(InputValue::none());

    // Copies fan out from the base rather than chaining off each other: the
    // second lands at 20, not at 30.
    CHECK_VEC(line_at(db, 1)->start(), 10.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(line_at(db, 2)->start(), 20.0, 0.0, 0.0, 1e-12);
}

TEST_CASE("copy: Multiple is the whole command, so one UNDO takes them all") {
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));

    engine.begin(make_command("COPY"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_keyword("MULTIPLE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_point({20, 0, 0}));
    engine.supply(InputValue::none());
    CHECK(db.size() == 3);

    CHECK(db.journal().undo(db));
    CHECK(db.size() == 1);
}

TEST_CASE("copy: Enter straight after the base still means <displacement>") {
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));

    // Without Multiple, Enter keeps its R12 meaning rather than ending early.
    engine.begin(make_command("COPY"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({5, 5, 0}));
    engine.supply(InputValue::none());
    CHECK(db.size() == 2);
    CHECK_VEC(line_at(db, 1)->start(), 5.0, 5.0, 0.0, 1e-12);
}

TEST_CASE("move: MOVE offers no Multiple") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));

    engine.begin(make_command("MOVE"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    // Moving something to several places at once is not a thing.
    CHECK(engine.prompt().keywords.empty());
}
