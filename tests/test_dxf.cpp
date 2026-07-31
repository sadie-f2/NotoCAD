// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "ncad/database.hpp"
#include "ncad/dxf.hpp"
#include "ncad/ecs.hpp"
#include "ncad/entities.hpp"

#include "ncad/dxf_read.hpp"
#include "ncad/sysvar.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

using namespace ncad;

namespace {

constexpr double kPi = std::numbers::pi;

struct Pair {
    int code;
    std::string value;
};

// Minimal DXF group reader, used to check our own output structurally rather
// than by string matching.
std::vector<Pair> parse(const std::string& text, bool* well_formed) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }

    *well_formed = (lines.size() % 2 == 0);
    std::vector<Pair> pairs;
    for (std::size_t i = 0; i + 1 < lines.size(); i += 2) {
        try {
            std::size_t consumed = 0;
            const int code = std::stoi(lines[i], &consumed);
            if (consumed != lines[i].size()) *well_formed = false;
            pairs.push_back({code, lines[i + 1]});
        } catch (...) {
            *well_formed = false;
        }
    }
    return pairs;
}

std::string dump(const Database& db) {
    std::ostringstream out;
    DxfWriter w(out, db);
    w.write_document();
    return out.str();
}

// Finds the value of `code` within the entity block starting at `start`.
std::string value_in_entity(const std::vector<Pair>& p, std::size_t start, int code) {
    for (std::size_t i = start + 1; i < p.size(); ++i) {
        if (p[i].code == 0) break;  // next entity
        if (p[i].code == code) return p[i].value;
    }
    return {};
}

std::size_t find_entity(const std::vector<Pair>& p, const std::string& type) {
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i].code == 0 && p[i].value == type) return i;
    }
    return p.size();
}

bool has_pair(const std::vector<Pair>& p, int code, const std::string& value) {
    for (const Pair& q : p) {
        if (q.code == code && q.value == value) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("dxf: real formatting always yields a parseable real") {
    CHECK(dxf_real(0.0) == "0.0");
    CHECK(dxf_real(1.0) == "1.0");
    CHECK(dxf_real(-3.0) == "-3.0");
    CHECK(dxf_real(0.5) == "0.5");
    // Round-trips exactly.
    CHECK(std::stod(dxf_real(1.0 / 3.0)) == 1.0 / 3.0);
    CHECK(std::stod(dxf_real(1e-15)) == 1e-15);
}

TEST_CASE("dxf: document is structurally well formed") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    const std::string text = dump(db);
    bool ok = false;
    const std::vector<Pair> p = parse(text, &ok);
    CHECK(ok);  // even line count, every code an integer

    // Line endings are CRLF, as DXF requires.
    CHECK(text.find("\r\n") != std::string::npos);
    CHECK(text.find("\n\n") == std::string::npos);

    // Sections appear, in order, and the file terminates properly.
    CHECK(has_pair(p, 2, "HEADER"));
    CHECK(has_pair(p, 2, "TABLES"));
    CHECK(has_pair(p, 2, "BLOCKS"));
    CHECK(has_pair(p, 2, "ENTITIES"));
    CHECK(has_pair(p, 1, "AC1009"));  // R12
    CHECK(!p.empty() && p.back().code == 0 && p.back().value == "EOF");

    // Every SECTION is closed.
    int depth = 0, max_depth = 0;
    for (const Pair& q : p) {
        if (q.code == 0 && q.value == "SECTION") { ++depth; max_depth = std::max(max_depth, depth); }
        if (q.code == 0 && q.value == "ENDSEC") --depth;
    }
    CHECK(depth == 0);
    CHECK(max_depth == 1);  // sections never nest
}

TEST_CASE("dxf: required tables are present") {
    const Database db;
    bool ok = false;
    const std::vector<Pair> p = parse(dump(db), &ok);
    CHECK(ok);

    CHECK(has_pair(p, 2, "LTYPE"));
    CHECK(has_pair(p, 2, "LAYER"));
    CHECK(has_pair(p, 2, "UCS"));
    CHECK(has_pair(p, 2, "STYLE"));
    CHECK(has_pair(p, 2, "APPID"));
    CHECK(has_pair(p, 2, "CONTINUOUS"));
    CHECK(has_pair(p, 2, "0"));  // layer "0" must exist

    // Five now that named coordinate systems have a table of their own. It is
    // written even when empty, because a reader expecting the table and not
    // finding it fails worse than one finding it empty.
    int endtabs = 0;
    for (const Pair& q : p) {
        if (q.code == 0 && q.value == "ENDTAB") ++endtabs;
    }
    CHECK(endtabs == 5);
}

TEST_CASE("dxf: LINE stores world coordinates") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{1, 2, 3}, Vec3{4, 5, 6}));

    bool ok = false;
    const std::vector<Pair> p = parse(dump(db), &ok);
    const std::size_t i = find_entity(p, "LINE");
    CHECK(i < p.size());

    // R12 LINE is the exception to ECS storage: both endpoints are in WCS.
    CHECK_NEAR(std::stod(value_in_entity(p, i, 10)), 1.0, 1e-12);
    CHECK_NEAR(std::stod(value_in_entity(p, i, 20)), 2.0, 1e-12);
    CHECK_NEAR(std::stod(value_in_entity(p, i, 30)), 3.0, 1e-12);
    CHECK_NEAR(std::stod(value_in_entity(p, i, 11)), 4.0, 1e-12);
    CHECK_NEAR(std::stod(value_in_entity(p, i, 21)), 5.0, 1e-12);
    CHECK_NEAR(std::stod(value_in_entity(p, i, 31)), 6.0, 1e-12);
    CHECK(value_in_entity(p, i, 8) == "0");  // default layer
}

TEST_CASE("dxf: a tilted CIRCLE round-trips through the entity coordinate system") {
    // This is the test the whole ECS design exists for. The centre is written in
    // ECS coordinates plus an extrusion vector; reading it back and converting to
    // world must recover the original point exactly.
    const Vec3 center{5, 3, 2};
    const Vec3 normal{1, 1, 1};

    Database db;
    db.add(std::make_unique<Circle>(center, 4.0, normal));

    bool ok = false;
    const std::vector<Pair> p = parse(dump(db), &ok);
    const std::size_t i = find_entity(p, "CIRCLE");
    CHECK(i < p.size());

    const Vec3 ocs{std::stod(value_in_entity(p, i, 10)),
                   std::stod(value_in_entity(p, i, 20)),
                   std::stod(value_in_entity(p, i, 30))};
    const Vec3 ext{std::stod(value_in_entity(p, i, 210)),
                   std::stod(value_in_entity(p, i, 220)),
                   std::stod(value_in_entity(p, i, 230))};

    CHECK_NEAR(std::stod(value_in_entity(p, i, 40)), 4.0, 1e-12);
    CHECK_VEC(ext, normalize(normal).x, normalize(normal).y, normalize(normal).z, 1e-12);

    const Vec3 recovered = ecs_to_world(ext).transform_point(ocs);
    CHECK_VEC(recovered, center.x, center.y, center.z, 1e-9);
}

TEST_CASE("dxf: a world-plane circle omits the extrusion vector") {
    Database db;
    db.add(std::make_unique<Circle>(Vec3{1, 2, 0}, 3.0));

    bool ok = false;
    const std::vector<Pair> p = parse(dump(db), &ok);
    const std::size_t i = find_entity(p, "CIRCLE");
    // Group 210 defaults to 0,0,1 and is left out, matching what R12 writes.
    CHECK(value_in_entity(p, i, 210).empty());
}

TEST_CASE("dxf: ARC writes angles in degrees") {
    Database db;
    db.add(std::make_unique<Arc>(Vec3{0, 0, 0}, 2.0, 0.0, kPi / 2.0));

    bool ok = false;
    const std::vector<Pair> p = parse(dump(db), &ok);
    const std::size_t i = find_entity(p, "ARC");
    CHECK(i < p.size());
    CHECK_NEAR(std::stod(value_in_entity(p, i, 50)), 0.0, 1e-9);
    CHECK_NEAR(std::stod(value_in_entity(p, i, 51)), 90.0, 1e-9);
    CHECK_NEAR(std::stod(value_in_entity(p, i, 40)), 2.0, 1e-12);
}

TEST_CASE("dxf: entity properties are emitted only when non-default") {
    Database db;
    const LayerId walls = db.add_layer("WALLS", 3);

    auto plain = std::make_unique<Line>(Vec3{}, Vec3{1, 0, 0});
    db.add(std::move(plain));

    auto styled = std::make_unique<Line>(Vec3{}, Vec3{0, 1, 0});
    styled->props().layer = walls;
    styled->props().color = 5;
    styled->props().thickness = 0.5;
    const Handle h = db.add(std::move(styled));

    bool ok = false;
    const std::vector<Pair> p = parse(dump(db), &ok);

    const std::size_t first = find_entity(p, "LINE");
    CHECK(value_in_entity(p, first, 62).empty());  // BYLAYER is implied
    CHECK(value_in_entity(p, first, 39).empty());  // zero thickness omitted

    // The styled line is the second LINE block.
    std::size_t second = first + 1;
    while (second < p.size() && !(p[second].code == 0 && p[second].value == "LINE")) ++second;
    CHECK(second < p.size());
    CHECK(value_in_entity(p, second, 8) == "WALLS");
    CHECK(value_in_entity(p, second, 62) == "5");
    CHECK_NEAR(std::stod(value_in_entity(p, second, 39)), 0.5, 1e-12);
    // No handle. R12 made them optional and we no longer write any -- see the
    // header comment in dxf.cpp, and the test below for why.
    CHECK(value_in_entity(p, second, 5).empty());
    CHECK(h != kNullHandle);
}

TEST_CASE("dxf: output is deterministic") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 1, 1}));
    db.add(std::make_unique<Circle>(Vec3{2, 2, 2}, 1.0, Vec3{1, 0, 1}));
    db.add(std::make_unique<Arc>(Vec3{0, 0, 0}, 1.0, 0.1, 1.2));

    CHECK(dump(db) == dump(db));
}

// --- what made AutoCAD call the file corrupt --------------------------------

TEST_CASE("dxf: no handles are written, because ours could not be unique") {
    // A POLYLINE's VERTEX and SEQEND records are not database entities and have
    // no handles of their own, so they were emitted carrying the PARENT'S. A
    // degraded ellipse wrote eighteen records all claiming handle 6. Handles
    // must be unique, and $HANDLING = 1 told the reader to check -- so AutoCAD
    // rejected the file outright.
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 10, 0}));
    db.add(std::make_unique<Ellipse>(Vec3{20, 0, 0}, Vec3{10, 0, 0}, 0.5));
    db.add(Spline::interpolating({{0, 50, 0}, {10, 60, 0}, {20, 45, 0}, {30, 55, 0}}));

    const std::string out = dump(db);
    bool ok = true;
    const std::vector<Pair> p = parse(out, &ok);
    CHECK(ok);

    std::size_t handles = 0;
    for (const Pair& g : p) {
        if (g.code == 5) ++handles;
    }
    CHECK(handles == 0);

    // And the header must not claim they are there.
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        if (p[i].code == 9 && p[i].value == "$HANDLING") CHECK(p[i + 1].value == "0");
        CHECK(!(p[i].code == 9 && p[i].value == "$HANDSEED"));
    }
}

TEST_CASE("dxf: every VERTEX sits inside a POLYLINE that a SEQEND closes") {
    // The structure a reader walks. An entity appearing between a POLYLINE and
    // its SEQEND, or a SEQEND with no POLYLINE open, is the other way a file is
    // called corrupt -- and the degrading entities are the ones that emit these
    // records, so they are the ones worth pinning.
    Database db;
    db.add(std::make_unique<Ellipse>(Vec3{20, 0, 0}, Vec3{10, 0, 0}, 0.5));
    db.add(Spline::interpolating({{0, 50, 0}, {10, 60, 0}, {20, 45, 0}, {30, 55, 0}}));
    {
        auto poly = std::make_unique<Polyline>();
        poly->add({0, 70, 0});
        poly->add({10, 75, 0}, 1.0);
        poly->add({20, 70, 0});
        db.add(std::move(poly));
    }
    db.add(std::make_unique<MText>(Vec3{0, 90, 0}, "one\\Ptwo", 2.0));

    bool ok = true;
    const std::vector<Pair> p = parse(dump(db), &ok);
    CHECK(ok);

    bool in_poly = false;
    std::size_t polylines = 0;
    std::size_t seqends = 0;
    for (const Pair& g : p) {
        if (g.code != 0) continue;
        if (g.value == "POLYLINE") {
            CHECK(!in_poly);  // never nested
            in_poly = true;
            ++polylines;
        } else if (g.value == "SEQEND") {
            CHECK(in_poly);  // never orphaned
            in_poly = false;
            ++seqends;
        } else if (g.value == "VERTEX") {
            CHECK(in_poly);  // never loose
        } else if (in_poly && g.value != "ENDSEC") {
            CHECK(false);  // no other entity may interrupt the sequence
        }
    }
    CHECK(!in_poly);
    CHECK(polylines == 3);
    CHECK(polylines == seqends);
}

// --- writing a later revision -----------------------------------------------

namespace {

std::string dump_as(const Database& db, DxfVersion v) {
    std::ostringstream out;
    DxfWriter w(out, db, v);
    w.set_handle_seed_hint(dxf_count_handles(db, v));
    w.write_document();
    return out.str();
}

void add_one_of_each(Database& db) {
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 10, 0}));
    db.add(std::make_unique<Ellipse>(Vec3{20, 0, 0}, Vec3{10, 0, 0}, 0.5));
    db.add(Spline::interpolating({{0, 50, 0}, {10, 60, 0}, {20, 45, 0}, {30, 55, 0}}));
    db.add(std::make_unique<MText>(Vec3{0, 90, 0}, "one\\Ptwo", 2.0, 20.0));

    // A REAL polyline, and its absence here is why AutoCAD found two faults
    // this suite could not. A POLYLINE is the only entity that writes
    // subordinate records -- VERTEX and SEQEND -- and those need handles and
    // subclass markers of their own at R2000. With no polyline in the fixture,
    // every structural check below walked past the records that were wrong.
    // It is the same blind spot that hid the R12 handle bug, where the sample
    // drawing had no polylines either.
    auto pl = std::make_unique<Polyline>();
    pl->add({100, 0, 0});
    pl->add({110, 0, 0});
    pl->add({120, 10, 0});
    pl->vertices()[1].bulge = 0.5;  // an arc segment, so the bulge travels too
    db.add(std::move(pl));
}

std::size_t count_records(const std::vector<Pair>& p, const char* type) {
    std::size_t n = 0;
    for (const Pair& g : p) {
        if (g.code == 0 && g.value == type) ++n;
    }
    return n;
}

}  // namespace

TEST_CASE("dxf r2000: the three degrading entities are written as themselves") {
    Database db;
    add_one_of_each(db);

    bool ok = true;
    const std::vector<Pair> p = parse(dump_as(db, DxfVersion::R2000), &ok);
    CHECK(ok);

    // The whole reason a later revision is worth offering.
    CHECK(count_records(p, "ELLIPSE") == 1);
    CHECK(count_records(p, "SPLINE") == 1);
    CHECK(count_records(p, "MTEXT") == 1);
    // Exactly ONE polyline: the real one. The curves are not tessellated any
    // more, so the only VERTEX records are that polyline's three.
    CHECK(count_records(p, "POLYLINE") == 1);
    CHECK(count_records(p, "VERTEX") == 3);

    // R12 still degrades all three, which is the interchange guarantee.
    const std::vector<Pair> r12 = parse(dump_as(db, DxfVersion::R12), &ok);
    CHECK(count_records(r12, "ELLIPSE") == 0);
    CHECK(count_records(r12, "SPLINE") == 0);
    CHECK(count_records(r12, "MTEXT") == 0);
    // The real one, plus the ellipse and the spline standing in as polylines.
    CHECK(count_records(r12, "POLYLINE") == 3);
    CHECK(count_records(r12, "TEXT") == 2);  // the mtext, one per line
}

TEST_CASE("dxf r2000: a round trip through our own writer is LOSSLESS") {
    // The claim the revision exists to make. At R12 an ellipse comes back a
    // polyline and nothing recovers it -- which is what cost an afternoon.
    Database db;
    add_one_of_each(db);
    db.sysvars().set_string(Sysvar::DxfVersionVar, "R2000");

    Database back;
    const DxfReadResult r = read_dxf_text(back, dump_as(db, DxfVersion::R2000));
    CHECK(r.ok);
    CHECK(r.proxies == 0);

    std::map<EntityType, int> kinds;
    for (const Handle h : back.order()) ++kinds[back.get(h)->type()];

    CHECK(kinds[EntityType::Line] == 1);
    CHECK(kinds[EntityType::Ellipse] == 1);
    CHECK(kinds[EntityType::Spline] == 1);
    CHECK(kinds[EntityType::MText] == 1);
    // One polyline, and it is the one that went in as a polyline. The ellipse
    // and the spline came back as themselves, so nothing degraded -- which is
    // what this count means now that the fixture holds a real one.
    CHECK(kinds[EntityType::Polyline] == 1);

    // And the geometry survived, not merely the type.
    for (const Handle h : back.order()) {
        const Entity* e = back.get(h);
        if (e->type() == EntityType::Ellipse) {
            const Ellipse& el = static_cast<const Ellipse&>(*e);
            CHECK_NEAR(el.major_length(), 10.0, 1e-9);
            CHECK_NEAR(el.ratio(), 0.5, 1e-9);
        } else if (e->type() == EntityType::Spline) {
            CHECK(static_cast<const Spline&>(*e).valid());
            CHECK(static_cast<const Spline&>(*e).degree() == 3);
        } else if (e->type() == EntityType::MText) {
            // The RAW string, inline codes intact.
            CHECK(static_cast<const MText&>(*e).text() == "one\\Ptwo");
        }
    }
}

TEST_CASE("dxf r2000: handles are unique and every owner resolves") {
    // R13 and later make handles mandatory, which is what R12 does not. The
    // failure this guards is the one AutoCAD rejected our R12 files for.
    Database db;
    add_one_of_each(db);

    bool ok = true;
    const std::vector<Pair> p = parse(dump_as(db, DxfVersion::R2000), &ok);
    CHECK(ok);

    std::set<std::string> allocated;
    std::vector<std::string> owners;
    std::string seed;
    bool in_header = false;
    std::size_t handle_records = 0;

    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i].code == 0 && p[i].value == "SECTION" && i + 1 < p.size()) {
            in_header = (p[i + 1].value == "HEADER");
        }
        if (p[i].code == 9 && p[i].value == "$HANDSEED" && i + 1 < p.size()) seed = p[i + 1].value;
        else if (p[i].code == 5 && !in_header) {
            ++handle_records;
            allocated.insert(p[i].value);
        } else if (p[i].code == 330) {
            owners.push_back(p[i].value);
        }
    }

    CHECK(handle_records > 0);
    CHECK(allocated.size() == handle_records);  // no duplicates anywhere

    // $HANDSEED is the NEXT handle to issue and must clear every one present.
    unsigned long max_used = 0;
    for (const std::string& h : allocated) max_used = std::max(max_used, std::stoul(h, nullptr, 16));
    CHECK(!seed.empty());
    CHECK(std::stoul(seed, nullptr, 16) > max_used);

    // An entity owned by nothing is rejected, so every owner must exist. "0" is
    // the document itself, which the tables hang off.
    for (const std::string& o : owners) {
        CHECK(o == "0" || allocated.count(o) == 1);
    }
}

TEST_CASE("dxf r2000: the sections and tables a later reader expects") {
    Database db;
    add_one_of_each(db);

    bool ok = true;
    const std::vector<Pair> p = parse(dump_as(db, DxfVersion::R2000), &ok);
    CHECK(ok);

    std::set<std::string> sections;
    std::set<std::string> tables;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        if (p[i].code == 0 && p[i].value == "SECTION") sections.insert(p[i + 1].value);
        if (p[i].code == 0 && p[i].value == "TABLE") tables.insert(p[i + 1].value);
    }

    CHECK(sections.count("CLASSES") == 1);
    CHECK(sections.count("OBJECTS") == 1);
    // BLOCK_RECORD is the one R12 has no concept of, and entities name its
    // *Model_Space entry as their owner.
    CHECK(tables.count("BLOCK_RECORD") == 1);
    CHECK(tables.count("VPORT") == 1);
    CHECK(tables.count("DIMSTYLE") == 1);

    // DIMSTYLE's table HEADER carries a second subclass marker of its own,
    // unlike every other table. AutoCAD refuses the file without it: "Class
    // separator for class AcDbDimStyleTable expected".
    bool dim_marker = false;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        if (p[i].code == 100 && p[i].value == "AcDbDimStyleTable") dim_marker = true;
    }
    CHECK(dim_marker);

    // And the DIMSTYLE record carries its handle in group 105, not 5. Group 5
    // is spoken for there by a dimension block name, so emitting 5 leaves the
    // record unhandled and AutoCAD rejects the NEXT one for reusing a handle it
    // never saw claimed: "Bad handle 13: already in use".
    bool dim_105 = false;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        if (p[i].code == 0 && p[i].value == "DIMSTYLE" && i + 1 < p.size() &&
            p[i + 1].code == 105) {
            dim_105 = true;
        }
    }
    CHECK(dim_105);

    // R2000 requires the LTYPE table to carry ByLayer and ByBlock by name --
    // "Missing Default entry ByLayer in SymbolTable:LTYPE" and the file is
    // refused. They are not linetypes a drawing owns but the two values an
    // entity's linetype may take instead of naming one, so they are synthesised
    // rather than stored, and R12 wants neither.
    bool by_layer = false;
    bool by_block = false;
    for (const Pair& g : p) {
        if (g.code != 2) continue;
        if (g.value == "ByLayer") by_layer = true;
        if (g.value == "ByBlock") by_block = true;
    }
    CHECK(by_layer);
    CHECK(by_block);

    // R12 has none of them.
    const std::vector<Pair> r12 = parse(dump_as(db, DxfVersion::R12), &ok);
    std::set<std::string> r12_sections;
    for (std::size_t i = 0; i + 1 < r12.size(); ++i) {
        if (r12[i].code == 0 && r12[i].value == "SECTION") r12_sections.insert(r12[i + 1].value);
    }
    CHECK(r12_sections.count("CLASSES") == 0);
    CHECK(r12_sections.count("OBJECTS") == 0);
}

TEST_CASE("dxf r2000: every entity record declares its AcDb class") {
    // AutoCAD discarded the conformance drawing over this: "Class separator for
    // class AcDbLine expected". Only ELLIPSE, SPLINE and MTEXT declared a class
    // -- every R12-era entity emitted AcDbEntity and stopped. The one R2000
    // file that had been through AutoCAD held nothing but splines, so it never
    // touched the gap.
    Database db;
    add_one_of_each(db);
    db.add(std::make_unique<Arc>(Vec3{0, 0, 0}, 5.0, 0.0, 1.0));
    db.add(std::make_unique<Circle>(Vec3{40, 0, 0}, 3.0));
    db.add(std::make_unique<Text>(Vec3{0, 120, 0}, "gyp", 2.0));

    bool ok = true;
    const std::vector<Pair> p = parse(dump_as(db, DxfVersion::R2000), &ok);
    CHECK(ok);

    // Walk records. Anything that declared AcDbEntity is an entity record and
    // must declare a concrete class too -- except SEQEND, which genuinely has
    // none.
    std::size_t checked = 0;
    std::string current;
    std::vector<std::string> marks;
    auto finish = [&]() {
        if (current.empty()) return;
        const bool is_entity =
            std::find(marks.begin(), marks.end(), "AcDbEntity") != marks.end();
        if (is_entity) {
            ++checked;
            if (current == "SEQEND") {
                CHECK(marks.size() == 1);
            } else {
                CHECK(marks.size() >= 2);
            }
        }
        marks.clear();
    };
    for (const Pair& g : p) {
        if (g.code == 0) {
            finish();
            current = g.value;
        } else if (g.code == 100) {
            marks.push_back(g.value);
        }
    }
    finish();
    // Guard against the walk silently matching nothing and passing.
    CHECK(checked >= 8);

    // ORDER, not merely presence. Layer, linetype and colour are AcDbEntity's,
    // so they sit BETWEEN the base marker and the concrete one; emitting them
    // after the concrete marker puts an AcDbEntity group inside the derived
    // class. AutoCAD accepted that for a LINE and refused it for a TEXT, so
    // presence alone is not the property worth asserting.
    int depth_entity = -1;
    int depth_concrete = -1;
    int depth_layer = -1;
    std::size_t ordered = 0;
    int idx = 0;
    auto check_order = [&]() {
        if (depth_entity < 0) return;
        CHECK(depth_layer > depth_entity);
        if (depth_concrete >= 0) CHECK(depth_layer < depth_concrete);
        ++ordered;
    };
    for (const Pair& g : p) {
        if (g.code == 0) {
            check_order();
            depth_entity = depth_concrete = depth_layer = -1;
            idx = 0;
        }
        ++idx;
        if (g.code == 100 && g.value == "AcDbEntity") {
            depth_entity = idx;
        } else if (g.code == 100 && depth_entity >= 0 && depth_concrete < 0) {
            depth_concrete = idx;
        } else if (g.code == 8 && depth_layer < 0) {
            depth_layer = idx;
        }
    }
    check_order();
    CHECK(ordered >= 8);

    // The chains that are more than one marker deep, each of which has to sit
    // at an exact place in the record rather than merely be present.
    auto has = [&](const char* v) {
        for (const Pair& g : p) {
            if (g.code == 100 && g.value == v) return true;
        }
        return false;
    };
    CHECK(has("AcDbLine"));
    CHECK(has("AcDbCircle"));   // both CIRCLE and ARC start here
    CHECK(has("AcDbArc"));      // ARC then continues
    CHECK(has("AcDbText"));
    CHECK(has("AcDb2dPolyline"));
    CHECK(has("AcDbVertex"));
    CHECK(has("AcDb2dVertex"));

    // A degraded entity takes the class of what it was written AS. At R12 there
    // are no markers at all, so this is checked by the R2000 ellipse being an
    // AcDbEllipse rather than a polyline -- and the polyline present being the
    // real one.
    CHECK(has("AcDbEllipse"));

    // R12 has no subclass markers anywhere, and its output is confirmed good in
    // a real reader.
    const std::vector<Pair> r12 = parse(dump_as(db, DxfVersion::R12), &ok);
    CHECK(ok);
    std::size_t r12_marks = 0;
    for (const Pair& g : r12) {
        if (g.code == 100) ++r12_marks;
    }
    CHECK(r12_marks == 0);
}

TEST_CASE("dxf r2000: an INSERT's shape does not depend on its content") {
    // AutoCAD refused a MINSERT whose scale and rotation were all defaults --
    // "Class separator for class AcDbMInsertBlock expected" -- while accepting
    // plain inserts in the same file with the same groups omitted. The reader
    // walks AcDbBlockReference's fields in order and will not take the derived
    // class's separator until it has seen them, so a record going straight from
    // the insertion point to the separator has no valid reading.
    //
    // R12 omits them when they are unity and its readers do not care, so only
    // the R2000 path changes.
    BlockDef def;
    def.name = "SQ";
    def.entities.push_back(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));

    Database db;
    const BlockId id = db.add_block(std::move(def));
    // Every default: unity scale, no rotation. The case that failed.
    db.add(std::make_unique<Insert>(db.block(id), Mat4::identity()));
    auto arr = std::make_unique<Insert>(db.block(id), Mat4::translation({20, 0, 0}));
    arr->set_array(2, 3, 5.0, 5.0);
    db.add(std::move(arr));

    bool ok = true;
    const std::vector<Pair> p = parse(dump_as(db, DxfVersion::R2000), &ok);
    CHECK(ok);

    // Both records carry the scale and rotation even though every one of them
    // is the default value.
    std::size_t inserts = 0;
    std::size_t with_scale = 0;
    bool in_insert = false;
    std::set<int> seen;
    int marker_at = -1;
    int idx = 0;
    int scale_at = -1;
    auto finish = [&]() {
        if (!in_insert) return;
        ++inserts;
        if (seen.count(41) && seen.count(42) && seen.count(43) && seen.count(50)) ++with_scale;
        // Where there IS a second marker, the parent's fields precede it.
        if (marker_at >= 0) CHECK(scale_at >= 0 && scale_at < marker_at);
        seen.clear();
        marker_at = -1;
        scale_at = -1;
    };
    int concrete = 0;
    for (const Pair& g : p) {
        if (g.code == 0) {
            finish();
            in_insert = (g.value == "INSERT");
            idx = 0;
            concrete = 0;
        }
        if (!in_insert) continue;
        ++idx;
        if (g.code == 100 && g.value != "AcDbEntity") {
            ++concrete;
            if (concrete == 2) marker_at = idx;
        }
        if (g.code == 50 && scale_at < 0) scale_at = idx;
        seen.insert(g.code);
    }
    finish();
    CHECK(inserts == 2);
    CHECK(with_scale == 2);

    // R12 still omits what is default, and that output is confirmed good.
    const std::vector<Pair> r12 = parse(dump_as(db, DxfVersion::R12), &ok);
    CHECK(ok);
    // Scoped to INSERT records: 41/42/43 mean other things elsewhere in the
    // document, and counting them across the whole file measures the header.
    std::size_t r12_scale_groups = 0;
    bool r12_in_insert = false;
    for (const Pair& g : r12) {
        if (g.code == 0) r12_in_insert = (g.value == "INSERT");
        if (!r12_in_insert) continue;
        if (g.code == 41 || g.code == 42 || g.code == 43) ++r12_scale_groups;
    }
    CHECK(r12_scale_groups == 0);
}

TEST_CASE("dxf r2000: the extrusion belongs to the class that owns it") {
    // AutoCAD: "Unexpected DXF group code: 210", reading an ARC. The markers
    // were right and the extrusion was in the wrong one -- an ARC's centre,
    // radius AND extrusion are AcDbCircle's, and only the two angles belong to
    // AcDbArc. Writing 210 after the angles puts it in a class that has no such
    // group.
    //
    // The rule generalises: where a record declares a second class, anything
    // belonging to the first has to precede the separator. So the assertion is
    // about position rather than about the ARC specifically.
    Database db;
    const Vec3 tilt = normalize(Vec3{0.7071, 0.0, 0.7071});
    auto arc = std::make_unique<Arc>(Vec3{0, 0, 0}, 6.0, 0.5, 3.0);
    arc->props().normal = tilt;
    db.add(std::move(arc));

    bool ok = true;
    const std::vector<Pair> p = parse(dump_as(db, DxfVersion::R2000), &ok);
    CHECK(ok);

    // Walk the ARC record: find the second concrete marker and check 210 is
    // before it, and that it is there at all -- a tilted entity that lost its
    // extrusion would be silently flat.
    int second_marker = -1;
    int extrusion = -1;
    int concrete = 0;
    int idx = 0;
    bool in_arc = false;
    for (const Pair& g : p) {
        if (g.code == 0) {
            in_arc = (g.value == "ARC");
            concrete = 0;
            idx = 0;
        }
        if (!in_arc) continue;
        ++idx;
        if (g.code == 100 && g.value != "AcDbEntity") {
            ++concrete;
            if (concrete == 2 && second_marker < 0) second_marker = idx;
        }
        if (g.code == 210 && extrusion < 0) extrusion = idx;
    }
    CHECK(second_marker > 0);           // AcDbArc is there
    CHECK(extrusion > 0);               // and so is the extrusion
    CHECK(extrusion < second_marker);   // and it is on the AcDbCircle side

    // No 210 after a second marker anywhere in the document.
    int since_second = -1;
    concrete = 0;
    for (const Pair& g : p) {
        if (g.code == 0) {
            concrete = 0;
            since_second = -1;
        }
        if (g.code == 100 && g.value != "AcDbEntity") {
            ++concrete;
            if (concrete == 2) since_second = 1;
        }
        if (g.code == 210 && since_second > 0) CHECK(false);
    }
}

TEST_CASE("dxf r2000: a polyline's VERTEX and SEQEND get their own handles") {
    // R13 and later require a handle on EVERY record and require it to be
    // unique. VERTEX and SEQEND are not database entities and own none, so they
    // were emitted with no handle at all -- the mirror of the R12 bug, where
    // they were emitted carrying the parent's.
    Database db;
    auto pl = std::make_unique<Polyline>();
    pl->add({0, 0, 0});
    pl->add({10, 0, 0});
    pl->add({10, 10, 0});
    db.add(std::move(pl));

    bool ok = true;
    const std::vector<Pair> p = parse(dump_as(db, DxfVersion::R2000), &ok);
    CHECK(ok);

    // Every record carries a handle, and no handle is used twice.
    std::vector<std::string> handles;
    std::string polyline_handle;
    std::string current;
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i].code == 0) current = p[i].value;
        if (p[i].code == 5) {
            handles.push_back(p[i].value);
            if (current == "POLYLINE") polyline_handle = p[i].value;
        }
    }
    std::vector<std::string> sorted = handles;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
    CHECK(!polyline_handle.empty());

    // And each subordinate record names the polyline as its owner, which is the
    // relationship the file exists to express.
    std::size_t owned = 0;
    current.clear();
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        if (p[i].code == 0) current = p[i].value;
        if (p[i].code == 330 && (current == "VERTEX" || current == "SEQEND")) {
            CHECK(p[i].value == polyline_handle);
            ++owned;
        }
    }
    CHECK(owned == 4);  // three vertices and the SEQEND
}

TEST_CASE("dxf r2000: every dash length is followed by an element type") {
    // AutoCAD rejected the conformance drawing over this and discarded it:
    // "Error in LTYPE Table / Missing group code 49 in complex linetype".
    //
    // The message names the group that IS there rather than the one that is
    // not. R13 and later expect an element TYPE (group 74) after every dash
    // length, so with the 74s absent the reader is still looking for the end of
    // the first element when it meets the second 49 -- and reports the 49.
    //
    // Zero means a plain dash: no embedded shape, no embedded text. Nothing
    // here generates anything else, since complex linetypes would need a shape
    // file and there is no SHX path.
    Database db;
    db.add_linetype("DASHED", "Dashed __ __ __", {0.5, -0.25});
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    bool ok = true;
    const std::vector<Pair> p = parse(dump_as(db, DxfVersion::R2000), &ok);
    CHECK(ok);

    // Every 49 is followed immediately by a 74, and there are as many pairs as
    // the pattern has elements.
    std::size_t dashes = 0;
    std::size_t paired = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i].code != 49) continue;
        ++dashes;
        if (i + 1 < p.size() && p[i + 1].code == 74) ++paired;
    }
    CHECK(dashes == 2);
    CHECK(paired == dashes);

    // AC1009 has no group 74 in LTYPE at all, and the R12 output is confirmed
    // good in a real reader -- so it must not acquire one.
    const std::vector<Pair> r12 = parse(dump_as(db, DxfVersion::R12), &ok);
    CHECK(ok);
    std::size_t r12_dashes = 0;
    std::size_t r12_74 = 0;
    for (const Pair& g : r12) {
        if (g.code == 49) ++r12_dashes;
        if (g.code == 74) ++r12_74;
    }
    CHECK(r12_dashes == 2);
    CHECK(r12_74 == 0);
}

TEST_CASE("dxf r2000: layers carry a plot style, and every pointer resolves") {
    // AutoCAD refused the whole file without this: "Error in LAYER Table / Did
    // not receive PlotStyleName". Group 390 is a HARD POINTER, so the object it
    // names has to exist -- which is why the OBJECTS section carries a
    // plot-style dictionary and a placeholder, and why their handles are
    // reserved before the tables are written.
    Database db;
    db.add_layer("WALLS");
    add_one_of_each(db);

    bool ok = true;
    const std::vector<Pair> p = parse(dump_as(db, DxfVersion::R2000), &ok);
    CHECK(ok);

    std::set<std::string> allocated;
    bool in_header = false;
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i].code == 0 && p[i].value == "SECTION" && i + 1 < p.size()) {
            in_header = (p[i + 1].value == "HEADER");
        }
        if (p[i].code == 5 && !in_header) allocated.insert(p[i].value);
    }

    // One plot style per layer, and each names something that exists.
    std::size_t plot_styles = 0;
    for (const Pair& g : p) {
        if (g.code != 390) continue;
        ++plot_styles;
        CHECK(allocated.count(g.value) == 1);
    }
    CHECK(plot_styles == db.layers().size());

    // Every reference of every kind resolves. "0" is the document root.
    for (const Pair& g : p) {
        if (g.code == 330 || g.code == 340 || g.code == 350) {
            CHECK(g.value == "0" || allocated.count(g.value) == 1);
        }
    }

    // And the objects the pointers land on are actually emitted.
    CHECK(count_records(p, "ACDBPLACEHOLDER") == 1);
    CHECK(count_records(p, "ACDBDICTIONARYWDFLT") == 1);
}
