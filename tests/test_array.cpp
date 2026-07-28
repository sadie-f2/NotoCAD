// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/commands.hpp"
#include "noto/database.hpp"
#include "noto/entities.hpp"

#include <memory>

using namespace noto;

namespace {

// One unit circle at the origin, selected, sitting at the R/P prompt.
void setup(Database& db, CommandEngine& engine, const Vec3& at = {0, 0, 0}) {
    db.add(std::make_unique<Circle>(at, 1.0));
    engine.begin(make_command("ARRAY"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
}

bool has_circle_at(const Database& db, const Vec3& p) {
    for (const Handle h : db.order()) {
        const Entity* e = db.get(h);
        if (e && e->type() == EntityType::Circle &&
            near_equal(static_cast<const Circle*>(e)->center(), p, 1e-9)) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("array: rectangular, rows by columns") {
    Database db;
    CommandEngine engine(db);
    setup(db, engine);

    engine.supply(InputValue::none());         // Enter = Rectangular, R12's default
    engine.supply(InputValue::of_integer(2));  // rows
    engine.supply(InputValue::of_integer(3));  // columns
    engine.supply(InputValue::of_real(10.0));  // row spacing
    engine.supply(InputValue::of_real(5.0));   // column spacing
    CHECK(engine.status() == EngineStatus::Finished);

    // Six positions in total, the original being one of them.
    CHECK(db.size() == 6);
    CHECK(has_circle_at(db, {0, 0, 0}));
    CHECK(has_circle_at(db, {5, 0, 0}));
    CHECK(has_circle_at(db, {10, 0, 0}));
    CHECK(has_circle_at(db, {0, 10, 0}));
    CHECK(has_circle_at(db, {5, 10, 0}));
    CHECK(has_circle_at(db, {10, 10, 0}));
}

TEST_CASE("array: a negative spacing arrays the other way") {
    Database db;
    CommandEngine engine(db);
    setup(db, engine);

    engine.supply(InputValue::of_keyword("RECTANGULAR"));
    engine.supply(InputValue::of_integer(2));
    engine.supply(InputValue::of_integer(1));
    engine.supply(InputValue::of_real(-10.0));  // downward

    CHECK(db.size() == 2);
    CHECK(has_circle_at(db, {0, -10, 0}));
}

TEST_CASE("array: a single row skips the row spacing prompt") {
    Database db;
    CommandEngine engine(db);
    setup(db, engine);

    engine.supply(InputValue::none());
    engine.supply(InputValue::of_integer(1));  // one row
    engine.supply(InputValue::of_integer(3));
    // R12 does not ask for a distance it cannot use, so the next answer is the
    // column spacing.
    CHECK(engine.prompt().message.find("columns") != std::string::npos);
    engine.supply(InputValue::of_real(4.0));

    CHECK(db.size() == 3);
    CHECK(has_circle_at(db, {8, 0, 0}));
}

TEST_CASE("array: one by one adds nothing") {
    Database db;
    CommandEngine engine(db);
    setup(db, engine);

    engine.supply(InputValue::none());
    engine.supply(InputValue::of_integer(1));
    engine.supply(InputValue::of_integer(1));
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(db.size() == 1);
}

TEST_CASE("array: a zero or negative count is refused") {
    Database db;
    CommandEngine engine(db);
    setup(db, engine);
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_integer(0));
    CHECK(engine.status() == EngineStatus::Failed);
}

TEST_CASE("array: polar, filling a full circle") {
    Database db;
    CommandEngine engine(db);
    setup(db, engine, {10, 0, 0});

    engine.supply(InputValue::of_keyword("POLAR"));
    engine.supply(InputValue::of_point({0, 0, 0}));  // centre
    engine.supply(InputValue::of_integer(4));        // items
    engine.supply(InputValue::none());               // Enter = 360 degrees
    engine.supply(InputValue::none());               // Enter = rotate, R12's default

    // Four items round a circle of radius 10, ninety degrees apart. A full fill
    // divides by the count so the last does not land on the first.
    CHECK(db.size() == 4);
    CHECK(has_circle_at(db, {10, 0, 0}));
    CHECK(has_circle_at(db, {0, 10, 0}));
    CHECK(has_circle_at(db, {-10, 0, 0}));
    CHECK(has_circle_at(db, {0, -10, 0}));
}

TEST_CASE("array: polar, filling part of a circle") {
    Database db;
    CommandEngine engine(db);
    setup(db, engine, {10, 0, 0});

    engine.supply(InputValue::of_keyword("POLAR"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_integer(3));
    engine.supply(InputValue::of_real(90.0));  // a quarter turn
    engine.supply(InputValue::none());

    // A partial fill puts the first and last on the ends of the arc, so three
    // items across ninety degrees are forty-five apart.
    CHECK(db.size() == 3);
    CHECK(has_circle_at(db, {10, 0, 0}));
    CHECK(has_circle_at(db, {0, 10, 0}));
    const double s = 10.0 / std::sqrt(2.0);
    CHECK(has_circle_at(db, {s, s, 0}));
}

TEST_CASE("array: a negative fill goes clockwise") {
    Database db;
    CommandEngine engine(db);
    setup(db, engine, {10, 0, 0});

    engine.supply(InputValue::of_keyword("POLAR"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_integer(2));
    engine.supply(InputValue::of_real(-90.0));
    engine.supply(InputValue::none());

    CHECK(has_circle_at(db, {0, -10, 0}));
}

TEST_CASE("array: polar without rotation keeps orientation") {
    Database db;
    CommandEngine engine(db);
    // A line, so orientation is visible. It runs along +X at x=10.
    db.add(std::make_unique<Line>(Vec3{10, 0, 0}, Vec3{12, 0, 0}));
    engine.begin(make_command("ARRAY"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_keyword("POLAR"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_integer(4));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_keyword("NO"));

    CHECK(db.size() == 4);
    // Every copy still runs along +X, however far round the circle it sits.
    for (const Handle h : db.order()) {
        const Line* l = static_cast<const Line*>(db.get(h));
        const Vec3 d = l->direction();
        CHECK_NEAR(d.y, 0.0, 1e-9);
        CHECK_NEAR(d.x, 2.0, 1e-9);
    }
}

TEST_CASE("array: polar with rotation turns the items") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{10, 0, 0}, Vec3{12, 0, 0}));
    engine.begin(make_command("ARRAY"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_keyword("POLAR"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_integer(4));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_keyword("YES"));

    // The quarter-turn copy points along +Y instead.
    const Line* second = static_cast<const Line*>(db.get(db.order()[1]));
    CHECK_NEAR(second->direction().x, 0.0, 1e-9);
    CHECK_NEAR(second->direction().y, 2.0, 1e-9);
}

TEST_CASE("array: a zero fill angle is refused") {
    Database db;
    CommandEngine engine(db);
    setup(db, engine, {10, 0, 0});
    engine.supply(InputValue::of_keyword("POLAR"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_integer(4));
    engine.supply(InputValue::of_real(0.0));
    CHECK(engine.status() == EngineStatus::Failed);
}

TEST_CASE("array: the whole array is one undo step") {
    Database db;
    CommandEngine engine(db);
    setup(db, engine);
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_integer(3));
    engine.supply(InputValue::of_integer(3));
    engine.supply(InputValue::of_real(2.0));
    engine.supply(InputValue::of_real(2.0));
    CHECK(db.size() == 9);

    CHECK(db.journal().undo(db));
    CHECK(db.size() == 1);
}

TEST_CASE("array: AR is the alias") {
    CHECK(resolve_command_name("AR").name == "ARRAY");
}
