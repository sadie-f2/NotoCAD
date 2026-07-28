// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/commands.hpp"
#include "noto/database.hpp"
#include "noto/entities.hpp"

#include <memory>

using namespace noto;

namespace {

bool says(const CommandEngine& e, const char* text) {
    return e.message().find(text) != std::string::npos;
}

}  // namespace

TEST_CASE("dist: distance, both angles, and the deltas") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("DIST"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({3, 4, 0}));
    CHECK(engine.status() == EngineStatus::Finished);

    CHECK(says(engine, "Distance = 5.0000"));
    CHECK(says(engine, "Delta X = 3.0000"));
    CHECK(says(engine, "Delta Y = 4.0000"));
    // 3-4-5 in the XY plane, flat.
    CHECK(says(engine, "Angle in X-Y Plane = 53.1301"));
    CHECK(says(engine, "Angle from X-Y Plane = 0.0000"));
}

TEST_CASE("dist: the angle out of plane is what catches a line that is not flat") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("DIST"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({1, 0, 1}));
    CHECK(says(engine, "Angle from X-Y Plane = 45.0000"));
}

TEST_CASE("id: reports a point") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("ID"));
    engine.supply(InputValue::of_point({1.5, -2.25, 3}));
    CHECK(says(engine, "X = 1.5000"));
    CHECK(says(engine, "Y = -2.2500"));
    CHECK(says(engine, "Z = 3.0000"));
}

TEST_CASE("area: a sequence of points, closed implicitly") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("AREA"));
    // A 10x10 square, given as four corners without repeating the first.
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_point({10, 10, 0}));
    engine.supply(InputValue::of_point({0, 10, 0}));
    engine.supply(InputValue::none());

    CHECK(says(engine, "Area = 100.0000"));
    CHECK(says(engine, "Perimeter = 40.0000"));
}

TEST_CASE("area: a triangle, and the winding does not matter") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("AREA"));
    // Clockwise, which the shoelace formula would report as negative unsigned.
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({0, 10, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::none());
    CHECK(says(engine, "Area = 50.0000"));
}

TEST_CASE("area: fewer than three points encloses nothing") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("AREA"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::none());
    CHECK(engine.status() == EngineStatus::Failed);
}

TEST_CASE("area: a circle by entity") {
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 5.0));

    engine.begin(make_command("AREA"));
    engine.supply(InputValue::of_keyword("ENTITY"));
    engine.supply(InputValue::of_entity(h));
    CHECK(says(engine, "Area = 78.5398"));
    CHECK(says(engine, "Circumference = 31.4159"));
}

TEST_CASE("area: a line encloses nothing, and says so") {
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    engine.begin(make_command("AREA"));
    engine.supply(InputValue::of_keyword("ENTITY"));
    engine.supply(InputValue::of_entity(h));
    // Zero would look like an answer. This is a refusal.
    CHECK(engine.status() == EngineStatus::Failed);
}

TEST_CASE("list: reports a line's geometry") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{3, 4, 0}));

    engine.begin(make_command("LIST"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());

    CHECK(says(engine, "LINE"));
    CHECK(says(engine, "Layer: 0"));
    CHECK(says(engine, "Length = 5.0000"));
}

TEST_CASE("list: reports a circle and an arc") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Circle>(Vec3{1, 2, 0}, 5.0));
    db.add(std::make_unique<Arc>(Vec3{0, 0, 0}, 10.0, 0.0, 1.5707963267948966));

    engine.begin(make_command("LIST"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());

    CHECK(says(engine, "CIRCLE"));
    CHECK(says(engine, "radius 5.0000"));
    CHECK(says(engine, "ARC"));
    CHECK(says(engine, "end angle 90.0000"));
}

TEST_CASE("list: selecting nothing finishes quietly") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LIST"));
    engine.supply(InputValue::none());
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(engine.message() == "Nothing selected");
}

TEST_CASE("inquiry: asking a question changes nothing, so there is nothing to undo") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{3, 4, 0}));
    const std::size_t before = db.journal().undo_depth();

    engine.begin(make_command("DIST"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({3, 4, 0}));

    engine.begin(make_command("LIST"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());

    // An undo entry that undoes nothing visible is worse than none.
    CHECK(db.journal().undo_depth() == before);
}

TEST_CASE("inquiry: the aliases") {
    CHECK(resolve_command_name("DI").name == "DIST");
    CHECK(resolve_command_name("AA").name == "AREA");
    CHECK(resolve_command_name("LI").name == "LIST");
    CHECK(resolve_command_name("ID").name == "ID");
}
