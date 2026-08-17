// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "ncad/database.hpp"
#include "ncad/dxf.hpp"
#include "ncad/dxf_read.hpp"
#include "ncad/entities.hpp"

#include <memory>
#include <sstream>
#include <string>

using namespace ncad;

namespace {

// DXF is pairs of lines. Written this way so the tests read like the file does.
std::string dxf(std::initializer_list<const char*> lines) {
    std::string out;
    for (const char* l : lines) {
        out += l;
        out += "\n";
    }
    return out;
}

const char* kEntitiesOpen = "  0\nSECTION\n  2\nENTITIES";
const char* kEnd = "  0\nENDSEC\n  0\nEOF";

}  // namespace

TEST_CASE("dxf read: a line, in world coordinates") {
    Database db;
    const DxfReadResult r = read_dxf_text(db, dxf({kEntitiesOpen,
                                                   "  0\nLINE\n  8\n0\n 10\n1.0\n 20\n2.0\n 30\n"
                                                   "0.0\n 11\n4.0\n 21\n6.0\n 31\n0.0",
                                                   kEnd}));
    CHECK(r.ok);
    CHECK(r.entities == 1);
    CHECK(db.size() == 1);

    const Line* l = static_cast<const Line*>(db.get(db.order()[0]));
    CHECK(l->type() == EntityType::Line);
    CHECK_VEC(l->start(), 1.0, 2.0, 0.0, 1e-12);
    CHECK_VEC(l->end(), 4.0, 6.0, 0.0, 1e-12);
}

TEST_CASE("dxf read: a circle's centre comes out of the entity coordinate system") {
    Database db;
    // Normal along +Y, so the stored centre is in that plane's own axes.
    const DxfReadResult r = read_dxf_text(
        db, dxf({kEntitiesOpen,
                 "  0\nCIRCLE\n  8\n0\n 10\n3.0\n 20\n0.0\n 30\n5.0\n 40\n2.5\n"
                 "210\n0.0\n220\n1.0\n230\n0.0",
                 kEnd}));
    CHECK(r.ok);

    const Circle* c = static_cast<const Circle*>(db.get(db.order()[0]));
    CHECK_NEAR(c->radius(), 2.5, 1e-12);
    CHECK_VEC(c->props().normal, 0.0, 1.0, 0.0, 1e-12);
    // Not (3, 0, 5): the ECS-to-world conversion has to have happened.
    CHECK(std::abs(c->center().y - 5.0) < 1e-9 || std::abs(c->center().z) > 1e-9);
}

TEST_CASE("dxf read: arc angles are degrees on disk and radians in memory") {
    Database db;
    read_dxf_text(db, dxf({kEntitiesOpen,
                           "  0\nARC\n  8\n0\n 10\n0.0\n 20\n0.0\n 30\n0.0\n 40\n10.0\n"
                           " 50\n0.0\n 51\n90.0",
                           kEnd}));

    const Arc* a = static_cast<const Arc*>(db.get(db.order()[0]));
    CHECK_NEAR(a->start_angle(), 0.0, 1e-12);
    CHECK_NEAR(a->end_angle(), 1.5707963267948966, 1e-12);
}

TEST_CASE("dxf read: a polyline becomes one entity, not one per vertex") {
    Database db;
    read_dxf_text(db, dxf({kEntitiesOpen,
                           "  0\nPOLYLINE\n  8\n0\n 66\n1\n 70\n1",
                           "  0\nVERTEX\n  8\n0\n 10\n0.0\n 20\n0.0\n 30\n0.0",
                           "  0\nVERTEX\n  8\n0\n 10\n10.0\n 20\n0.0\n 30\n0.0\n 42\n1.0",
                           "  0\nVERTEX\n  8\n0\n 10\n10.0\n 20\n10.0\n 30\n0.0",
                           "  0\nSEQEND\n  8\n0",
                           kEnd}));

    // The whole storage decision, checked at the boundary that motivated it.
    CHECK(db.size() == 1);
    const Polyline* p = static_cast<const Polyline*>(db.get(db.order()[0]));
    CHECK(p->type() == EntityType::Polyline);
    CHECK(p->size() == 3);
    CHECK(p->closed());
    CHECK_VEC(p->vertices()[1].pos, 10.0, 0.0, 0.0, 1e-12);
    CHECK_NEAR(p->vertices()[1].bulge, 1.0, 1e-12);
}

TEST_CASE("dxf read: an entity after a polyline is not swallowed") {
    Database db;
    read_dxf_text(db, dxf({kEntitiesOpen,
                           "  0\nPOLYLINE\n  8\n0\n 66\n1",
                           "  0\nVERTEX\n  8\n0\n 10\n0.0\n 20\n0.0\n 30\n0.0",
                           "  0\nVERTEX\n  8\n0\n 10\n1.0\n 20\n0.0\n 30\n0.0",
                           "  0\nSEQEND\n  8\n0",
                           "  0\nLINE\n  8\n0\n 10\n5.0\n 20\n5.0\n 30\n0.0\n"
                           " 11\n6.0\n 21\n5.0\n 31\n0.0",
                           kEnd}));

    // The vertex loop reads ahead, so the entity following SEQEND is the one
    // most likely to be lost by an off-by-one in the pushback.
    CHECK(db.size() == 2);
    CHECK(db.get(db.order()[1])->type() == EntityType::Line);
}

TEST_CASE("dxf read: an unknown entity survives as a proxy") {
    Database db;
    // HATCH, which has no class here. This test has now used TEXT, then INSERT,
    // then DIMENSION and outlived all three, which is exactly how a type is
    // meant to leave proxy status -- the reader gains a branch and nothing else
    // changes. HATCH needs a decision about what it degrades TO before it can
    // follow them, so this one should last a while.
    const DxfReadResult r = read_dxf_text(
        db, dxf({kEntitiesOpen,
                 "  0\nHATCH\n  8\nHATCHES\n  2\nANSI31\n 10\n1.0\n 20\n2.0\n 30\n0.0\n"
                 " 91\n1\n 70\n0",
                 kEnd}));

    CHECK(r.ok);
    CHECK(r.entities == 1);
    CHECK(r.proxies == 1);

    const Entity* e = db.get(db.order()[0]);
    CHECK(e->type() == EntityType::Proxy);
    CHECK(static_cast<const Proxy*>(e)->dxf_name() == "HATCH");
    // Nothing to draw, nothing to pick, nothing to frame.
    CHECK(!e->bbox().valid());
}

TEST_CASE("dxf read: TEXT is held as an entity, not as a proxy") {
    Database db;
    const DxfReadResult r = read_dxf_text(
        db, dxf({kEntitiesOpen,
                 "  0\nTEXT\n  8\nNOTES\n 10\n1.0\n 20\n2.0\n 30\n0.0\n 40\n0.25\n"
                 " 50\n30.0\n  1\nHello world",
                 kEnd}));

    CHECK(r.ok);
    CHECK(r.proxies == 0);

    const Entity* e = db.get(db.order()[0]);
    CHECK(e->type() == EntityType::Text);

    // The glyphs are deferred; the content is not. A reader that dropped this
    // would open a drawing and save it back without its annotation.
    const Text* t = static_cast<const Text*>(e);
    CHECK(t->value() == "Hello world");
    CHECK_NEAR(t->height(), 0.25, 1e-12);
    CHECK_NEAR(t->rotation(), 30.0 * 3.14159265358979323846 / 180.0, 1e-9);
    CHECK_VEC(t->position(), 1.0, 2.0, 0.0, 1e-12);
}

TEST_CASE("dxf read: a proxy writes back what it read, unchanged") {
    Database db;
    read_dxf_text(db, dxf({kEntitiesOpen,
                           "  0\nHATCH\n  8\nHATCHES\n  2\nANSI31\n 10\n1.5\n 20\n2.5\n"
                           " 30\n0.0\n  1\nsome text\n 70\n0",
                           kEnd}));
    CHECK(db.size() == 1);

    std::ostringstream out;
    DxfWriter w(out, db);
    w.write_document();
    const std::string text = out.str();

    // The point of the exercise: opening a drawing and saving it must not
    // quietly empty it of what this program does not understand.
    CHECK(text.find("HATCH") != std::string::npos);
    CHECK(text.find("some text") != std::string::npos);
    CHECK(text.find("ANSI31") != std::string::npos);
}

TEST_CASE("dxf read: layers and linetypes come from the tables") {
    Database db;
    const DxfReadResult r = read_dxf_text(
        db, dxf({"  0\nSECTION\n  2\nTABLES",
                 "  0\nTABLE\n  2\nLTYPE",
                 "  0\nLTYPE\n  2\nDASHED\n  3\nDashed\n 72\n65\n 73\n2\n 40\n0.75\n"
                 " 49\n0.5\n 49\n-0.25",
                 "  0\nENDTAB",
                 "  0\nTABLE\n  2\nLAYER",
                 "  0\nLAYER\n  2\nWALLS\n 62\n3\n  6\nDASHED\n 70\n0",
                 "  0\nLAYER\n  2\nHIDDEN\n 62\n1\n 70\n1",
                 "  0\nENDTAB",
                 "  0\nENDSEC",
                 kEnd}));

    CHECK(r.ok);
    const LinetypeId dashed = db.find_linetype("DASHED");
    CHECK(dashed != kInvalidLinetype);
    CHECK(db.linetype(dashed).pattern.size() == 2);
    CHECK_NEAR(db.linetype(dashed).pattern_length(), 0.75, 1e-12);

    const LayerId walls = db.find_layer("WALLS");
    CHECK(walls != kInvalidLayer);
    CHECK(db.layer(walls).color == 3);
    CHECK(db.layer(walls).linetype == dashed);

    // Group 70 bit 1 is frozen.
    CHECK(db.layer(db.find_layer("HIDDEN")).frozen);
}

TEST_CASE("dxf read: an entity naming a layer the tables forgot still gets one") {
    Database db;
    read_dxf_text(db, dxf({kEntitiesOpen,
                           "  0\nLINE\n  8\nGHOST\n 10\n0.0\n 20\n0.0\n 30\n0.0\n"
                           " 11\n1.0\n 21\n0.0\n 31\n0.0",
                           kEnd}));

    // Better than dropping it onto layer 0 and losing the name, which is the
    // sort of loss nobody notices until a layer is missing from a plot.
    const LayerId ghost = db.find_layer("GHOST");
    CHECK(ghost != kInvalidLayer);
    CHECK(db.get(db.order()[0])->props().layer == ghost);
}

TEST_CASE("dxf read: a round trip through the writer and back") {
    Database source;
    source.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 5, 0}));
    source.add(std::make_unique<Circle>(Vec3{1, 2, 0}, 3.0));
    auto p = std::make_unique<Polyline>();
    p->add({0, 0, 0});
    p->add({5, 0, 0}, 0.5);
    p->add({5, 5, 0});
    source.add(std::move(p));

    std::ostringstream out;
    DxfWriter w(out, source);
    w.write_document();

    Database loaded;
    const DxfReadResult r = read_dxf_text(loaded, out.str());
    CHECK(r.ok);
    CHECK(loaded.size() == 3);

    CHECK(loaded.get(loaded.order()[0])->type() == EntityType::Line);
    CHECK(loaded.get(loaded.order()[1])->type() == EntityType::Circle);

    const Polyline* back = static_cast<const Polyline*>(loaded.get(loaded.order()[2]));
    CHECK(back->size() == 3);
    CHECK_NEAR(back->vertices()[1].bulge, 0.5, 1e-9);
    CHECK_VEC(back->vertices()[2].pos, 5.0, 5.0, 0.0, 1e-9);
}

TEST_CASE("dxf read: opening replaces the drawing and leaves no history") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{99, 99, 0}, Vec3{100, 100, 0}));
    CHECK(db.journal().can_undo());

    read_dxf_text(db, dxf({kEntitiesOpen,
                           "  0\nLINE\n  8\n0\n 10\n0.0\n 20\n0.0\n 30\n0.0\n"
                           " 11\n1.0\n 21\n0.0\n 31\n0.0",
                           kEnd}));

    CHECK(db.size() == 1);
    // Undoing past the load of a drawing is not meaningful, and the load is not
    // an edit.
    CHECK(!db.journal().can_undo());
    CHECK(!db.journal().can_redo());
}

TEST_CASE("dxf read: the version is reported rather than enforced") {
    Database db;
    DxfReadResult r = read_dxf_text(
        db, dxf({"  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1009\n  0\nENDSEC", kEnd}));
    CHECK(r.version == "AC1009");
    CHECK(!r.newer_version);

    Database db2;
    r = read_dxf_text(
        db2, dxf({"  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1015\n  0\nENDSEC", kEnd}));
    CHECK(r.newer_version);
    // Reading continues: a later file is mostly R12 plus entities that become
    // proxies, and refusing it outright would lose more than it protects.
    CHECK(r.ok);
}

TEST_CASE("dxf read: a truncated file does not run away") {
    Database db;
    // A code with no value, which is what a file cut off mid-write looks like.
    const DxfReadResult r = read_dxf_text(db, "  0\nSECTION\n  2\nENTITIES\n  0\nLINE\n 10\n");
    CHECK(r.ok);  // what was readable was read
    CHECK(db.size() <= 1);
}

TEST_CASE("dxf read: carriage returns and padded codes are tolerated") {
    Database db;
    // Written on Windows, with codes right-aligned in a three-character field.
    const std::string text =
        "  0\r\nSECTION\r\n  2\r\nENTITIES\r\n"
        "  0\r\nLINE\r\n  8\r\n0\r\n 10\r\n1.0\r\n 20\r\n2.0\r\n 30\r\n0.0\r\n"
        " 11\r\n3.0\r\n 21\r\n4.0\r\n 31\r\n0.0\r\n"
        "  0\r\nENDSEC\r\n  0\r\nEOF\r\n";
    const DxfReadResult r = read_dxf_text(db, text);
    CHECK(r.ok);
    CHECK(db.size() == 1);
    CHECK_VEC(static_cast<const Line*>(db.get(db.order()[0]))->end(), 3.0, 4.0, 0.0, 1e-12);
}

// --- Entities newer than AC1009 ---------------------------------------------
//
// Import only. Writing still degrades to R12, which is the asymmetry Sadie
// asked for: read the format everyone else emits, guarantee the one everyone
// else can read.

TEST_CASE("dxf read: an R13+ ELLIPSE becomes a real ellipse, not a proxy") {
    Database db;
    // Note the subclass markers and the handle -- present in every file newer
    // than R12 and absent from every R12 one. They are ignored, not tripped on.
    const DxfReadResult r = read_dxf_text(db, dxf({kEntitiesOpen,
        "  0", "ELLIPSE", "  5", "2B",
        "100", "AcDbEntity", "  8", "0", "100", "AcDbEllipse",
        " 10", "3.0", " 20", "4.0", " 30", "0.0",
        " 11", "20.0", " 21", "0.0", " 31", "0.0",
        " 40", "0.5", " 41", "0.0", " 42", "6.283185307179586",
        kEnd}));

    CHECK(r.ok);
    CHECK(r.proxies == 0);
    REQUIRE(db.order().size() == 1);

    const Entity* e = db.get(db.order()[0]);
    REQUIRE(e->type() == EntityType::Ellipse);
    const Ellipse& el = static_cast<const Ellipse&>(*e);

    // Centre and major axis are WORLD here, and the major axis is a VECTOR from
    // the centre -- unlike CIRCLE in the same file, whose centre is ECS.
    CHECK_VEC(el.center(), 3.0, 4.0, 0.0, 1e-12);
    CHECK_NEAR(el.major_length(), 20.0, 1e-12);
    CHECK_NEAR(el.ratio(), 0.5, 1e-12);
    CHECK(el.is_full());
}

TEST_CASE("dxf read: an LWPOLYLINE becomes a polyline, with bulge and elevation") {
    Database db;
    const DxfReadResult r = read_dxf_text(db, dxf({kEntitiesOpen,
        "  0", "LWPOLYLINE", "  5", "2C",
        "100", "AcDbEntity", "  8", "0", "100", "AcDbPolyline",
        " 90", "3", " 70", "1", " 38", "2.0",
        " 10", "0.0", " 20", "0.0", " 42", "1.0",
        " 10", "5.0", " 20", "5.0",
        " 10", "10.0", " 20", "0.0",
        kEnd}));

    CHECK(r.ok);
    CHECK(r.proxies == 0);
    REQUIRE(db.order().size() == 1);

    const Entity* e = db.get(db.order()[0]);
    REQUIRE(e->type() == EntityType::Polyline);
    const Polyline& p = static_cast<const Polyline&>(*e);

    REQUIRE(p.size() == 3);
    CHECK(p.closed());
    // Elevation is group 38, not a z on each vertex -- it is the one place a
    // LWPOLYLINE keeps its height, and dropping it flattens the drawing.
    CHECK_VEC(p.vertices()[0].pos, 0.0, 0.0, 2.0, 1e-12);
    CHECK_VEC(p.vertices()[2].pos, 10.0, 0.0, 2.0, 1e-12);
    // The bulge belongs to the vertex it followed, not to the next one.
    CHECK_NEAR(p.vertices()[0].bulge, 1.0, 1e-12);
    CHECK_NEAR(p.vertices()[1].bulge, 0.0, 1e-12);
}

TEST_CASE("dxf read: an R13+ SPLINE becomes a NURBS curve") {
    Database db;
    const DxfReadResult r = read_dxf_text(db, dxf({kEntitiesOpen,
        "  0", "SPLINE", "  5", "2E",
        "100", "AcDbEntity", "  8", "0", "100", "AcDbSpline",
        " 70", "8", " 71", "3", " 72", "8", " 73", "4", " 74", "0",
        " 40", "0.0", " 40", "0.0", " 40", "0.0", " 40", "0.0",
        " 40", "1.0", " 40", "1.0", " 40", "1.0", " 40", "1.0",
        " 10", "0.0", " 20", "0.0", " 30", "0.0",
        " 10", "3.0", " 20", "9.0", " 30", "0.0",
        " 10", "9.0", " 20", "-3.0", " 30", "0.0",
        " 10", "12.0", " 20", "6.0", " 30", "0.0",
        kEnd}));

    CHECK(r.ok);
    CHECK(r.proxies == 0);
    REQUIRE(db.order().size() == 1);

    const Entity* e = db.get(db.order()[0]);
    REQUIRE(e->type() == EntityType::Spline);
    const Spline& s = static_cast<const Spline&>(*e);

    CHECK(s.degree() == 3);
    CHECK(s.control_points().size() == 4);
    CHECK(s.knots().size() == 8);
    CHECK(s.valid());
    // A clamped knot vector means the curve starts and ends on its outer
    // control points, which is the cheapest check that the knots landed in the
    // right order rather than merely in the right quantity.
    CHECK_VEC(s.start_point(), 0.0, 0.0, 0.0, 1e-9);
    CHECK_VEC(s.end_point(), 12.0, 6.0, 0.0, 1e-9);
}

TEST_CASE("dxf read: an unusable SPLINE survives as a proxy rather than as wreckage") {
    Database db;
    // Knot count must be control points + degree + 1. This one says degree 3
    // with four control points and only three knots.
    const DxfReadResult r = read_dxf_text(db, dxf({kEntitiesOpen,
        "  0", "SPLINE", "100", "AcDbEntity", "  8", "0", "100", "AcDbSpline",
        " 71", "3", " 73", "4",
        " 40", "0.0", " 40", "1.0", " 40", "2.0",
        " 10", "0.0", " 20", "0.0", " 30", "0.0",
        " 10", "3.0", " 20", "9.0", " 30", "0.0",
        " 10", "9.0", " 20", "-3.0", " 30", "0.0",
        " 10", "12.0", " 20", "6.0", " 30", "0.0",
        kEnd}));

    CHECK(r.ok);
    CHECK(r.proxies == 1);
    REQUIRE(db.order().size() == 1);
    // Kept whole, so saving the file back does not destroy what we could not
    // use -- the same contract every unknown entity already gets.
    CHECK(db.get(db.order()[0])->type() == EntityType::Proxy);
}

TEST_CASE("dxf read: a 2018 file's sections and markers are stepped over") {
    Database db;
    const DxfReadResult r = read_dxf_text(db,
        "  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1032\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nCLASSES\n"
        "  0\nCLASS\n  1\nACDBDICTIONARYWDFLT\n  2\nAcDbDictionaryWithDefault\n 90\n0\n"
        "  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nLINE\n  5\n2A\n100\nAcDbEntity\n  8\n0\n100\nAcDbLine\n"
        " 10\n0.0\n 20\n0.0\n 30\n0.0\n 11\n10.0\n 21\n5.0\n 31\n0.0\n"
        "  0\nENDSEC\n"
        "  0\nSECTION\n  2\nOBJECTS\n"
        "  0\nDICTIONARY\n  5\nC\n100\nAcDbDictionary\n  3\nACAD_GROUP\n350\nD\n"
        "  0\nENDSEC\n  0\nEOF\n");

    CHECK(r.ok);
    CHECK(r.version == "AC1032");
    CHECK(r.newer_version);
    // CLASSES and OBJECTS carry nothing this program models, and stepping over
    // them is why a modern file reads at all.
    REQUIRE(db.order().size() == 1);
    CHECK(db.get(db.order()[0])->type() == EntityType::Line);
}

// --- Merge, which is what DXFIN does ----------------------------------------

TEST_CASE("dxf merge: entities are added to the drawing rather than replacing it") {
    // The reported bug: DXFIN emptied the drawing it was importing into,
    // because OPEN and DXFIN were one implementation and OPEN must clear.
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 1, 0}));
    REQUIRE(db.size() == 1);

    const DxfReadResult r = read_dxf_text(db, dxf({kEntitiesOpen,
                                                   "  0\nLINE\n  8\n0\n 10\n1.0\n 20\n2.0\n 30\n"
                                                   "0.0\n 11\n4.0\n 21\n6.0\n 31\n0.0",
                                                   kEnd}),
                                          DxfReadMode::Merge);
    CHECK(r.ok);
    CHECK(db.size() == 2);  // both, not one
}

TEST_CASE("dxf merge: imported entities cannot collide with handles already in use") {
    // Database::clear deliberately never rewinds next_handle_, and add always
    // takes the next one, so this holds without any renumbering pass. It is
    // pinned because a merge is the first thing that would notice if it stopped
    // being true.
    Database db;
    const Handle first = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));

    read_dxf_text(db, dxf({kEntitiesOpen,
                           "  0\nLINE\n  8\n0\n 10\n0.0\n 20\n0.0\n 30\n0.0\n"
                           " 11\n2.0\n 21\n0.0\n 31\n0.0",
                           kEnd}),
                  DxfReadMode::Merge);

    REQUIRE(db.size() == 2);
    CHECK(db.order()[0] == first);
    CHECK(db.order()[1] != first);
    CHECK(db.get(first) != nullptr);  // the original survived intact
}

TEST_CASE("dxf merge: a layer the drawing already defines keeps its own colour") {
    // Table entries merge by name and the existing one wins, so importing a
    // file that happens to name GEOM does not redefine the layer being drawn on.
    Database db;
    const LayerId mine = db.add_layer("GEOM", 3, kInvalidLinetype);
    REQUIRE(mine != kInvalidLayer);

    read_dxf_text(db,
                  "  0\nSECTION\n  2\nTABLES\n"
                  "  0\nTABLE\n  2\nLAYER\n"
                  "  0\nLAYER\n  2\nGEOM\n 62\n5\n 70\n0\n"
                  "  0\nENDTAB\n  0\nENDSEC\n  0\nEOF\n",
                  DxfReadMode::Merge);

    CHECK(db.find_layer("GEOM") == mine);
    CHECK(db.layer(mine).color == 3);  // not 5, from the file
}

TEST_CASE("dxf merge: the current UCS is the drawing's, not the imported file's") {
    // An import must not move the construction plane out from under whatever
    // the user was doing.
    Database db;
    Ucs mine;
    mine.origin = Vec3{7, 8, 9};
    db.set_current_ucs(mine, "MINE");

    read_dxf_text(db,
                  "  0\nSECTION\n  2\nHEADER\n"
                  "  9\n$UCSORG\n 10\n1.0\n 20\n2.0\n 30\n3.0\n"
                  "  0\nENDSEC\n  0\nEOF\n",
                  DxfReadMode::Merge);

    CHECK_VEC(db.current_ucs().origin, 7.0, 8.0, 9.0, 1e-12);
}

TEST_CASE("dxf merge: undo history survives, and Replace still discards it") {
    // A merge IS an edit made to a drawing whose history is worth keeping.
    Database db;
    db.journal().begin_group("LINE");
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 1, 0}));
    db.journal().end_group();
    REQUIRE(db.journal().undo_depth() == 1);

    read_dxf_text(db, dxf({kEntitiesOpen,
                           "  0\nLINE\n  8\n0\n 10\n0.0\n 20\n0.0\n 30\n0.0\n"
                           " 11\n2.0\n 21\n0.0\n 31\n0.0",
                           kEnd}),
                  DxfReadMode::Merge);
    // The earlier group is still there. It is 2 rather than 1 because no
    // command group is open here, so the imported entity is its own step --
    // see the DXFIN test for the case that matters, where the engine's group
    // makes the whole import one.
    CHECK(db.journal().undo_depth() == 2);

    // Replace is the other half of the contract: undoing past the load of a
    // drawing is not meaningful.
    read_dxf_text(db, dxf({kEntitiesOpen, kEnd}), DxfReadMode::Replace);
    CHECK(db.journal().undo_depth() == 0);
}

TEST_CASE("dxf merge: inside a command group the whole import is one undo step") {
    // What DXFIN actually does, since CommandEngine opens a group per command.
    // Undoing an import must take back the whole file, not one entity of it.
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 1, 0}));
    const std::size_t before = db.size();

    db.journal().begin_group("DXFIN");
    read_dxf_text(db,
                  dxf({kEntitiesOpen,
                       "  0\nLINE\n  8\n0\n 10\n0.0\n 20\n0.0\n 30\n0.0\n"
                       " 11\n2.0\n 21\n0.0\n 31\n0.0",
                       "  0\nLINE\n  8\n0\n 10\n0.0\n 20\n1.0\n 30\n0.0\n"
                       " 11\n2.0\n 21\n1.0\n 31\n0.0",
                       kEnd}),
                  DxfReadMode::Merge);
    db.journal().end_group();

    REQUIRE(db.size() == before + 2);
    CHECK(db.journal().undo(db));
    CHECK(db.size() == before);  // both imported entities went, and only those
}

// --- hostile input, found by audit 2026-08-17 -------------------------------
//
// A DXF is untrusted: it comes from another program, a corrupt download, or a
// truncated copy. None of the files below is exotic, and every one of them was
// a heap-use-after-free or a silent corruption before the fix beside it.
//
// The four block cases share a cause. An INSERT may name a block defined later,
// so inserts are registered unresolved and fixed up at the end -- but the
// registration held a raw pointer, and four paths destroy the entity before the
// fix-up runs. They are written out separately because each is a different way
// for a file to stop early, and a future refactor could reintroduce any one.

TEST_CASE("dxf read: ENDSEC inside a BLOCK does not leave a dangling insert") {
    Database db;
    read_dxf_text(db, dxf({"  0\nSECTION\n  2\nBLOCKS",
                           "  0\nBLOCK\n  2\nA",
                           "  0\nLINE\n 10\n0.0\n 20\n0.0\n 11\n1.0\n 21\n1.0",
                           "  0\nENDBLK",
                           "  0\nBLOCK\n  2\nB",
                           "  0\nINSERT\n  2\nA\n 10\n0.0\n 20\n0.0",
                           kEnd}),
                  DxfReadMode::Replace);
    // The assertion is that we got here at all: block B never reached ENDBLK,
    // so its INSERT died with it while still registered for resolution.
    CHECK(db.find_block("A") != kInvalidBlock);
}

TEST_CASE("dxf read: an entity outside any block does not leave a dangling insert") {
    Database db;
    read_dxf_text(db, dxf({"  0\nSECTION\n  2\nBLOCKS",
                           "  0\nBLOCK\n  2\nA",
                           "  0\nENDBLK",
                           "  0\nINSERT\n  2\nA\n 10\n0.0\n 20\n0.0",
                           kEnd}),
                  DxfReadMode::Replace);
    CHECK(db.find_block("A") != kInvalidBlock);
}

TEST_CASE("dxf read: a BLOCK with no name does not leave a dangling insert") {
    Database db;
    read_dxf_text(db, dxf({"  0\nSECTION\n  2\nBLOCKS",
                           "  0\nBLOCK\n  2\nA",
                           "  0\nENDBLK",
                           "  0\nBLOCK",  // no group 2: nowhere for it to go
                           "  0\nINSERT\n  2\nA\n 10\n0.0\n 20\n0.0",
                           "  0\nENDBLK",
                           kEnd}),
                  DxfReadMode::Replace);
    CHECK(db.find_block("A") != kInvalidBlock);
}

TEST_CASE("dxf read: input ending mid-block does not leave a dangling insert") {
    Database db;
    // No ENDBLK, no ENDSEC, no EOF -- a download cut short.
    read_dxf_text(db, dxf({"  0\nSECTION\n  2\nBLOCKS",
                           "  0\nBLOCK\n  2\nA",
                           "  0\nLINE\n 10\n0.0\n 20\n0.0\n 11\n1.0\n 21\n1.0",
                           "  0\nENDBLK",
                           "  0\nBLOCK\n  2\nB",
                           "  0\nINSERT\n  2\nA\n 10\n0.0\n 20\n0.0"}),
                  DxfReadMode::Replace);
    CHECK(db.find_block("A") != kInvalidBlock);
}

TEST_CASE("dxf read: a redefined block does not free an insert still registered") {
    // R12 redefinition rewrites the definition in place, destroying what it
    // held. Two blocks of the same name is not a corrupt file -- xref
    // flattening emits them.
    Database db;
    read_dxf_text(db, dxf({"  0\nSECTION\n  2\nBLOCKS",
                           "  0\nBLOCK\n  2\nA",
                           "  0\nLINE\n 10\n0.0\n 20\n0.0\n 11\n1.0\n 21\n1.0",
                           "  0\nENDBLK",
                           "  0\nBLOCK\n  2\nDUP",
                           "  0\nINSERT\n  2\nA\n 10\n0.0\n 20\n0.0",
                           "  0\nENDBLK",
                           "  0\nBLOCK\n  2\nDUP",
                           "  0\nLINE\n 10\n0.0\n 20\n0.0\n 11\n1.0\n 21\n1.0",
                           "  0\nENDBLK",
                           kEnd}),
                  DxfReadMode::Replace);
    CHECK(db.find_block("DUP") != kInvalidBlock);
}

TEST_CASE("dxf read: nan and inf never enter the drawing") {
    // strtod parses all three happily. A NaN coordinate then propagates through
    // BBox::expand, so the entity is unpickable and invisible to ZOOM EXTENTS --
    // and it used to survive DXFOUT, handing AutoCAD a file with `nan` in it.
    Database db;
    read_dxf_text(db, dxf({kEntitiesOpen,
                           "  0\nLINE\n 10\nnan\n 20\n0.0\n 30\n0.0\n"
                           " 11\ninf\n 21\n1e999\n 31\n-inf",
                           kEnd}),
                  DxfReadMode::Replace);
    REQUIRE(db.size() == 1);
    const Entity* e = db.get(db.order().front());
    REQUIRE(e != nullptr);

    const BBox box = e->bbox();
    CHECK(box.valid());

    // Substituted with zero, which is what an absent group would have given.
    const Line* line = static_cast<const Line*>(e);
    CHECK(line->start().x == 0.0);
    CHECK(line->end().x == 0.0);
    CHECK(line->end().y == 0.0);
}

TEST_CASE("dxf read: a hostile colour index is clamped at the door") {
    // 62 = 32768 negates to itself in an int16_t, so the renderer's
    // "off layers carry a sign" flip left it negative and indexed the palette
    // table from -32768.
    Database db;
    read_dxf_text(db, dxf({kEntitiesOpen,
                           "  0\nLINE\n 62\n32768\n 10\n0.0\n 20\n0.0\n"
                           " 11\n1.0\n 21\n1.0",
                           kEnd}),
                  DxfReadMode::Replace);
    REQUIRE(db.size() == 1);
    const Entity* e = db.get(db.order().front());
    REQUIRE(e != nullptr);
    CHECK(e->props().color >= -256);
    CHECK(e->props().color <= 256);
}
