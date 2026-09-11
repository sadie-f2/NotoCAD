// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The solids boundary. Split deliberately into the two halves that can be
// tested WITHOUT the engine -- what goes out and what comes back -- and a
// small number that need it and skip when it is absent.
//
// That split is the point. The protocol is text, so both directions are pure
// functions over strings, and a bug in either is findable without a
// subprocess, a Python installation, or a working geometry engine.

#include "ncad/database.hpp"
#include "ncad/entities.hpp"
#include "ncad/solids.hpp"
#include "test.hpp"

#include <cstdlib>
#include <string>
#include <vector>

using namespace ncad;

namespace {

std::vector<Handle> all_handles(const Database& db) {
    return std::vector<Handle>(db.order().begin(), db.order().end());
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

}  // namespace

// --- what goes out ----------------------------------------------------------

TEST_CASE("solids: a circle crosses as a CIRCLE with its normal") {
    Database db;
    db.add(std::make_unique<Circle>(Vec3{1.0, 2.0, 3.0}, 5.0));

    std::string body;
    std::string error;
    REQUIRE(solids_profile_request(db, all_handles(db), body, error));
    CHECK(error.empty());
    CHECK(contains(body, "PROFILE\n"));
    CHECK(contains(body, "ENDPROFILE\n"));
    CHECK(contains(body, "CIRCLE 1 2 3 0 0 1 5"));
}

TEST_CASE("solids: a line crosses as its two endpoints") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{0.0, 0.0, 0.0}, Vec3{4.0, 0.0, 0.0}));

    std::string body;
    std::string error;
    REQUIRE(solids_profile_request(db, all_handles(db), body, error));
    CHECK(contains(body, "LINE 0 0 0 4 0 0"));
}

TEST_CASE("solids: an arc carries the axis its angles are measured from") {
    // Without the ECS x-axis the far side has to guess a reference, and a
    // tilted arc then lands rotated. Six numbers of centre and normal are not
    // enough to place it; this is the regression that keeps the ninth through
    // eleventh fields honest.
    Database db;
    db.add(std::make_unique<Arc>(Vec3{0.0, 0.0, 0.0}, 2.0, 0.0, 1.5));

    std::string body;
    std::string error;
    REQUIRE(solids_profile_request(db, all_handles(db), body, error));
    CHECK(contains(body, "ARC 0 0 0 0 0 1 1 0 0 2 0 1.5"));
}

TEST_CASE("solids: a polyline crosses as its segments, arcs included") {
    Database db;
    auto pl = std::make_unique<Polyline>();
    pl->add(Vec3{0.0, 0.0, 0.0});
    pl->add(Vec3{10.0, 0.0, 0.0});
    pl->add(Vec3{10.0, 10.0, 0.0});
    pl->set_closed(true);
    db.add(std::move(pl));

    std::string body;
    std::string error;
    REQUIRE(solids_profile_request(db, all_handles(db), body, error));
    // Closed, so three segments rather than two.
    std::size_t lines = 0;
    for (std::size_t i = body.find("LINE"); i != std::string::npos;
         i = body.find("LINE", i + 1)) {
        ++lines;
    }
    CHECK(lines == 3);
}

TEST_CASE("solids: an entity that cannot be a profile is named, not dropped") {
    // Dropping it silently would produce a solid nobody can explain later,
    // which is the failure this refuses by name.
    Database db;
    db.add(std::make_unique<Circle>(Vec3{}, 1.0));
    db.add(std::make_unique<PointEntity>(Vec3{5.0, 5.0, 0.0}));

    std::string body;
    std::string error;
    CHECK(!solids_profile_request(db, all_handles(db), body, error));
    CHECK(contains(error, "POINT"));
}

TEST_CASE("solids: an empty selection is refused with a reason") {
    Database db;
    std::string body;
    std::string error;
    CHECK(!solids_profile_request(db, {}, body, error));
    CHECK(!error.empty());
}

// --- what comes back --------------------------------------------------------

TEST_CASE("solids: the reply becomes ordinary entities") {
    const std::vector<std::string> reply = {
        "CIRCLE 0 0 0 0 0 1 5",
        "CIRCLE 0 0 4 0 0 1 5",
        "LINE 5 0 0 5 0 4",
        "ARC 1 2 3 0 0 1 1 0 0 2 0 1.5",
        "NOTE volume 314.159265",
    };

    std::vector<EntityPtr> made;
    std::vector<std::string> notes;
    std::string error;
    REQUIRE(solids_wire_entities(reply, made, notes, error));
    REQUIRE(made.size() == 4);
    CHECK(made[0]->type() == EntityType::Circle);
    CHECK(made[1]->type() == EntityType::Circle);
    CHECK(made[2]->type() == EntityType::Line);
    CHECK(made[3]->type() == EntityType::Arc);
    REQUIRE(notes.size() == 1);
    CHECK(notes[0] == "volume 314.159265");

    const auto* c = static_cast<const Circle*>(made[1].get());
    CHECK_NEAR(c->center().z, 4.0, 1e-9);
    CHECK_NEAR(c->radius(), 5.0, 1e-9);

    const auto* a = static_cast<const Arc*>(made[3].get());
    CHECK_NEAR(a->radius(), 2.0, 1e-9);
    CHECK_NEAR(a->start_angle(), 0.0, 1e-9);
    CHECK_NEAR(a->end_angle(), 1.5, 1e-9);
}

TEST_CASE("solids: a PLINE reply becomes a polyline of the stated length") {
    const std::vector<std::string> reply = {
        "PLINE 3 0 0.01",
        "V 0 0 0",
        "V 1 0 0",
        "V 1 1 0",
    };
    std::vector<EntityPtr> made;
    std::vector<std::string> notes;
    std::string error;
    REQUIRE(solids_wire_entities(reply, made, notes, error));
    REQUIRE(made.size() == 1);
    REQUIRE(made[0]->type() == EntityType::Polyline);
    const auto* pl = static_cast<const Polyline*>(made[0].get());
    CHECK(pl->size() == 3);
    CHECK(!pl->closed());
}

TEST_CASE("solids: an ELLIPSE reply survives, which is why R2000 matters") {
    const std::vector<std::string> reply = {
        "ELLIPSE 0 0 0 0 0 1 4 0 0 0.5 0 6.28318530718",
    };
    std::vector<EntityPtr> made;
    std::vector<std::string> notes;
    std::string error;
    REQUIRE(solids_wire_entities(reply, made, notes, error));
    REQUIRE(made.size() == 1);
    CHECK(made[0]->type() == EntityType::Ellipse);
}

TEST_CASE("solids: a truncated record is refused rather than half-read") {
    const std::vector<std::string> reply = {"LINE 0 0 0 1 1"};
    std::vector<EntityPtr> made;
    std::vector<std::string> notes;
    std::string error;
    CHECK(!solids_wire_entities(reply, made, notes, error));
    CHECK(contains(error, "LINE"));
}

TEST_CASE("solids: a PLINE that ends early is refused") {
    const std::vector<std::string> reply = {"PLINE 4 0 0.01", "V 0 0 0", "V 1 0 0"};
    std::vector<EntityPtr> made;
    std::vector<std::string> notes;
    std::string error;
    CHECK(!solids_wire_entities(reply, made, notes, error));
}

TEST_CASE("solids: an unknown record is refused, so a version skew is loud") {
    const std::vector<std::string> reply = {"NURBS 1 2 3"};
    std::vector<EntityPtr> made;
    std::vector<std::string> notes;
    std::string error;
    CHECK(!solids_wire_entities(reply, made, notes, error));
    CHECK(contains(error, "NURBS"));
}

// --- the engine, when there is one ------------------------------------------

TEST_CASE("solids: a missing engine explains itself rather than failing blank") {
    SolidEngine& engine = SolidEngine::instance();
    engine.set_root("");
    std::vector<std::string> reply;
    std::string error;
    // set_root("") marks discovery done with no root, which is the state a
    // machine without the engine installed is in.
    CHECK(!engine.request("PING\n", reply, error));
}

TEST_CASE("solids: a round trip through the real engine, when it is installed") {
    const char* root = std::getenv("NCAD_NSOLID");
    if (!root) {
        std::printf("       skipped: NCAD_NSOLID is not set\n");
        return;
    }
    SolidEngine& engine = SolidEngine::instance();
    engine.set_root(root);

    std::vector<std::string> reply;
    std::string error;
    const std::string body =
        "PROFILE\nCIRCLE 0 0 0 0 0 1 5\nENDPROFILE\nEXTRUDE NORMAL 4\nGO\n";
    REQUIRE(engine.request(body, reply, error));

    std::vector<EntityPtr> made;
    std::vector<std::string> notes;
    REQUIRE(solids_wire_entities(reply, made, notes, error));

    // Two rim circles and four isolines is what an extruded circle is.
    std::size_t circles = 0;
    std::size_t lines = 0;
    for (const EntityPtr& e : made) {
        if (e->type() == EntityType::Circle) ++circles;
        if (e->type() == EntityType::Line) ++lines;
    }
    CHECK(circles == 2);
    CHECK(lines == 4);

    bool said_volume = false;
    for (const std::string& n : notes) {
        if (contains(n, "volume")) said_volume = true;
    }
    CHECK(said_volume);
}

TEST_CASE("solids: the engine refuses geometry that is not a profile") {
    const char* root = std::getenv("NCAD_NSOLID");
    if (!root) return;
    SolidEngine::instance().set_root(root);

    std::vector<std::string> reply;
    std::string error;
    const std::string body =
        "PROFILE\nLINE 0 0 0 1 0 0\nENDPROFILE\nEXTRUDE NORMAL 1\nGO\n";
    CHECK(!SolidEngine::instance().request(body, reply, error));
    // The engine's own words, verbatim: it knows more about the geometry than
    // this side does, and paraphrasing loses the useful part.
    CHECK(!error.empty());
}

// --- the named solid table --------------------------------------------------

TEST_CASE("solids: a defined solid gets a name and keeps its body") {
    SolidTable& t = SolidTable::instance();
    t.clear();

    const std::string a = t.define("SOLID @NAME@\nEXTRUDE NORMAL 1\nENDSOLID\n", {});
    CHECK(a == "S1");
    CHECK(t.has("S1"));
    CHECK(!t.has("S2"));

    std::string body;
    std::string error;
    REQUIRE(t.request_for(a, body, error));
    // @NAME@ is substituted, so nothing downstream has to know the name
    // before it exists.
    CHECK(!contains(body, "@NAME@"));
    CHECK(contains(body, "SOLID S1"));
    CHECK(contains(body, "WIRE S1"));
}

TEST_CASE("solids: a boolean emits its operands first, and exactly once") {
    SolidTable& t = SolidTable::instance();
    t.clear();
    const std::string a = t.define("SOLID @NAME@\nA\nENDSOLID\n", {});
    const std::string b = t.define("SOLID @NAME@\nB\nENDSOLID\n", {});
    const std::string c = t.define("DIFF @NAME@ " + a + " " + b + "\n", {a, b});

    std::string body;
    std::string error;
    REQUIRE(t.request_for(c, body, error));

    const std::size_t pa = body.find("SOLID S1");
    const std::size_t pb = body.find("SOLID S2");
    const std::size_t pc = body.find("DIFF S3");
    REQUIRE(pa != std::string::npos);
    REQUIRE(pb != std::string::npos);
    REQUIRE(pc != std::string::npos);
    CHECK(pa < pc);
    CHECK(pb < pc);
    CHECK(body.rfind("SOLID S1") == pa);   // once, not twice
}

TEST_CASE("solids: a shared operand is still emitted only once") {
    // S3 = S1 - S2, S4 = S3 + S1. S1 is reachable by two routes and defining
    // it twice would be a protocol error on the far side.
    SolidTable& t = SolidTable::instance();
    t.clear();
    const std::string a = t.define("SOLID @NAME@\nA\nENDSOLID\n", {});
    const std::string b = t.define("SOLID @NAME@\nB\nENDSOLID\n", {});
    const std::string c = t.define("DIFF @NAME@ " + a + " " + b + "\n", {a, b});
    const std::string d = t.define("UNION @NAME@ " + c + " " + a + "\n", {c, a});

    std::string body;
    std::string error;
    REQUIRE(t.request_for(d, body, error));

    std::size_t count = 0;
    for (std::size_t i = body.find("SOLID S1"); i != std::string::npos;
         i = body.find("SOLID S1", i + 1)) {
        ++count;
    }
    CHECK(count == 1);
    CHECK(contains(body, "WIRE S4"));
}

TEST_CASE("solids: an unknown name is refused with a reason") {
    SolidTable& t = SolidTable::instance();
    t.clear();
    std::string body;
    std::string error;
    CHECK(!t.request_for("S9", body, error));
    CHECK(contains(error, "S9"));
}

TEST_CASE("solids: the table records what each solid drew, so a boolean can consume it") {
    Database db;
    SolidTable& t = SolidTable::instance();
    t.clear();
    const std::string a = t.define("SOLID @NAME@\nA\nENDSOLID\n", {});

    std::vector<Handle> drawn;
    drawn.push_back(db.add(std::make_unique<Line>(Vec3{}, Vec3{1.0, 0.0, 0.0})));
    drawn.push_back(db.add(std::make_unique<Circle>(Vec3{}, 2.0)));
    t.record_drawn(a, drawn);
    REQUIRE(db.size() == 2);

    CHECK(t.erase_drawn(db, a) == 2);
    CHECK(db.size() == 0);
    // Erasing twice is a no-op rather than a double-erase.
    CHECK(t.erase_drawn(db, a) == 0);
}

TEST_CASE("solids: clearing the table forgets everything, names included") {
    SolidTable& t = SolidTable::instance();
    t.clear();
    t.define("SOLID @NAME@\n", {});
    t.define("SOLID @NAME@\n", {});
    CHECK(t.names().size() == 2);
    t.clear();
    CHECK(t.names().empty());
    CHECK(t.define("SOLID @NAME@\n", {}) == "S1");   // numbering restarts
}

TEST_CASE("solids: a refused solid is taken back out of the table") {
    // The name has to be reserved before the engine sees the request, because
    // the request contains it. So a refusal must undo the reservation -- and
    // because the whole tree is re-sent every time, a poisoned entry does not
    // fail once, it fails every later request that reaches it.
    SolidTable& t = SolidTable::instance();
    t.clear();

    const std::string a = t.define("SOLID @NAME@\nA\nENDSOLID\n", {});
    CHECK(a == "S1");
    t.forget(a);
    CHECK(!t.has("S1"));
    CHECK(t.names().empty());

    // And the numbering is reclaimed, so a failed attempt leaves no hole.
    CHECK(t.define("SOLID @NAME@\nB\nENDSOLID\n", {}) == "S1");
}

TEST_CASE("solids: forgetting a middle solid does not renumber the others") {
    SolidTable& t = SolidTable::instance();
    t.clear();
    t.define("SOLID @NAME@\n", {});
    const std::string b = t.define("SOLID @NAME@\n", {});
    t.define("SOLID @NAME@\n", {});
    t.forget(b);
    CHECK(t.has("S1"));
    CHECK(!t.has("S2"));
    CHECK(t.has("S3"));
    // S3 is still the most recent, so the next name is S4 rather than
    // reusing the hole -- reusing it would make an old name mean a new solid.
    CHECK(t.define("SOLID @NAME@\n", {}) == "S4");
}

TEST_CASE("solids: forgetting something that was never there is harmless") {
    SolidTable& t = SolidTable::instance();
    t.clear();
    t.forget("S9");
    CHECK(t.names().empty());
}

// --- the plane the engine computed must survive ------------------------------

TEST_CASE("solids: applying drawing properties does not flatten the plane") {
    // The bug this pins: assigning the whole EntityProps also assigns
    // `normal`, and `current_props()` leaves that at world Z. The solid stayed
    // right and the line work came back flat -- a revolve about a Y axis drew
    // its rings in XY, and an extrude off a tilted UCS drew its caps there too.
    Database db;
    db.add_layer("SOLIDS", 3);

    EntityProps drawing = db.current_props();
    drawing.layer = db.find_layer("SOLIDS");
    drawing.color = 3;
    CHECK_NEAR(drawing.normal.z, 1.0, 1e-12);   // the default that did the damage

    Circle ring{Vec3{0.0, 2.0, 0.0}, 5.0, Vec3{0.0, 1.0, 0.0}};
    solids_apply_props(ring, drawing);
    CHECK(ring.props().layer == db.find_layer("SOLIDS"));
    CHECK(ring.props().color == 3);
    CHECK_NEAR(ring.props().normal.y, 1.0, 1e-12);
    CHECK_NEAR(ring.props().normal.z, 0.0, 1e-12);

    Arc cap{Vec3{}, 2.0, 0.0, 1.5, Vec3{-1.0, 0.0, 0.0}};
    solids_apply_props(cap, drawing);
    CHECK_NEAR(cap.props().normal.x, -1.0, 1e-12);

    // A world-Z entity is of course left alone.
    Circle flat{Vec3{}, 1.0};
    solids_apply_props(flat, drawing);
    CHECK_NEAR(flat.props().normal.z, 1.0, 1e-12);
}

TEST_CASE("solids: a reply's normals reach the entities in the first place") {
    const std::vector<std::string> reply = {
        "CIRCLE 0 2 0  0 1 0  5",
        "ARC 1 2 3  -1 0 0  0 0 1  2 0 1.5",
    };
    std::vector<EntityPtr> made;
    std::vector<std::string> notes;
    std::string error;
    REQUIRE(solids_wire_entities(reply, made, notes, error));
    REQUIRE(made.size() == 2);
    CHECK_NEAR(made[0]->props().normal.y, 1.0, 1e-12);
    CHECK_NEAR(made[1]->props().normal.x, -1.0, 1e-12);
}

TEST_CASE("solids: erase_drawn keeps its record, so UNDO can put the body back") {
    // The bug: erasing cleared the record, UNDO restored the entities, and the
    // table had forgotten they belonged to the solid -- so the body sat on
    // screen orphaned and every later boolean on it erased nothing. Handles
    // are never reused within a drawing, so keeping them is safe.
    Database db;
    SolidTable& t = SolidTable::instance();
    t.clear();
    const std::string a = t.define("SOLID @NAME@\nA\nENDSOLID\n", {});

    const Handle h1 = db.add(std::make_unique<Line>(Vec3{}, Vec3{1.0, 0.0, 0.0}));
    const Handle h2 = db.add(std::make_unique<Circle>(Vec3{}, 2.0));
    t.record_drawn(a, {h1, h2});

    CHECK(t.erase_drawn(db, a) == 2);
    CHECK(db.size() == 0);

    // Erasing again finds nothing -- but the record survives, which is the
    // point: the handles are still the solid's.
    CHECK(t.erase_drawn(db, a) == 0);

    // Now put them back the way UNDO does, under the same handles.
    db.restore(h1, std::make_unique<Line>(Vec3{}, Vec3{1.0, 0.0, 0.0}), 0);
    db.restore(h2, std::make_unique<Circle>(Vec3{}, 2.0), 1);
    REQUIRE(db.size() == 2);

    // The solid still knows them, so a later boolean consumes them properly.
    CHECK(t.erase_drawn(db, a) == 2);
    CHECK(db.size() == 0);
}
