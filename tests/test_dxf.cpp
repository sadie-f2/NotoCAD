// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/database.hpp"
#include "noto/dxf.hpp"
#include "noto/ecs.hpp"
#include "noto/entities.hpp"

#include <algorithm>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

using namespace noto;

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
