// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/entities.hpp"

#include <memory>

using namespace ncad;

namespace {


const Line* line_at(const Database& db, std::size_t i) {
    return static_cast<const Line*>(db.get(db.order()[i]));
}

// A unit line along +X from the origin, selected and ready for a base point.
Handle setup(Database& db, CommandEngine& engine, const char* cmd) {
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));
    engine.begin(make_command(cmd));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    return h;
}

}  // namespace

TEST_CASE("rotate: an angle in degrees, about the base point") {
    Database db;
    CommandEngine engine(db);
    const Handle h = setup(db, engine, "ROTATE");

    engine.supply(InputValue::of_point({0, 0, 0}));  // base
    engine.supply(InputValue::of_real(90.0));        // degrees, as R12 talks to users
    CHECK(engine.status() == EngineStatus::Finished);

    CHECK_VEC(line_at(db, 0)->start(), 0.0, 0.0, 0.0, 1e-9);
    CHECK_VEC(line_at(db, 0)->end(), 0.0, 10.0, 0.0, 1e-9);
    CHECK(db.get(h) != nullptr);  // rotated in place; the handle survives
}

TEST_CASE("rotate: about a base point that is not the origin") {
    Database db;
    CommandEngine engine(db);
    setup(db, engine, "ROTATE");

    engine.supply(InputValue::of_point({10, 0, 0}));  // the far end
    engine.supply(InputValue::of_real(180.0));

    CHECK_VEC(line_at(db, 0)->start(), 20.0, 0.0, 0.0, 1e-9);
    CHECK_VEC(line_at(db, 0)->end(), 10.0, 0.0, 0.0, 1e-9);
}

TEST_CASE("rotate: the angle can be shown instead of typed") {
    Database db;
    CommandEngine engine(db);
    setup(db, engine, "ROTATE");

    engine.supply(InputValue::of_point({0, 0, 0}));
    // Pointing up and to the left is 135 degrees from the base point.
    engine.supply(InputValue::of_point({-5, 5, 0}));

    const double s = 10.0 / std::sqrt(2.0);
    CHECK_VEC(line_at(db, 0)->end(), -s, s, 0.0, 1e-9);
}

TEST_CASE("rotate: acts about the construction plane normal, not the view") {
    Database db;
    CommandEngine engine(db);
    // A line out of plane. Rotating in the XY plane leaves z alone, which is
    // what "you can only draw in the current plane" means for the axis.
    db.add(std::make_unique<Line>(Vec3{0, 0, 7}, Vec3{10, 0, 7}));
    engine.begin(make_command("ROTATE"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(90.0));

    CHECK_VEC(line_at(db, 0)->end(), 0.0, 10.0, 7.0, 1e-9);
}

TEST_CASE("scale: a factor about the base point") {
    Database db;
    CommandEngine engine(db);
    setup(db, engine, "SCALE");

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(3.0));

    CHECK_VEC(line_at(db, 0)->start(), 0.0, 0.0, 0.0, 1e-9);
    CHECK_VEC(line_at(db, 0)->end(), 30.0, 0.0, 0.0, 1e-9);
}

TEST_CASE("scale: the base point stays put") {
    Database db;
    CommandEngine engine(db);
    setup(db, engine, "SCALE");

    engine.supply(InputValue::of_point({10, 0, 0}));  // the far end
    engine.supply(InputValue::of_real(0.5));

    CHECK_VEC(line_at(db, 0)->end(), 10.0, 0.0, 0.0, 1e-9);   // unmoved
    CHECK_VEC(line_at(db, 0)->start(), 5.0, 0.0, 0.0, 1e-9);
}

TEST_CASE("scale: a factor of zero or less is refused") {
    Database db;
    CommandEngine engine(db);
    setup(db, engine, "SCALE");
    engine.supply(InputValue::of_point({0, 0, 0}));

    // Collapsing everything to a point cannot be undone by scaling back up.
    engine.supply(InputValue::of_real(0.0));
    CHECK(engine.status() == EngineStatus::Failed);
}

TEST_CASE("scale: a circle scales its radius") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 5.0));
    engine.begin(make_command("SCALE"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(4.0));

    CHECK_NEAR(static_cast<const Circle*>(db.get(db.order()[0]))->radius(), 20.0, 1e-9);
}

TEST_CASE("mirror: across a line, keeping the original by default") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 5, 0}, Vec3{10, 5, 0}));
    engine.begin(make_command("MIRROR"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());

    // Mirror line along the X axis.
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({1, 0, 0}));
    engine.supply(InputValue::none());  // Enter: R12 defaults to keeping them

    CHECK(db.size() == 2);
    CHECK_VEC(line_at(db, 0)->start(), 0.0, 5.0, 0.0, 1e-9);   // original
    CHECK_VEC(line_at(db, 1)->start(), 0.0, -5.0, 0.0, 1e-9);  // reflection
}

TEST_CASE("mirror: Yes deletes the originals") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 5, 0}, Vec3{10, 5, 0}));
    engine.begin(make_command("MIRROR"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({1, 0, 0}));
    engine.supply(InputValue::of_keyword("YES"));

    CHECK(db.size() == 1);
    CHECK_VEC(line_at(db, 0)->start(), 0.0, -5.0, 0.0, 1e-9);
}

TEST_CASE("mirror: across a diagonal line") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{4, 0, 0}, Vec3{4, 1, 0}));
    engine.begin(make_command("MIRROR"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    // y = x, so mirroring swaps the coordinates.
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({1, 1, 0}));
    engine.supply(InputValue::of_keyword("YES"));

    CHECK_VEC(line_at(db, 0)->start(), 0.0, 4.0, 0.0, 1e-9);
    CHECK_VEC(line_at(db, 0)->end(), 1.0, 4.0, 0.0, 1e-9);
}

TEST_CASE("mirror: a degenerate mirror line is refused") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 5, 0}, Vec3{10, 5, 0}));
    engine.begin(make_command("MIRROR"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({3, 3, 0}));
    engine.supply(InputValue::of_point({3, 3, 0}));  // the same point
    CHECK(engine.status() == EngineStatus::Failed);
}

TEST_CASE("transform: each is one undo step") {
    Database db;
    CommandEngine engine(db);
    setup(db, engine, "ROTATE");
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(90.0));
    CHECK_VEC(line_at(db, 0)->end(), 0.0, 10.0, 0.0, 1e-9);

    CHECK(db.journal().undo(db));
    CHECK_VEC(line_at(db, 0)->end(), 10.0, 0.0, 0.0, 1e-9);
}

TEST_CASE("transform: the aliases") {
    CHECK(resolve_command_name("RO").name == "ROTATE");
    CHECK(resolve_command_name("SC").name == "SCALE");
    CHECK(resolve_command_name("MI").name == "MIRROR");
}

TEST_CASE("transform: selecting nothing finishes quietly") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("ROTATE"));
    engine.supply(InputValue::none());
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(engine.message() == "Nothing selected");
}
