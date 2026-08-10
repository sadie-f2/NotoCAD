// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// OFFSET, FILLET and CHAMFER: the kernel and the commands.
//
// The corner cases are the point of the offset suite. A polyline's segments
// each move sideways on their own, and it is only where the moved segments
// MEET that the result is a parallel outline rather than a pile of pieces --
// so the tests that matter measure corners, not segments.

#include "test.hpp"

#include "ncad/commands.hpp"
#include "ncad/corner.hpp"
#include "ncad/database.hpp"
#include "ncad/entities.hpp"
#include "ncad/offset.hpp"

#include <cmath>
#include <memory>

using namespace ncad;

namespace {

const Polyline* as_pline(const Entity* e) {
    if (!e || e->type() != EntityType::Polyline) return nullptr;
    return static_cast<const Polyline*>(e);
}

}  // namespace

TEST_CASE("offset: a line moves sideways, on the side the pick names") {
    Line l(Vec3{0, 0, 0}, Vec3{10, 0, 0});

    EntityPtr up = offset_curve(l, 2.0, Vec3{5, 5, 0}, kWorldZ);
    REQUIRE(up != nullptr);
    const auto* lu = static_cast<const Line*>(up.get());
    CHECK_VEC(lu->start(), 0.0, 2.0, 0.0, 1e-12);
    CHECK_VEC(lu->end(), 10.0, 2.0, 0.0, 1e-12);

    // The other side is the same distance the other way, not a sign the caller
    // had to work out.
    EntityPtr down = offset_curve(l, 2.0, Vec3{5, -5, 0}, kWorldZ);
    REQUIRE(down != nullptr);
    CHECK_VEC(static_cast<const Line*>(down.get())->start(), 0.0, -2.0, 0.0, 1e-12);
}

TEST_CASE("offset: a line offsets in the plane it is given, not in world XY") {
    // A line along X, offset in the XZ plane: the result must move in Z.
    Line l(Vec3{0, 0, 0}, Vec3{10, 0, 0});
    EntityPtr out = offset_curve(l, 3.0, Vec3{5, 0, 9}, Vec3{0, -1, 0});
    REQUIRE(out != nullptr);
    const auto* lo = static_cast<const Line*>(out.get());
    CHECK_VEC(lo->start(), 0.0, 0.0, 3.0, 1e-12);
}

TEST_CASE("offset: a line seen edge-on in the plane has no answer") {
    // Along the plane normal there is no sideways to move in.
    Line l(Vec3{0, 0, 0}, Vec3{0, 0, 10});
    CHECK(offset_curve(l, 1.0, Vec3{1, 0, 5}, kWorldZ) == nullptr);
}

TEST_CASE("offset: a circle grows or shrinks, and refuses to pass its centre") {
    Circle c(Vec3{0, 0, 0}, 5.0);

    EntityPtr out = offset_curve(c, 2.0, Vec3{9, 0, 0}, kWorldZ);
    REQUIRE(out != nullptr);
    CHECK(std::abs(static_cast<const Circle*>(out.get())->radius() - 7.0) < 1e-12);

    EntityPtr in = offset_curve(c, 2.0, Vec3{1, 0, 0}, kWorldZ);
    REQUIRE(in != nullptr);
    CHECK(std::abs(static_cast<const Circle*>(in.get())->radius() - 3.0) < 1e-12);

    // Inward past the centre is not a tiny circle, it is no circle.
    CHECK(offset_curve(c, 5.0, Vec3{0, 0, 0}, kWorldZ) == nullptr);
    CHECK(offset_curve(c, 9.0, Vec3{1, 0, 0}, kWorldZ) == nullptr);
}

TEST_CASE("offset: an arc stays concentric and keeps its sweep") {
    Arc a(Vec3{0, 0, 0}, 10.0, 0.0, kFullTurn * 0.25);

    EntityPtr out = offset_curve(a, 1.0, Vec3{20, 0, 0}, kWorldZ);
    REQUIRE(out != nullptr);
    const auto* ao = static_cast<const Arc*>(out.get());
    CHECK_VEC(ao->center(), 0.0, 0.0, 0.0, 1e-12);
    CHECK(std::abs(ao->radius() - 11.0) < 1e-12);
    CHECK(std::abs(ao->start_angle() - a.start_angle()) < 1e-12);
    CHECK(std::abs(ao->end_angle() - a.end_angle()) < 1e-12);
}

TEST_CASE("offset: a right-angled polyline corner miters, it does not round off") {
    // An L: right along X, then up in Y. Offset to the inside of the corner.
    auto pl = std::make_unique<Polyline>();
    pl->add(Vec3{0, 0, 0});
    pl->add(Vec3{10, 0, 0});
    pl->add(Vec3{10, 10, 0});

    EntityPtr out = offset_curve(*pl, 2.0, Vec3{5, 2, 0}, kWorldZ);
    REQUIRE(out != nullptr);
    const Polyline* po = as_pline(out.get());
    REQUIRE(po != nullptr);
    REQUIRE(po->size() == 3);

    // The corner vertex is where the two offset segments MEET -- (8,2) -- not
    // either segment's own displaced endpoint, which would be (10,2) or (8,0)
    // and would leave the outline open.
    CHECK_VEC(po->vertices()[0].pos, 0.0, 2.0, 0.0, 1e-12);
    CHECK_VEC(po->vertices()[1].pos, 8.0, 2.0, 0.0, 1e-12);
    CHECK_VEC(po->vertices()[2].pos, 8.0, 10.0, 0.0, 1e-12);
}

TEST_CASE("offset: a shallow corner miters further out than the distance") {
    // Two segments meeting at 90 degrees offset by d put the corner d*sqrt(2)
    // from the original vertex along the bisector; a SHALLOWER angle puts it
    // further still, which is the property a naive per-segment offset loses.
    auto pl = std::make_unique<Polyline>();
    pl->add(Vec3{0, 0, 0});
    pl->add(Vec3{10, 0, 0});
    pl->add(Vec3{20, 2, 0});  // a gentle turn

    EntityPtr out = offset_curve(*pl, 1.0, Vec3{10, -5, 0}, kWorldZ);
    REQUIRE(out != nullptr);
    const Polyline* po = as_pline(out.get());
    REQUIRE(po != nullptr);

    const Vec3 corner = po->vertices()[1].pos;
    const double moved = length(corner - Vec3{10, 0, 0});
    CHECK(moved > 1.0);   // further than the offset distance
    CHECK(moved < 1.05);  // but only a little, for a gentle turn
    CHECK(corner.y < 0.0);
}

TEST_CASE("offset: a closed polyline stays closed and every corner miters") {
    // A unit square offset outward by 1 becomes a 3x3 square, which is only
    // true if all four corners were solved as intersections.
    auto pl = std::make_unique<Polyline>();
    pl->add(Vec3{0, 0, 0});
    pl->add(Vec3{1, 0, 0});
    pl->add(Vec3{1, 1, 0});
    pl->add(Vec3{0, 1, 0});
    pl->set_closed(true);

    EntityPtr out = offset_curve(*pl, 1.0, Vec3{-5, 0.5, 0}, kWorldZ);
    REQUIRE(out != nullptr);
    const Polyline* po = as_pline(out.get());
    REQUIRE(po != nullptr);
    CHECK(po->closed());
    REQUIRE(po->size() == 4);

    CHECK_VEC(po->vertices()[0].pos, -1.0, -1.0, 0.0, 1e-12);
    CHECK_VEC(po->vertices()[1].pos, 2.0, -1.0, 0.0, 1e-12);
    CHECK_VEC(po->vertices()[2].pos, 2.0, 2.0, 0.0, 1e-12);
    CHECK_VEC(po->vertices()[3].pos, -1.0, 2.0, 0.0, 1e-12);
}

TEST_CASE("offset: a bulged polyline keeps its arc, with the radius moved") {
    // One straight then one semicircular bulge. Offsetting outward must widen
    // the arc rather than turning it into a chord.
    auto pl = std::make_unique<Polyline>();
    pl->add(Vec3{0, 0, 0}, 0.0);
    pl->add(Vec3{10, 0, 0}, 1.0);  // bulge 1 = a half turn
    pl->add(Vec3{10, 4, 0}, 0.0);

    EntityPtr out = offset_curve(*pl, 1.0, Vec3{5, -3, 0}, kWorldZ);
    REQUIRE(out != nullptr);
    const Polyline* po = as_pline(out.get());
    REQUIRE(po != nullptr);
    REQUIRE(po->size() == 3);

    // The arc segment is still an arc, still a half turn: a semicircle offset
    // concentrically subtends the same angle whatever its radius.
    CHECK(std::abs(po->vertices()[1].bulge - 1.0) < 1e-9);

    Vec3 centre{};
    double radius = 0.0;
    double sa = 0.0;
    double ea = 0.0;
    REQUIRE(po->segment_arc(1, &centre, &radius, &sa, &ea));
    // The source arc has radius 2 about (10,2); offset away from it gives 3.
    CHECK_VEC(centre, 10.0, 2.0, 0.0, 1e-9);
    CHECK(std::abs(radius - 3.0) < 1e-9);
}

TEST_CASE("offset: entities with no offset decline rather than approximate") {
    Ellipse e(Vec3{0, 0, 0}, Vec3{10, 0, 0}, 0.5);
    CHECK(offset_curve(e, 1.0, Vec3{0, 20, 0}, kWorldZ) == nullptr);

    Text t;
    CHECK(offset_curve(t, 1.0, Vec3{0, 1, 0}, kWorldZ) == nullptr);
}

TEST_CASE("offset: properties travel with the copy") {
    Database db;
    const LayerId walls = db.add_layer("WALLS", 5);
    Line l(Vec3{0, 0, 0}, Vec3{10, 0, 0});
    l.props().layer = walls;
    l.props().color = 3;

    EntityPtr out = offset_curve(l, 1.0, Vec3{5, 1, 0}, kWorldZ);
    REQUIRE(out != nullptr);
    CHECK(out->props().layer == walls);
    CHECK(out->props().color == 3);
}

// --- the command ------------------------------------------------------------

TEST_CASE("offset command: distance, object, side, and it keeps asking") {
    Database db;
    CommandEngine engine(db);
    const Handle a = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));
    const Handle b = db.add(std::make_unique<Line>(Vec3{0, 20, 0}, Vec3{10, 20, 0}));

    engine.begin(make_command("OFFSET"));
    engine.supply(InputValue::of_real(2.0));
    engine.supply(InputValue::of_entity(a));
    engine.supply(InputValue::of_point({5, 5, 0}));
    // Still running: the loop is the command.
    CHECK(engine.active());
    engine.supply(InputValue::of_entity(b));
    engine.supply(InputValue::of_point({5, 25, 0}));
    engine.supply(InputValue::none());
    CHECK(engine.status() == EngineStatus::Finished);

    CHECK(db.size() == 4);
    CHECK_VEC(static_cast<const Line*>(db.get(db.order()[2]))->start(), 0.0, 2.0, 0.0, 1e-12);
    CHECK_VEC(static_cast<const Line*>(db.get(db.order()[3]))->start(), 0.0, 22.0, 0.0, 1e-12);
}

TEST_CASE("offset command: Through takes its distance from the point") {
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    engine.begin(make_command("OFFSET"));
    engine.supply(InputValue::of_keyword("THROUGH"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_point({5, 3.5, 0}));
    engine.supply(InputValue::none());

    REQUIRE(db.size() == 2);
    CHECK_VEC(static_cast<const Line*>(db.get(db.order()[1]))->start(), 0.0, 3.5, 0.0, 1e-12);
}

TEST_CASE("offset command: the distance is remembered for next time") {
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    engine.begin(make_command("OFFSET"));
    engine.supply(InputValue::of_real(4.0));
    engine.supply(InputValue::none());

    // Enter at the distance prompt now means "the same as last time".
    engine.begin(make_command("OFFSET"));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_point({5, 1, 0}));
    engine.supply(InputValue::none());

    REQUIRE(db.size() == 2);
    CHECK_VEC(static_cast<const Line*>(db.get(db.order()[1]))->start(), 0.0, 4.0, 0.0, 1e-12);
}

TEST_CASE("offset command: one undo removes the whole run") {
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    engine.begin(make_command("OFFSET"));
    engine.supply(InputValue::of_real(1.0));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_point({5, 1, 0}));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::of_point({5, -1, 0}));
    engine.supply(InputValue::none());
    CHECK(db.size() == 3);

    engine.begin(make_command("UNDO"));
    CHECK(db.size() == 1);
}

// --- file prompts -----------------------------------------------------------
//
// A window offers a dialog for these and the terminal does not, so what is
// testable here is the fact the prompt states rather than the dialog itself:
// that a file prompt says it is one, and says which kind.

TEST_CASE("file prompts: the ones that name a file say so, with an extension") {
    Database db;
    CommandEngine engine(db);

    struct Expect {
        const char* command;
        FileIntent intent;
        const char* extension;
    };
    const Expect cases[] = {
        {"OPEN", FileIntent::Open, "dxf"},   {"DXFIN", FileIntent::Open, "dxf"},
        {"SAVEAS", FileIntent::Save, "dxf"}, {"DXFOUT", FileIntent::Save, "dxf"},
        {"WBLOCK", FileIntent::Save, "dxf"}, {"APPLOAD", FileIntent::Open, "lsp"},
    };

    for (const Expect& c : cases) {
        engine.begin(make_command(c.command));
        REQUIRE(engine.active());
        CHECK(engine.prompt().kind == PromptKind::String);
        CHECK(engine.prompt().file == c.intent);
        CHECK(engine.prompt().file_extension == c.extension);
        engine.cancel();
    }
}

TEST_CASE("file prompts: a prompt that is not about a file says nothing about one") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LINE"));
    CHECK(engine.prompt().file == FileIntent::None);
    engine.cancel();

    // BLOCK asks for a NAME, not a file -- the distinction the flag exists to
    // make, since both are String prompts and only one wants a file dialog.
    engine.begin(make_command("BLOCK"));
    CHECK(engine.prompt().kind == PromptKind::String);
    CHECK(engine.prompt().file == FileIntent::None);
    engine.cancel();
}

TEST_CASE("file prompts: FILEDIA exists, defaults on, and is not drawing state") {
    Database db;
    CHECK(db.sysvars().get_int(Sysvar::FileDia) == 1);

    // Settable from LISP and scripts, which is the point: a routine that drives
    // OPEN turns it off so no modal window waits for a person who is not there.
    CHECK(db.sysvars().set_int(Sysvar::FileDia, 0) == Sysvars::SetStatus::Ok);
    CHECK(db.sysvars().get_int(Sysvar::FileDia) == 0);
    // Bounded to 0/1: it is a switch, and 2 is not a third kind of dialog.
    CHECK(db.sysvars().set_int(Sysvar::FileDia, 2) == Sysvars::SetStatus::OutOfRange);

    // Not saved in the drawing -- it follows the installation, like PICKBOX --
    // so opening a file cannot turn someone's dialogs back on.
    const SysvarDef* def = find_sysvar("FILEDIA");
    REQUIRE(def != nullptr);
    CHECK(!def->save_in_drawing);
}

// --- FILLET / CHAMFER -------------------------------------------------------

TEST_CASE("fillet: a right angle gets a tangent arc and two shortened lines") {
    Line a(Vec3{0, 0, 0}, Vec3{10, 0, 0});
    Line b(Vec3{10, 0, 0}, Vec3{10, 10, 0});

    CornerFit fit;
    REQUIRE(fillet_lines(a, Vec3{2, 0, 0}, b, Vec3{10, 2, 0}, 2.0, kWorldZ, &fit));

    // Tangent length equals the radius at 90 degrees.
    CHECK_VEC(fit.cut_a, 8.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(fit.cut_b, 10.0, 2.0, 0.0, 1e-12);
    CHECK_VEC(fit.centre, 8.0, 2.0, 0.0, 1e-12);
    CHECK(fit.has_arc);
    CHECK(std::abs(fit.radius - 2.0) < 1e-12);

    // The surviving ends are the ones the picks were on.
    CHECK_VEC(fit.keep_a, 0.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(fit.keep_b, 10.0, 10.0, 0.0, 1e-12);
}

TEST_CASE("fillet: radius zero closes the corner without rounding it") {
    // Two lines that do not reach each other: the crossing is beyond both ends.
    Line a(Vec3{0, 0, 0}, Vec3{8, 0, 0});
    Line b(Vec3{10, 2, 0}, Vec3{10, 10, 0});

    CornerFit fit;
    REQUIRE(fillet_lines(a, Vec3{2, 0, 0}, b, Vec3{10, 8, 0}, 0.0, kWorldZ, &fit));
    CHECK(!fit.has_arc);
    // Both are extended to meet exactly, which is what makes this the tool for
    // closing a corner that falls short.
    CHECK_VEC(fit.cut_a, 10.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(fit.cut_b, 10.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(fit.keep_a, 0.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(fit.keep_b, 10.0, 10.0, 0.0, 1e-12);
}

TEST_CASE("fillet: a radius the lines cannot carry is refused") {
    Line a(Vec3{0, 0, 0}, Vec3{3, 0, 0});
    Line b(Vec3{3, 0, 0}, Vec3{3, 3, 0});

    CornerFit fit;
    // A radius of 10 needs 10 units of tangent on lines that are 3 long.
    CHECK(!fillet_lines(a, Vec3{1, 0, 0}, b, Vec3{3, 1, 0}, 10.0, kWorldZ, &fit));
}

TEST_CASE("fillet: parallel lines have no corner") {
    Line a(Vec3{0, 0, 0}, Vec3{10, 0, 0});
    Line b(Vec3{0, 5, 0}, Vec3{10, 5, 0});

    CornerFit fit;
    CHECK(!fillet_lines(a, Vec3{5, 0, 0}, b, Vec3{5, 5, 0}, 1.0, kWorldZ, &fit));
}

TEST_CASE("chamfer: the corner is cut back by each distance in turn") {
    Line a(Vec3{0, 0, 0}, Vec3{10, 0, 0});
    Line b(Vec3{10, 0, 0}, Vec3{10, 10, 0});

    CornerFit fit;
    REQUIRE(chamfer_lines(a, Vec3{2, 0, 0}, b, Vec3{10, 2, 0}, 3.0, 1.0, kWorldZ, &fit));
    CHECK(!fit.has_arc);
    CHECK_VEC(fit.cut_a, 7.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(fit.cut_b, 10.0, 1.0, 0.0, 1e-12);
}

TEST_CASE("fillet command: the drawing ends with two lines and an arc") {
    Database db;
    CommandEngine engine(db);
    const Handle a = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));
    const Handle b = db.add(std::make_unique<Line>(Vec3{10, 0, 0}, Vec3{10, 10, 0}));

    engine.begin(make_command("FILLET"));
    engine.supply(InputValue::of_keyword("RADIUS"));
    engine.supply(InputValue::of_real(2.0));
    engine.supply(InputValue::of_picked_entity(a, Vec3{2, 0, 0}));
    engine.supply(InputValue::of_picked_entity(b, Vec3{10, 2, 0}));
    CHECK(engine.status() == EngineStatus::Finished);

    CHECK(db.size() == 3);
    const auto* la = static_cast<const Line*>(db.get(a));
    CHECK_VEC(la->end(), 8.0, 0.0, 0.0, 1e-12);
    const Entity* last = db.get(db.last());
    REQUIRE(last->type() == EntityType::Arc);
    CHECK(std::abs(static_cast<const Arc*>(last)->radius() - 2.0) < 1e-12);
}

TEST_CASE("fillet command: the radius is remembered, and non-lines are refused") {
    Database db;
    CommandEngine engine(db);
    const Handle a = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));
    const Handle c = db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 5.0));

    engine.begin(make_command("FILLET"));
    engine.supply(InputValue::of_keyword("RADIUS"));
    engine.supply(InputValue::of_real(1.5));
    engine.supply(InputValue::none());  // back at the object prompt, Enter ends

    engine.begin(make_command("FILLET"));
    engine.supply(InputValue::of_picked_entity(a, Vec3{2, 0, 0}));
    engine.supply(InputValue::of_picked_entity(c, Vec3{5, 0, 0}));
    CHECK(engine.status() == EngineStatus::Failed);
    CHECK(db.size() == 2);  // nothing was changed by the refusal
}

TEST_CASE("fillet command: a handle with no pick point still works") {
    // What `(command "FILLET" ...)` and the terminal both send: an entity with
    // no location. The midpoint stands in, which is the side that survives
    // whenever the corner is at an end -- so FILLET is scriptable, not
    // mouse-only.
    Database db;
    CommandEngine engine(db);
    const Handle a = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));
    const Handle b = db.add(std::make_unique<Line>(Vec3{10, 0, 0}, Vec3{10, 10, 0}));

    engine.begin(make_command("FILLET"));
    engine.supply(InputValue::of_keyword("RADIUS"));
    engine.supply(InputValue::of_real(2.0));
    engine.supply(InputValue::of_entity(a));
    engine.supply(InputValue::of_entity(b));
    CHECK(engine.status() == EngineStatus::Finished);

    CHECK(db.size() == 3);
    CHECK_VEC(static_cast<const Line*>(db.get(a))->end(), 8.0, 0.0, 0.0, 1e-12);
    CHECK(db.get(db.last())->type() == EntityType::Arc);
}

TEST_CASE("chamfer command: distances are asked once and then remembered") {
    Database db;
    CommandEngine engine(db);
    const Handle a = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));
    const Handle b = db.add(std::make_unique<Line>(Vec3{10, 0, 0}, Vec3{10, 10, 0}));

    engine.begin(make_command("CHAMFER"));
    engine.supply(InputValue::of_keyword("DISTANCES"));
    engine.supply(InputValue::of_real(2.0));
    engine.supply(InputValue::of_real(2.0));
    engine.supply(InputValue::of_picked_entity(a, Vec3{2, 0, 0}));
    engine.supply(InputValue::of_picked_entity(b, Vec3{10, 2, 0}));
    CHECK(engine.status() == EngineStatus::Finished);

    CHECK(db.size() == 3);
    const Entity* last = db.get(db.last());
    REQUIRE(last->type() == EntityType::Line);
    const auto* join = static_cast<const Line*>(last);
    CHECK_VEC(join->start(), 8.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(join->end(), 10.0, 2.0, 0.0, 1e-12);
}
