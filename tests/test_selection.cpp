// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/entities.hpp"
#include "ncad/selection.hpp"
#include "ncad/view_control.hpp"

#include <memory>

using namespace ncad;

namespace {

Handle add_line(Database& db, double y) {
    return db.add(std::make_unique<Line>(Vec3{0, y, 0}, Vec3{10, y, 0}));
}

void select_all_and_erase(CommandEngine& engine) {
    engine.begin(make_command("ERASE"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
}

// A view looking down world X, so the screen's right is world Y and its up is
// world Z. Chosen because it shares no axis with world XY: a selection box
// built on the wrong frame then comes out with zero width rather than merely
// the wrong size, which is a difference no tolerance can hide.
class SideView final : public ViewControl {
public:
    void set_plan_view(const Vec3&) override {}
    void zoom_extents() override {}
    void zoom_window(const Vec3&, const Vec3&) override {}
    void zoom_scale(double) override {}
    bool zoom_previous() override { return false; }
    void pan(const Vec3&, const Vec3&) override {}
    void set_view_direction(const Vec3&) override {}
    Vec3 view_direction() const override { return Vec3{1, 0, 0}; }
    Basis view_basis() const override { return Basis{{0, 1, 0}, {0, 0, 1}, {1, 0, 0}}; }
    DrawContext draw_context() const override { return DrawContext{}; }
};

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
    db.set_layer_frozen(hidden, true);

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

namespace {

// A drawing of three separated horizontal lines, at y = 0, 10 and 20, each
// running from x=0 to x=10.
struct Grid {
    Database db;
    CommandEngine engine{db};
    Handle low, mid, high;
    Grid() {
        low = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));
        mid = db.add(std::make_unique<Line>(Vec3{0, 10, 0}, Vec3{10, 10, 0}));
        high = db.add(std::make_unique<Line>(Vec3{0, 20, 0}, Vec3{10, 20, 0}));
    }
    // Drags a box from (x0,y0) to (x1,y1) at the selection prompt.
    void box(const char* mode, double x0, double y0, double x1, double y1) {
        engine.supply(InputValue::of_keyword(mode));
        engine.supply(InputValue::of_point({x0, y0, 0}));
        engine.supply(InputValue::of_point({x1, y1, 0}));
    }
};

}  // namespace

TEST_CASE("selection: WINDOW takes only what is wholly inside") {
    Grid g;
    g.engine.begin(make_command("ERASE"));

    // A box enclosing the y=0 and y=10 lines entirely, and missing y=20.
    g.box("WINDOW", -1, -1, 11, 11);
    CHECK(g.engine.selection().size() == 2);
    CHECK(g.engine.selection().contains(g.low));
    CHECK(g.engine.selection().contains(g.mid));
    CHECK(!g.engine.selection().contains(g.high));

    g.engine.selection().clear();
    // Now a box that only partly covers every line: nothing is wholly inside.
    g.box("WINDOW", 5, -1, 20, 25);
    CHECK(g.engine.selection().empty());
}

TEST_CASE("selection: CROSSING takes anything it touches") {
    Grid g;
    g.engine.begin(make_command("ERASE"));

    // The same partial box that WINDOW refused.
    g.box("CROSSING", 5, -1, 20, 25);
    CHECK(g.engine.selection().size() == 3);
}

TEST_CASE("selection: a crossing box catches a line it merely cuts") {
    Grid g;
    g.engine.begin(make_command("ERASE"));

    // A tall thin box crossing the middle of all three lines, containing no
    // endpoint of any of them.
    g.box("CROSSING", 4, -5, 6, 25);
    CHECK(g.engine.selection().size() == 3);

    // The same box as a window catches nothing, since nothing is wholly inside.
    g.engine.selection().clear();
    g.box("WINDOW", 4, -5, 6, 25);
    CHECK(g.engine.selection().empty());
}

TEST_CASE("selection: corner order does not matter") {
    Grid g;
    g.engine.begin(make_command("ERASE"));
    g.box("CROSSING", 6, 25, 4, -5);  // dragged the other way
    CHECK(g.engine.selection().size() == 3);
}

TEST_CASE("selection: a box that touches nothing selects nothing") {
    Grid g;
    g.engine.begin(make_command("ERASE"));
    g.box("CROSSING", 100, 100, 110, 110);
    CHECK(g.engine.selection().empty());

    // A degenerate drag is a zero-size box, which must select nothing rather
    // than everything.
    g.box("CROSSING", 5, 5, 5, 5);
    CHECK(g.engine.selection().empty());
}

TEST_CASE("selection: the corner sub-prompts are point prompts") {
    Grid g;
    g.engine.begin(make_command("ERASE"));
    CHECK(g.engine.prompt().kind == PromptKind::Entity);

    g.engine.supply(InputValue::of_keyword("CROSSING"));
    CHECK(g.engine.prompt().kind == PromptKind::Point);
    CHECK(g.engine.prompt().message.find("crossing") != std::string::npos);

    g.engine.supply(InputValue::of_point({0, 0, 0}));
    CHECK(g.engine.prompt().kind == PromptKind::Point);
    // The second corner rubber-bands from the first...
    CHECK(g.engine.prompt().has_base);
    CHECK_VEC(g.engine.prompt().base, 0.0, 0.0, 0.0, 1e-12);
    // ...as a box, not a line. The kind alone cannot say so, which is the
    // whole reason the field exists.
    CHECK(g.engine.prompt().rubber_band == RubberBand::Box);

    g.engine.supply(InputValue::of_point({11, 11, 0}));
    // ...and then it is back to selecting.
    CHECK(g.engine.prompt().kind == PromptKind::Entity);
}

TEST_CASE("selection: only a crossing box is kept as the stretch region") {
    Grid g;
    g.engine.begin(make_command("ERASE"));

    // A window selection is not a stretch region. Keeping it would let STRETCH
    // run on a window selection, where every point is inside and it silently
    // degenerates into MOVE.
    g.box("WINDOW", -1, -1, 11, 11);
    CHECK(!g.engine.selection().has_region());

    g.box("CROSSING", 4, -5, 6, 25);
    CHECK(g.engine.selection().has_region());
    CHECK(g.engine.selection().region().contains(Vec3{5, 10, 0}));
    CHECK(!g.engine.selection().region().contains(Vec3{0, 10, 0}));
}

TEST_CASE("selection: REMOVE works with a box too") {
    Grid g;
    g.engine.begin(make_command("ERASE"));
    g.engine.supply(InputValue::of_keyword("ALL"));
    CHECK(g.engine.selection().size() == 3);

    g.engine.supply(InputValue::of_keyword("REMOVE"));
    g.box("CROSSING", -1, -1, 11, 1);  // just the y=0 line
    CHECK(g.engine.selection().size() == 2);
    CHECK(!g.engine.selection().contains(g.low));
}

TEST_CASE("selection: a circle is crossed by a box over its rim only") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 10.0));

    engine.begin(make_command("ERASE"));
    // A box entirely inside the circle touches no drawn geometry, so a crossing
    // selection finds nothing -- same reasoning as picking a circle at its
    // centre.
    engine.supply(InputValue::of_keyword("CROSSING"));
    engine.supply(InputValue::of_point({-2, -2, 0}));
    engine.supply(InputValue::of_point({2, 2, 0}));
    CHECK(engine.selection().empty());

    // A box over the rim does.
    engine.supply(InputValue::of_keyword("CROSSING"));
    engine.supply(InputValue::of_point({8, -2, 0}));
    engine.supply(InputValue::of_point({12, 2, 0}));
    CHECK(engine.selection().size() == 1);

    // And one enclosing the whole circle satisfies WINDOW.
    engine.selection().clear();
    engine.supply(InputValue::of_keyword("WINDOW"));
    engine.supply(InputValue::of_point({-11, -11, 0}));
    engine.supply(InputValue::of_point({11, 11, 0}));
    CHECK(engine.selection().size() == 1);
}

TEST_CASE("selection: a LINE's next point rubber-bands as a line, not a box") {
    // The other half of the distinction: same PromptKind, same has_base, and
    // it must still draw what it always drew.
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    CHECK(engine.prompt().kind == PromptKind::Point);
    CHECK(engine.prompt().has_base);
    CHECK(engine.prompt().rubber_band == RubberBand::Line);
}

TEST_CASE("selection: a window is built in the view's frame, not the world's") {
    // The regression this pins: SelectionPrompter had setters for the view
    // axes that nothing ever called, so every region selection ran against
    // world XY. Invisible in plan view -- and wrong in every other.
    //
    // Seen down world X, the two corners below span 10 by 10 of screen. Read
    // against world XY they span nothing at all, because the whole box lies in
    // a plane of constant X.
    Database db;
    CommandEngine engine(db);
    SideView view;

    // Wholly inside the box as seen from the side. Its X is 5 rather than 0
    // because a region ignores depth: at X = 0 it would sit exactly in the
    // degenerate world-XY box too, and the test would pass either way.
    db.add(std::make_unique<Line>(Vec3{5, 2, 2}, Vec3{5, 8, 8}));

    engine.set_view_control(&view);
    engine.begin(make_command("ERASE"));
    engine.supply(InputValue::of_keyword("WINDOW"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({0, 10, 10}));
    CHECK(engine.selection().size() == 1);
}

TEST_CASE("selection: with no view at all, a window is world XY") {
    // `ncad` has no screen, and a typed window has no frame but the world's.
    // Same construction as above, and it finds nothing -- correctly, since the
    // box has no extent in world XY.
    Database db;
    CommandEngine engine(db);

    db.add(std::make_unique<Line>(Vec3{5, 2, 2}, Vec3{5, 8, 8}));

    CHECK(engine.view_control() == nullptr);
    engine.begin(make_command("ERASE"));
    engine.supply(InputValue::of_keyword("WINDOW"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({0, 10, 10}));
    // Those two corners span nothing in world XY, so the box has zero width
    // and takes nothing -- the safe way for a degenerate drag to fail.
    CHECK(engine.selection().empty());

    // And an ordinary world-XY window still works, which is what every other
    // test in this file relies on. Depth is dropped, so the line's Z does not
    // keep it out.
    engine.supply(InputValue::of_keyword("WINDOW"));
    engine.supply(InputValue::of_point({-1, -1, 0}));
    engine.supply(InputValue::of_point({11, 11, 0}));
    CHECK(engine.selection().size() == 1);
}
