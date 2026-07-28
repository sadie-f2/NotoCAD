// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/commands.hpp"
#include "noto/database.hpp"
#include "noto/entities.hpp"
#include "noto/selection.hpp"

#include <memory>

using namespace noto;

namespace {

Handle add_line(Database& db, double y) {
    return db.add(std::make_unique<Line>(Vec3{0, y, 0}, Vec3{10, y, 0}));
}

void select_all_and_erase(CommandEngine& engine) {
    engine.begin(make_command("ERASE"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
}

}  // namespace

TEST_CASE("selection: add dedupes and remove takes back out") {
    SelectionSet s;
    CHECK(s.empty());

    CHECK(s.add(7));
    CHECK(!s.add(7));  // already there
    CHECK(s.add(9));
    CHECK(s.size() == 2);
    CHECK(s.contains(7));

    CHECK(s.remove(7));
    CHECK(!s.remove(7));
    CHECK(!s.contains(7));
    CHECK(s.size() == 1);

    // A null handle is never a member.
    CHECK(!s.add(kNullHandle));

    s.clear();
    CHECK(s.empty());
}

TEST_CASE("selection: order is selection order, not drawing order") {
    SelectionSet s;
    s.add(5);
    s.add(1);
    s.add(3);
    CHECK(s.handles()[0] == 5);
    CHECK(s.handles()[1] == 1);
    CHECK(s.handles()[2] == 3);
}

TEST_CASE("selection: a region ignores depth") {
    SelectionRegion r;
    r.origin = Vec3{0, 0, 0};
    r.ax = Vec3{1, 0, 0};
    r.ay = Vec3{0, 1, 0};
    r.width = 10.0;
    r.height = 5.0;

    CHECK(r.contains(Vec3{5, 2, 0}));
    CHECK(r.contains(Vec3{0, 0, 0}));    // corners are inside
    CHECK(r.contains(Vec3{10, 5, 0}));
    CHECK(!r.contains(Vec3{11, 2, 0}));
    CHECK(!r.contains(Vec3{5, 6, 0}));

    // A crossing window is a screen-space question: it catches what lies under
    // it however far away, so the out-of-plane component is dropped.
    CHECK(r.contains(Vec3{5, 2, 1000}));
    CHECK(r.contains(Vec3{5, 2, -1000}));
}

TEST_CASE("selection: a region on a tilted view frame") {
    // Screen axes need not be world axes. The region is stored in the frame it
    // was dragged in, so it stays correct without a viewport to ask.
    SelectionRegion r;
    r.origin = Vec3{0, 0, 0};
    r.ax = normalize(Vec3{1, 1, 0});
    r.ay = Vec3{0, 0, 1};
    r.width = 10.0;
    r.height = 10.0;

    CHECK(r.contains(normalize(Vec3{1, 1, 0}) * 5.0 + Vec3{0, 0, 5}));
    CHECK(!r.contains(normalize(Vec3{1, 1, 0}) * 15.0));
    // Negative along the region's own axis, not merely a different world
    // direction: normalize(-1,1,0) projects to exactly the origin corner.
    CHECK(!r.contains(normalize(Vec3{-1, -1, 0}) * 5.0));
    CHECK(!r.contains(normalize(Vec3{1, 1, 0}) * 5.0 - Vec3{0, 0, 1}));
}

TEST_CASE("selection: ERASE uses the shared set") {
    Database db;
    CommandEngine engine(db);
    const Handle a = add_line(db, 0);
    const Handle b = add_line(db, 1);
    add_line(db, 2);

    engine.begin(make_command("ERASE"));
    engine.supply(InputValue::of_entity(a));
    engine.supply(InputValue::of_entity(b));
    CHECK(engine.selection().size() == 2);
    engine.supply(InputValue::none());

    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(db.size() == 1);
}

TEST_CASE("selection: picking the same entity twice selects it once") {
    Database db;
    CommandEngine engine(db);
    const Handle a = add_line(db, 0);

    engine.begin(make_command("ERASE"));
    engine.supply(InputValue::of_entity(a));
    engine.supply(InputValue::of_entity(a));
    CHECK(engine.selection().size() == 1);
}

TEST_CASE("selection: ALL takes everything visible") {
    Database db;
    CommandEngine engine(db);
    add_line(db, 0);
    add_line(db, 1);

    const LayerId hidden = db.add_layer("HIDDEN");
    auto l = std::make_unique<Line>(Vec3{0, 9, 0}, Vec3{1, 9, 0});
    l->props().layer = hidden;
    db.add(std::move(l));
    db.layer(hidden).frozen = true;

    engine.begin(make_command("ERASE"));
    engine.supply(InputValue::of_keyword("ALL"));
    // The frozen one is skipped: erasing what you cannot see is how a drawing
    // loses geometry nobody notices until later.
    CHECK(engine.selection().size() == 2);
    engine.supply(InputValue::none());
    CHECK(db.size() == 1);
}

TEST_CASE("selection: LAST is the newest entity") {
    Database db;
    CommandEngine engine(db);
    add_line(db, 0);
    const Handle newest = add_line(db, 1);

    engine.begin(make_command("ERASE"));
    engine.supply(InputValue::of_keyword("LAST"));
    CHECK(engine.selection().size() == 1);
    CHECK(engine.selection().contains(newest));
}

TEST_CASE("selection: REMOVE and ADD toggle the mode") {
    Database db;
    CommandEngine engine(db);
    const Handle a = add_line(db, 0);
    const Handle b = add_line(db, 1);

    engine.begin(make_command("ERASE"));
    engine.supply(InputValue::of_keyword("ALL"));
    CHECK(engine.selection().size() == 2);

    engine.supply(InputValue::of_keyword("REMOVE"));
    engine.supply(InputValue::of_entity(a));
    CHECK(engine.selection().size() == 1);
    CHECK(engine.selection().contains(b));

    engine.supply(InputValue::of_keyword("ADD"));
    engine.supply(InputValue::of_entity(a));
    CHECK(engine.selection().size() == 2);
}

TEST_CASE("selection: the prompt counts what is found") {
    Database db;
    CommandEngine engine(db);
    const Handle a = add_line(db, 0);

    engine.begin(make_command("ERASE"));
    CHECK(engine.prompt().text().find("found") == std::string::npos);
    engine.supply(InputValue::of_entity(a));
    CHECK(engine.prompt().text().find("1 found") != std::string::npos);

    engine.supply(InputValue::of_keyword("REMOVE"));
    CHECK(engine.prompt().text().find("Remove objects") != std::string::npos);
}

TEST_CASE("selection: PREVIOUS survives an intervening command") {
    Database db;
    CommandEngine engine(db);
    const Handle a = add_line(db, 0);
    const Handle b = add_line(db, 1);

    // Select two and escape, so the set was built but nothing was destroyed.
    // (Once MOVE exists this is the ordinary case: select, act, reuse.)
    engine.begin(make_command("ERASE"));
    engine.supply(InputValue::of_entity(a));
    engine.supply(InputValue::of_entity(b));
    engine.supply(InputValue::cancel());
    CHECK(db.size() == 2);

    // Something in between that selects nothing at all.
    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_point({0, 5, 0}));
    engine.supply(InputValue::of_point({9, 5, 0}));
    engine.supply(InputValue::none());

    // Previous still means what ERASE was given, not "nothing".
    engine.begin(make_command("ERASE"));
    engine.supply(InputValue::of_keyword("PREVIOUS"));
    CHECK(engine.selection().size() == 2);
    CHECK(engine.selection().contains(a));
    CHECK(engine.selection().contains(b));
}

TEST_CASE("selection: PREVIOUS skips entities that have since gone") {
    Database db;
    CommandEngine engine(db);
    const Handle a = add_line(db, 0);
    add_line(db, 1);

    select_all_and_erase(engine);
    CHECK(db.size() == 0);

    // Everything Previous names is gone, so it selects nothing rather than
    // handing out dangling handles.
    engine.begin(make_command("ERASE"));
    engine.supply(InputValue::of_keyword("PREVIOUS"));
    CHECK(engine.selection().empty());
    CHECK(!engine.selection().contains(a));
}

TEST_CASE("selection: a new command starts with an empty working set") {
    Database db;
    CommandEngine engine(db);
    add_line(db, 0);

    engine.begin(make_command("ERASE"));
    engine.supply(InputValue::of_keyword("ALL"));
    CHECK(!engine.selection().empty());

    engine.begin(make_command("ERASE"));
    CHECK(engine.selection().empty());
    // ...and what it held is now Previous.
    CHECK(engine.previous_selection().size() == 1);
}

TEST_CASE("selection: erasing is one undo step for the whole set") {
    Database db;
    CommandEngine engine(db);
    add_line(db, 0);
    add_line(db, 1);
    add_line(db, 2);

    select_all_and_erase(engine);
    CHECK(db.size() == 0);

    // Three entities went, one command ran, so one UNDO brings all three back.
    CHECK(db.journal().undo(db));
    CHECK(db.size() == 3);
}
