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

// Selects by crossing box, then answers base and displacement.
void stretch_by_crossing(CommandEngine& engine, Vec3 c0, Vec3 c1, Vec3 delta) {
    engine.begin(make_command("STRETCH"));
    engine.supply(InputValue::of_keyword("CROSSING"));
    engine.supply(InputValue::of_point(c0));
    engine.supply(InputValue::of_point(c1));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point(delta));
}

}  // namespace

TEST_CASE("stretch: only the endpoint inside the window moves") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));

    // A box around the far end only.
    stretch_by_crossing(engine, {90, -5, 0}, {110, 5, 0}, {0, 20, 0});
    CHECK(engine.status() == EngineStatus::Finished);

    CHECK_VEC(line_at(db, 0)->start(), 0.0, 0.0, 0.0, 1e-12);    // untouched
    CHECK_VEC(line_at(db, 0)->end(), 100.0, 20.0, 0.0, 1e-12);   // moved
}

TEST_CASE("stretch: a window catching both ends moves the whole line") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));

    stretch_by_crossing(engine, {-5, -5, 0}, {105, 5, 0}, {0, 20, 0});

    CHECK_VEC(line_at(db, 0)->start(), 0.0, 20.0, 0.0, 1e-12);
    CHECK_VEC(line_at(db, 0)->end(), 100.0, 20.0, 0.0, 1e-12);
    // Length preserved: it moved rather than deformed.
    CHECK_NEAR(line_at(db, 0)->length(), 100.0, 1e-12);
}

TEST_CASE("stretch: a line merely crossed in the middle does not move") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));

    // A tall thin box over the middle: the line is selected, but neither
    // endpoint is inside, so nothing of it moves.
    stretch_by_crossing(engine, {45, -50, 0}, {55, 50, 0}, {0, 20, 0});

    CHECK_VEC(line_at(db, 0)->start(), 0.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(line_at(db, 0)->end(), 100.0, 0.0, 0.0, 1e-12);
    CHECK(engine.message() == "0 stretched");
}

TEST_CASE("stretch: the classic case, a rectangle pulled wider") {
    Database db;
    CommandEngine engine(db);
    // Three sides of a box: the right-hand edge and the two horizontals.
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));    // bottom
    db.add(std::make_unique<Line>(Vec3{10, 0, 0}, Vec3{10, 10, 0}));  // right
    db.add(std::make_unique<Line>(Vec3{0, 10, 0}, Vec3{10, 10, 0}));  // top

    // A crossing box over the whole right-hand edge and the right ends of the
    // horizontals. This is what STRETCH is for.
    stretch_by_crossing(engine, {5, -5, 0}, {15, 15, 0}, {5, 0, 0});

    CHECK_VEC(line_at(db, 0)->start(), 0.0, 0.0, 0.0, 1e-12);   // left ends stay
    CHECK_VEC(line_at(db, 0)->end(), 15.0, 0.0, 0.0, 1e-12);    // right ends move
    CHECK_VEC(line_at(db, 1)->start(), 15.0, 0.0, 0.0, 1e-12);  // edge moves whole
    CHECK_VEC(line_at(db, 1)->end(), 15.0, 10.0, 0.0, 1e-12);
    CHECK_VEC(line_at(db, 2)->start(), 0.0, 10.0, 0.0, 1e-12);
    CHECK_VEC(line_at(db, 2)->end(), 15.0, 10.0, 0.0, 1e-12);
}

TEST_CASE("stretch: a circle moves when its centre is inside") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 10.0));

    // The box has to do two things at once: cross the rim, or the circle is
    // never selected at all, and contain the centre, or nothing of it moves. A
    // box floating inside the circle touches no drawn geometry and selects
    // nothing -- the same reason a circle is not picked at its centre.
    stretch_by_crossing(engine, {-12, -2, 0}, {2, 2, 0}, {5, 5, 0});

    const Circle* c = static_cast<const Circle*>(db.get(db.order()[0]));
    CHECK_VEC(c->center(), 5.0, 5.0, 0.0, 1e-12);
    // A circle has no stretchable geometry: it moved, it did not deform.
    CHECK_NEAR(c->radius(), 10.0, 1e-12);
}

TEST_CASE("stretch: a box inside a circle selects nothing at all") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 10.0));

    // Not via the helper: the command finishes at the selection prompt, so
    // supplying a base point afterwards would be talking to a finished engine.
    engine.begin(make_command("STRETCH"));
    engine.supply(InputValue::of_keyword("CROSSING"));
    engine.supply(InputValue::of_point({-2, -2, 0}));
    engine.supply(InputValue::of_point({2, 2, 0}));
    engine.supply(InputValue::none());
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(engine.message() == "Nothing selected");

    const Circle* c = static_cast<const Circle*>(db.get(db.order()[0]));
    CHECK_VEC(c->center(), 0.0, 0.0, 0.0, 1e-12);
}

TEST_CASE("stretch: a circle crossed at the rim only is left alone") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 10.0));

    // Over the rim, well away from the centre. A quadrant grip is inside, but
    // STRETCH must not resize a circle.
    stretch_by_crossing(engine, {8, -2, 0}, {12, 2, 0}, {5, 5, 0});

    const Circle* c = static_cast<const Circle*>(db.get(db.order()[0]));
    CHECK_VEC(c->center(), 0.0, 0.0, 0.0, 1e-12);
    CHECK_NEAR(c->radius(), 10.0, 1e-12);
}

TEST_CASE("stretch: without a crossing window it degenerates into a move") {
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));

    // Picked individually: every defining point is "inside" the selection, so
    // R12 moves the whole thing. We do too -- but say so, which is the whole
    // difference between the command looking broken and being understood.
    engine.begin(make_command("STRETCH"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({0, 20, 0}));

    CHECK_VEC(line_at(db, 0)->start(), 0.0, 20.0, 0.0, 1e-12);
    CHECK_VEC(line_at(db, 0)->end(), 100.0, 20.0, 0.0, 1e-12);
    CHECK(engine.message().find("no crossing window") != std::string::npos);
}

TEST_CASE("stretch: a window selection also degenerates, and says so") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    engine.begin(make_command("STRETCH"));
    engine.supply(InputValue::of_keyword("WINDOW"));
    engine.supply(InputValue::of_point({-5, -5, 0}));
    engine.supply(InputValue::of_point({15, 5, 0}));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({0, 7, 0}));

    // A Window box is not a stretch region, so it is not kept as one.
    CHECK(engine.message().find("no crossing window") != std::string::npos);
    CHECK_VEC(line_at(db, 0)->start(), 0.0, 7.0, 0.0, 1e-12);
}

TEST_CASE("stretch: Enter after the base point means <displacement>") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));

    engine.begin(make_command("STRETCH"));
    engine.supply(InputValue::of_keyword("CROSSING"));
    engine.supply(InputValue::of_point({90, -5, 0}));
    engine.supply(InputValue::of_point({110, 5, 0}));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 30, 0}));  // the vector itself
    engine.supply(InputValue::none());

    CHECK_VEC(line_at(db, 0)->end(), 100.0, 30.0, 0.0, 1e-12);
}

TEST_CASE("stretch: it is one undo step") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 10, 0}, Vec3{100, 10, 0}));

    stretch_by_crossing(engine, {90, -5, 0}, {110, 15, 0}, {0, 5, 0});
    CHECK_VEC(line_at(db, 0)->end(), 100.0, 5.0, 0.0, 1e-12);

    CHECK(db.journal().undo(db));
    CHECK_VEC(line_at(db, 0)->end(), 100.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(line_at(db, 1)->end(), 100.0, 10.0, 0.0, 1e-12);
}

TEST_CASE("stretch: S is the alias") {
    CHECK(resolve_command_name("S").name == "STRETCH");
}
