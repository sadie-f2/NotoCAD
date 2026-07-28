// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The user coordinate system.
//
// The properties that carry the weight:
//
//   A typed coordinate is read in the current UCS. That is the whole feature,
//   and everything else is scaffolding for it. A picked coordinate is NOT --
//   the viewport already unprojected it into world -- and keeping those two
//   apart is the thing most likely to go wrong.
//
//   A relative coordinate rotates but does not translate. `@5,0` means five
//   units along the UCS X axis from the last point, so its offset is a vector
//   and applying the UCS origin to it would move the point twice.
//
//   The frame is always orthonormal, because the axes arrive through system
//   variables that LISP can write, and a sheared frame would put geometry
//   somewhere no transform could undo.
//
//   Geometry drawn in a tilted UCS gets the right EXTRUSION, which is where UCS
//   and ECS finally meet. If that is wrong the drawing looks right here and
//   wrong in every other program.

#include "test.hpp"

#include "noto/command.hpp"
#include "noto/commands.hpp"
#include "noto/database.hpp"
#include "noto/dxf.hpp"
#include "noto/dxf_read.hpp"
#include "noto/entities.hpp"
#include "noto/input_text.hpp"

#include <cmath>
#include <memory>
#include <numbers>
#include <sstream>

using namespace noto;

namespace {

constexpr double kPi = std::numbers::pi;

// Parses a token as a point against `db`'s current UCS, the way the command
// line does.
Vec3 typed_point(const Database& db, const char* token, const Vec3* last = nullptr) {
    Prompt prompt;
    prompt.kind = PromptKind::Point;
    if (last) {
        prompt.last_point = *last;
        prompt.has_last_point = true;
    }

    const Mat4 frame = db.current_ucs().to_world();
    InputValue out;
    std::string error;
    if (!parse_input(prompt, token, out, error, &frame)) return Vec3{-999, -999, -999};
    return out.point;
}

}  // namespace

// --- the frame --------------------------------------------------------------

TEST_CASE("ucs: a new drawing is in the world system") {
    Database db;
    const Ucs u = db.current_ucs();
    CHECK(u.is_world());
    CHECK(near_equal(u.origin, Vec3{0, 0, 0}, 1e-12));
    CHECK(near_equal(u.zdir(), kWorldZ, 1e-12));
    CHECK(db.sysvars().get_int(Sysvar::WorldUcs) == 1);
}

TEST_CASE("ucs: axes are orthonormalised on the way out") {
    // The axes reach the database through system variables, so a frame whose
    // Y is not perpendicular to its X can be set. It must not survive as one.
    Ucs skewed;
    skewed.xdir = {2, 0, 0};        // not unit
    skewed.ydir = {1, 1, 0};        // not perpendicular
    const Ucs n = skewed.normalized();

    CHECK_NEAR(length(n.xdir), 1.0, 1e-12);
    CHECK_NEAR(length(n.ydir), 1.0, 1e-12);
    CHECK_NEAR(dot(n.xdir, n.ydir), 0.0, 1e-12);
    // X kept its direction; Y gave way, which is what Gram-Schmidt does and is
    // the right choice -- X is the axis the user named first.
    CHECK(near_equal(n.xdir, kWorldX, 1e-12));
    CHECK(near_equal(n.ydir, kWorldY, 1e-12));
}

TEST_CASE("ucs: a degenerate frame is repaired rather than propagated") {
    Ucs bad;
    bad.xdir = {0, 0, 0};
    bad.ydir = {0, 0, 0};
    const Ucs n = bad.normalized();
    CHECK_NEAR(length(n.xdir), 1.0, 1e-12);
    CHECK_NEAR(length(n.ydir), 1.0, 1e-12);
    CHECK_NEAR(dot(n.xdir, n.ydir), 0.0, 1e-12);
}

TEST_CASE("ucs: parallel axes still yield a usable frame") {
    Ucs bad;
    bad.xdir = {1, 0, 0};
    bad.ydir = {2, 0, 0};  // says nothing X has not already said
    const Ucs n = bad.normalized();
    CHECK_NEAR(dot(n.xdir, n.ydir), 0.0, 1e-12);
    CHECK_NEAR(length(cross(n.xdir, n.ydir)), 1.0, 1e-12);
}

TEST_CASE("ucs: to_world and from_world are inverses") {
    Ucs u;
    u.origin = {10, 20, 30};
    u.xdir = {0, 1, 0};
    u.ydir = {0, 0, 1};

    const Mat4 out = u.to_world();
    const Mat4 back = u.from_world();

    for (const Vec3 p : {Vec3{0, 0, 0}, Vec3{1, 2, 3}, Vec3{-5, 0.25, 7}}) {
        const Vec3 world = out.transform_point(p);
        CHECK(near_equal(back.transform_point(world), p, 1e-9));
    }
}

TEST_CASE("ucs: the origin maps to the origin") {
    Ucs u;
    u.origin = {10, 20, 30};
    CHECK(near_equal(u.to_world().transform_point(Vec3{0, 0, 0}), Vec3{10, 20, 30}, 1e-12));
}

// --- typed coordinates ------------------------------------------------------

TEST_CASE("ucs: a typed coordinate is read in the current system") {
    Database db;
    CHECK(near_equal(typed_point(db, "1,2,3"), Vec3{1, 2, 3}, 1e-12));

    // Move the origin: the same text now means somewhere else.
    Ucs u;
    u.origin = {100, 0, 0};
    db.set_current_ucs(u);
    CHECK(near_equal(typed_point(db, "1,2,3"), Vec3{101, 2, 3}, 1e-12));
}

TEST_CASE("ucs: a typed coordinate follows a rotated frame") {
    Database db;
    // X points along world Y, Y points along world -X: a quarter turn.
    Ucs u;
    u.xdir = {0, 1, 0};
    u.ydir = {-1, 0, 0};
    db.set_current_ucs(u);

    CHECK(near_equal(typed_point(db, "5,0"), Vec3{0, 5, 0}, 1e-12));
    CHECK(near_equal(typed_point(db, "0,5"), Vec3{-5, 0, 0}, 1e-12));
}

TEST_CASE("ucs: a typed coordinate in a vertical plane leaves world XY") {
    Database db;
    // The XZ plane: UCS Y is world Z.
    Ucs u;
    u.xdir = {1, 0, 0};
    u.ydir = {0, 0, 1};
    db.set_current_ucs(u);

    CHECK(near_equal(typed_point(db, "3,4"), Vec3{3, 0, 4}, 1e-12));
    CHECK(near_equal(db.construction_normal(), Vec3{0, -1, 0}, 1e-12));
}

TEST_CASE("ucs: a relative coordinate rotates but does not translate") {
    // The case that would be wrong if the offset were transformed as a point:
    // the UCS origin would be applied on top of a base that is already world.
    Database db;
    Ucs u;
    u.origin = {100, 200, 0};
    u.xdir = {0, 1, 0};
    u.ydir = {-1, 0, 0};
    db.set_current_ucs(u);

    const Vec3 last{10, 10, 0};
    // Five along the UCS X, which is world +Y, from the last point.
    CHECK(near_equal(typed_point(db, "@5,0", &last), Vec3{10, 15, 0}, 1e-12));
}

TEST_CASE("ucs: polar input is measured in the UCS plane") {
    Database db;
    Ucs u;
    u.xdir = {0, 1, 0};
    u.ydir = {-1, 0, 0};
    db.set_current_ucs(u);

    // Ten units at zero degrees is along the UCS X axis, which is world +Y.
    CHECK(near_equal(typed_point(db, "10<0"), Vec3{0, 10, 0}, 1e-9));
}

TEST_CASE("ucs: with no database the parser stays in world") {
    // Every caller that predates UCS, and any caller with no drawing.
    Prompt prompt;
    prompt.kind = PromptKind::Point;
    InputValue out;
    std::string error;
    REQUIRE(parse_input(prompt, "1,2,3", out, error, nullptr));
    CHECK(near_equal(out.point, Vec3{1, 2, 3}, 1e-12));
}

// --- the command ------------------------------------------------------------

TEST_CASE("ucs: Origin moves the frame without reorienting it") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("ORIGIN"));
    engine.supply(InputValue::of_point({10, 20, 30}));

    const Ucs u = db.current_ucs();
    CHECK(near_equal(u.origin, Vec3{10, 20, 30}, 1e-12));
    CHECK(near_equal(u.xdir, kWorldX, 1e-12));
    CHECK(db.sysvars().get_int(Sysvar::WorldUcs) == 0);
}

TEST_CASE("ucs: World gets back to the world system") {
    Database db;
    Ucs u;
    u.origin = {5, 5, 5};
    u.xdir = {0, 1, 0};
    u.ydir = {0, 0, 1};
    db.set_current_ucs(u, "TILTED");

    CommandEngine engine(db);
    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("WORLD"));

    CHECK(db.current_ucs().is_world());
    CHECK(db.sysvars().get_string(Sysvar::UcsName).empty());
}

TEST_CASE("ucs: Enter is World, which is R12's default answer") {
    Database db;
    Ucs u;
    u.origin = {5, 5, 5};
    db.set_current_ucs(u);

    CommandEngine engine(db);
    engine.begin(make_command("UCS"));
    engine.supply(InputValue::none());

    CHECK(db.current_ucs().is_world());
}

TEST_CASE("ucs: 3point builds a frame from an origin and two directions") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("3POINT"));
    engine.supply(InputValue::of_point({1, 1, 0}));   // origin
    engine.supply(InputValue::of_point({2, 1, 0}));   // +X
    engine.supply(InputValue::of_point({1, 1, 5}));   // +Y, straight up

    const Ucs u = db.current_ucs();
    CHECK(near_equal(u.origin, Vec3{1, 1, 0}, 1e-12));
    CHECK(near_equal(u.xdir, kWorldX, 1e-9));
    CHECK(near_equal(u.ydir, kWorldZ, 1e-9));
    // The plane faces -Y.
    CHECK(near_equal(u.zdir(), Vec3{0, -1, 0}, 1e-9));
}

TEST_CASE("ucs: 3point refuses collinear points") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("3POINT"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({1, 0, 0}));
    const EngineStatus status = engine.supply(InputValue::of_point({2, 0, 0}));

    CHECK(status == EngineStatus::Failed);
    CHECK(db.current_ucs().is_world());
}

TEST_CASE("ucs: ZAxis takes a normal and derives the rest") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("ZAXIS"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({1, 0, 0}));  // Z along world X

    const Ucs u = db.current_ucs();
    CHECK(near_equal(u.zdir(), kWorldX, 1e-9));
    // And the axes it derived are a proper frame.
    CHECK_NEAR(dot(u.xdir, u.ydir), 0.0, 1e-12);
}

TEST_CASE("ucs: X rotates the current frame about its own X axis") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("X"));
    engine.supply(InputValue::of_real(90.0));

    const Ucs u = db.current_ucs();
    // Y has swung up to world Z, so the plane now stands vertical.
    CHECK(near_equal(u.xdir, kWorldX, 1e-9));
    CHECK(near_equal(u.ydir, kWorldZ, 1e-9));
}

TEST_CASE("ucs: rotations compose on the current frame, not on world") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("X"));
    engine.supply(InputValue::of_real(90.0));

    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("X"));
    engine.supply(InputValue::of_real(90.0));

    // Two quarter turns about X is a half turn.
    const Ucs u = db.current_ucs();
    CHECK(near_equal(u.ydir, Vec3{0, -1, 0}, 1e-9));
}

TEST_CASE("ucs: Save then Restore brings a frame back") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("3POINT"));
    engine.supply(InputValue::of_point({1, 2, 3}));
    engine.supply(InputValue::of_point({2, 2, 3}));
    engine.supply(InputValue::of_point({1, 2, 4}));

    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("SAVE"));
    engine.supply(InputValue::of_string("SIDE"));
    CHECK(db.ucs_table().size() == 1);
    CHECK(db.sysvars().get_string(Sysvar::UcsName) == "SIDE");

    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("WORLD"));
    CHECK(db.current_ucs().is_world());

    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("RESTORE"));
    engine.supply(InputValue::of_string("SIDE"));

    const Ucs u = db.current_ucs();
    CHECK(near_equal(u.origin, Vec3{1, 2, 3}, 1e-9));
    CHECK(near_equal(u.zdir(), Vec3{0, -1, 0}, 1e-9));
    CHECK(db.sysvars().get_string(Sysvar::UcsName) == "SIDE");
}

TEST_CASE("ucs: Restore of an unknown name fails") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("RESTORE"));
    const EngineStatus status = engine.supply(InputValue::of_string("NOPE"));
    CHECK(status == EngineStatus::Failed);
}

TEST_CASE("ucs: Prev goes back one frame") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("ORIGIN"));
    engine.supply(InputValue::of_point({10, 0, 0}));

    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("ORIGIN"));
    engine.supply(InputValue::of_point({20, 0, 0}));
    CHECK_NEAR(db.current_ucs().origin.x, 20.0, 1e-12);

    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("PREV"));
    CHECK_NEAR(db.current_ucs().origin.x, 10.0, 1e-12);
}

TEST_CASE("ucs: Entity adopts the entity's own plane") {
    // Where UCS and ECS meet: an entity's extrusion IS a construction plane,
    // and this option is the one place the two are the same thing.
    Database db;
    const Handle h = db.add(std::make_unique<Circle>(Vec3{1, 2, 3}, 5.0, Vec3{0, 1, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("ENTITY"));
    engine.supply(InputValue::of_entity(h));

    CHECK(near_equal(db.current_ucs().zdir(), Vec3{0, 1, 0}, 1e-9));
}

TEST_CASE("ucs: Delete removes a saved frame") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("SAVE"));
    engine.supply(InputValue::of_string("GONE"));
    CHECK(db.find_ucs("GONE") != kInvalidUcs);

    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("DELETE"));
    engine.supply(InputValue::of_string("GONE"));
    CHECK(db.find_ucs("GONE") == kInvalidUcs);
}

TEST_CASE("ucs: setting the frame is undoable") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("ORIGIN"));
    engine.supply(InputValue::of_point({10, 20, 30}));
    CHECK(!db.current_ucs().is_world());

    engine.begin(make_command("UNDO"));
    CHECK(db.current_ucs().is_world());
}

TEST_CASE("ucsicon: sets the variable R12 packs both answers into") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("UCSICON"));
    engine.supply(InputValue::of_keyword("OFF"));
    CHECK(db.sysvars().get_int(Sysvar::UcsIcon) == 0);

    engine.begin(make_command("UCSICON"));
    engine.supply(InputValue::of_keyword("ORIGIN"));
    CHECK(db.sysvars().get_int(Sysvar::UcsIcon) == 2);
}

TEST_CASE("ucs: the read-only variables refuse a direct setvar") {
    // R12 marks them read-only because the UCS command owns them.
    Database db;
    const Sysvars::SetStatus s =
        db.sysvars().set("UCSORG", SysvarValue::of_point(Vec3{1, 1, 1}));
    CHECK(s == Sysvars::SetStatus::ReadOnly);
    CHECK(db.current_ucs().is_world());
}

// --- geometry drawn in a UCS ------------------------------------------------

TEST_CASE("ucs: a circle drawn in a tilted UCS gets the right extrusion") {
    // Where UCS meets ECS, and the test that matters most: get this wrong and
    // the drawing looks right here and wrong everywhere else.
    Database db;
    CommandEngine engine(db);

    // Stand the construction plane up in XZ.
    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("X"));
    engine.supply(InputValue::of_real(90.0));

    engine.begin(make_command("CIRCLE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(5.0));

    const Entity* e = db.get(db.last());
    REQUIRE(e != nullptr);
    REQUIRE(e->type() == EntityType::Circle);
    // The circle's plane is the construction plane, not world XY.
    CHECK(near_equal(e->props().normal, db.construction_normal(), 1e-9));
    CHECK(near_equal(e->props().normal, Vec3{0, -1, 0}, 1e-9));
}

TEST_CASE("ucs: a rotation acts in the construction plane, not in world XY") {
    Database db;
    CommandEngine engine(db);

    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    // Stand the plane up, then rotate: the line should swing through Z, not
    // stay flat in world XY.
    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("X"));
    engine.supply(InputValue::of_real(90.0));

    engine.begin(make_command("ROTATE"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(90.0));

    const Line* l = static_cast<const Line*>(db.get(h));
    REQUIRE(l != nullptr);
    // Rotating about -Y takes +X down to -Z.
    CHECK_NEAR(std::abs(l->end().z), 10.0, 1e-9);
    CHECK_NEAR(l->end().x, 0.0, 1e-9);
}

// --- DXF --------------------------------------------------------------------

TEST_CASE("ucs: the current frame survives a DXF round trip") {
    Database source;
    Ucs u;
    u.origin = {1, 2, 3};
    u.xdir = {0, 1, 0};
    u.ydir = {0, 0, 1};
    source.set_current_ucs(u, "SIDE");

    std::ostringstream out;
    DxfWriter w(out, source);
    w.write_document();

    Database loaded;
    const DxfReadResult r = read_dxf_text(loaded, out.str());
    CHECK(r.ok);

    const Ucs back = loaded.current_ucs();
    CHECK(near_equal(back.origin, Vec3{1, 2, 3}, 1e-9));
    CHECK(near_equal(back.xdir, Vec3{0, 1, 0}, 1e-9));
    CHECK(near_equal(back.ydir, Vec3{0, 0, 1}, 1e-9));
    CHECK(loaded.sysvars().get_string(Sysvar::UcsName) == "SIDE");
    CHECK(loaded.sysvars().get_int(Sysvar::WorldUcs) == 0);
}

TEST_CASE("ucs: named systems survive a DXF round trip") {
    Database source;
    Ucs side;
    side.origin = {5, 0, 0};
    side.xdir = {0, 1, 0};
    side.ydir = {0, 0, 1};
    source.add_ucs("SIDE", side);

    Ucs top;
    top.origin = {0, 0, 9};
    source.add_ucs("TOP", top);

    std::ostringstream out;
    DxfWriter w(out, source);
    w.write_document();

    Database loaded;
    const DxfReadResult r = read_dxf_text(loaded, out.str());
    CHECK(r.ok);
    CHECK(r.coordinate_systems == 2);
    REQUIRE(loaded.ucs_table().size() == 2);

    const UcsId id = loaded.find_ucs("SIDE");
    REQUIRE(id != kInvalidUcs);
    CHECK(near_equal(loaded.ucs(id).ucs.origin, Vec3{5, 0, 0}, 1e-9));
    CHECK(near_equal(loaded.ucs(id).ucs.zdir(), Vec3{1, 0, 0}, 1e-9));
}

TEST_CASE("ucs: a world drawing writes no surprise into the header") {
    Database db;
    std::ostringstream out;
    DxfWriter w(out, db);
    w.write_document();

    const std::string text = out.str();
    CHECK(text.find("$UCSORG") != std::string::npos);
    CHECK(text.find("$WORLDUCS") != std::string::npos);

    // And it reads back as world rather than as a frame that merely looks like
    // one.
    Database loaded;
    read_dxf_text(loaded, text);
    CHECK(loaded.current_ucs().is_world());
    CHECK(loaded.sysvars().get_int(Sysvar::WorldUcs) == 1);
}

// --- session memory ---------------------------------------------------------
//
// UCS Prev lived in a file-scope static when it was first written, which is one
// per PROCESS: two drawings shared it, undo could not see it, and it leaked
// between test cases. It lives in CommandMemory now, owned by the engine beside
// the selection and LASTPOINT.

TEST_CASE("ucs: Prev is per-engine, not per-process") {
    // The defect the static had. Two drawings must not share one another's
    // previous coordinate system.
    Database a;
    CommandEngine engine_a(a);
    engine_a.begin(make_command("UCS"));
    engine_a.supply(InputValue::of_keyword("ORIGIN"));
    engine_a.supply(InputValue::of_point({50, 0, 0}));

    Database b;
    CommandEngine engine_b(b);
    // This engine has no history at all, whatever the other one did.
    engine_b.begin(make_command("UCS"));
    const EngineStatus status = engine_b.supply(InputValue::of_keyword("PREV"));
    CHECK(status == EngineStatus::Failed);
    CHECK(b.current_ucs().is_world());
}

TEST_CASE("ucs: Prev with no history says so rather than snapping to world") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("UCS"));
    const EngineStatus status = engine.supply(InputValue::of_keyword("PREV"));
    CHECK(status == EngineStatus::Failed);
}

TEST_CASE("ucs: the previous system is session state, not drawing state") {
    // A UCS change is undoable; what Prev would restore is not. That is R12's
    // behaviour and it is what "session state" means -- the previous system
    // describes what you were doing, not what you drew.
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("UCS"));
    engine.supply(InputValue::of_keyword("ORIGIN"));
    engine.supply(InputValue::of_point({10, 0, 0}));

    engine.begin(make_command("UNDO"));
    CHECK(db.current_ucs().is_world());
    // The memory still holds what it held: undo rolled back the drawing, not
    // the session.
    CHECK(engine.memory().has_previous_ucs);
}
