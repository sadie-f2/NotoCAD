// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The drawing's unit, and the polyglot prompt.
//
// The rule under test is `units.hpp`'s: the drawing has ONE unit and the
// prompt accepts ANY. What makes that safe rather than clever is that the
// conversion happens in exactly one place, so these tests can pin it in one
// place too -- if a second conversion ever appears, one of these stops
// agreeing with the drawing.

#include "test.hpp"

#include "ncad/command.hpp"
#include "ncad/database.hpp"
#include "ncad/input_text.hpp"
#include "ncad/dxf.hpp"
#include "ncad/dxf_read.hpp"
#include "ncad/entities.hpp"
#include "ncad/sysvar.hpp"
#include "ncad/units.hpp"

#include <string>

using namespace ncad;

namespace {

constexpr double kTol = 1e-9;

// A length read at a Distance prompt, which is the shortest path to the
// parser without building a command.
bool read_length(const char* text, std::int32_t insunits, double& out, std::string& error) {
    Prompt p;
    p.kind = PromptKind::Distance;
    InputValue v;
    if (!parse_input(p, text, v, error, nullptr, insunits)) return false;
    if (v.kind != InputKind::Real) return false;
    out = v.real;
    return true;
}

}  // namespace

TEST_CASE("units: a bare number is the workspace unit, whatever that is") {
    // The compatibility promise, and the reason nothing in the corpus moved:
    // input that carries no unit means exactly what it always meant.
    for (const std::int32_t code : {kInsUnitless, kInsInches, kInsMillimetres, kInsMetres}) {
        double v = 0.0;
        std::string error;
        REQUIRE(read_length("6.35", code, v, error));
        CHECK_NEAR(v, 6.35, kTol);
    }
}

TEST_CASE("units: a suffix converts into the drawing's own unit") {
    double v = 0.0;
    std::string error;

    // A quarter inch, on a millimetre drawing.
    REQUIRE(read_length("1/4\"", kInsMillimetres, v, error));
    CHECK_NEAR(v, 6.35, kTol);

    // The same length written three other ways.
    REQUIRE(read_length("0.25in", kInsMillimetres, v, error));
    CHECK_NEAR(v, 6.35, kTol);
    REQUIRE(read_length("6.35mm", kInsMillimetres, v, error));
    CHECK_NEAR(v, 6.35, kTol);
    REQUIRE(read_length("0.635cm", kInsMillimetres, v, error));
    CHECK_NEAR(v, 6.35, kTol);

    // And the same quarter inch on an INCH drawing is a quarter, not 6.35.
    // This is the pair that matters: one input, two drawings, two right
    // answers.
    REQUIRE(read_length("1/4\"", kInsInches, v, error));
    CHECK_NEAR(v, 0.25, kTol);
    REQUIRE(read_length("6.35mm", kInsInches, v, error));
    CHECK_NEAR(v, 0.25, kTol);
}

TEST_CASE("units: feet and metres, and the spelling with a symbol") {
    double v = 0.0;
    std::string error;
    REQUIRE(read_length("1'", kInsMillimetres, v, error));
    CHECK_NEAR(v, 304.8, kTol);
    REQUIRE(read_length("1ft", kInsInches, v, error));
    CHECK_NEAR(v, 12.0, kTol);
    REQUIRE(read_length("1m", kInsMillimetres, v, error));
    CHECK_NEAR(v, 1000.0, kTol);
}

TEST_CASE("units: a suffix on a unitless drawing refuses, naming INSUNITS") {
    // NOT an assumption of millimetres. There is no answer to "a quarter inch
    // of what", and guessing one is exactly how an inch part leaves 25.4x
    // wrong -- which is the failure the whole unit decision exists to stop.
    double v = 0.0;
    std::string error;
    CHECK(!read_length("1/4\"", kInsUnitless, v, error));
    CHECK(error.find("INSUNITS") != std::string::npos);
    if (error.find("INSUNITS") == std::string::npos) {
        std::printf("       refusal did not name INSUNITS: %s\n", error.c_str());
    }
}

TEST_CASE("units: the full-consumption rule survives") {
    // "1.5 is a number, 1.5x is not" predates units and must outlive them: a
    // typo has to stay an error rather than becoming a silently truncated
    // value with a unit invented for it.
    double v = 0.0;
    std::string error;
    CHECK(!read_length("1.5x", kInsMillimetres, v, error));
    CHECK(!read_length("6.35mmm", kInsMillimetres, v, error));
    CHECK(!read_length("mm", kInsMillimetres, v, error));
    CHECK(!read_length("1/", kInsMillimetres, v, error));
    CHECK(!read_length("1/0", kInsMillimetres, v, error));
}

TEST_CASE("units: fractions, including the hyphenated mixed form") {
    double v = 0.0;
    std::string error;
    REQUIRE(read_length("1/4", kInsInches, v, error));
    CHECK_NEAR(v, 0.25, kTol);

    // A typed line is TOKENISED on whitespace before any parser sees it, so
    // AutoCAD's "1 1/4" arrives as two separate answers. The hyphen is the
    // form that survives, and coordinates carry no arithmetic to confuse it
    // with.
    REQUIRE(read_length("1-1/4", kInsInches, v, error));
    CHECK_NEAR(v, 1.25, kTol);

    // A leading minus is a SIGN, and the fraction stays on its side of zero.
    REQUIRE(read_length("-1/4", kInsInches, v, error));
    CHECK_NEAR(v, -0.25, kTol);
    REQUIRE(read_length("-1-1/4", kInsInches, v, error));
    CHECK_NEAR(v, -1.25, kTol);
}

TEST_CASE("units: an angle never takes a unit") {
    // `30<45` must not become `30<45mm`. A bearing has nothing to convert
    // into, and letting it try would make a typo silently mean something.
    Prompt p;
    p.kind = PromptKind::Angle;
    InputValue v;
    std::string error;
    CHECK(!parse_input(p, "45mm", v, error, nullptr, kInsMillimetres));
    CHECK(parse_input(p, "45", v, error, nullptr, kInsMillimetres));
}

TEST_CASE("units: a coordinate takes a unit per component, and may mix them") {
    // Converting at the parser rather than at one command is what makes this
    // work: each coordinate is read on its own, so a mixed pair is not a
    // special case anybody had to write.
    Prompt p;
    p.kind = PromptKind::Point;
    InputValue v;
    std::string error;
    REQUIRE(parse_input(p, "1/4\",10mm", v, error, nullptr, kInsMillimetres));
    REQUIRE(v.kind == InputKind::Point);
    CHECK_NEAR(v.point.x, 6.35, kTol);
    CHECK_NEAR(v.point.y, 10.0, kTol);
}

TEST_CASE("units: a polar distance takes a unit and its bearing does not") {
    Prompt p;
    p.kind = PromptKind::Point;
    InputValue v;
    std::string error;
    REQUIRE(parse_input(p, "1/4\"<0", v, error, nullptr, kInsMillimetres));
    REQUIRE(v.kind == InputKind::Point);
    CHECK_NEAR(v.point.x, 6.35, kTol);
    CHECK(!parse_input(p, "10<45mm", v, error, nullptr, kInsMillimetres));
}

TEST_CASE("units: INSUNITS accepts a SET of codes, not a span") {
    // 3 is miles in AutoCAD's table. It sits inside 0..6 and is not a unit
    // this program will carry into a STEP file, so a range check would have
    // let it through.
    Sysvars sv;
    CHECK(sv.get_int(Sysvar::InsUnits) == 0);  // every drawing that exists
    for (const std::int32_t ok : {0, 1, 2, 4, 5, 6}) {
        CHECK(sv.set_int(Sysvar::InsUnits, ok) == Sysvars::SetStatus::Ok);
    }
    for (const std::int32_t bad : {3, 7, -1, 99}) {
        CHECK(sv.set_int(Sysvar::InsUnits, bad) == Sysvars::SetStatus::OutOfRange);
    }
    CHECK(insunits_valid(4));
    CHECK(!insunits_valid(3));
    CHECK(std::string(insunits_name(4)) == "millimetres");
}

// --- the drawing carries it -----------------------------------------------

TEST_CASE("units: a unitless drawing writes no $INSUNITS at all") {
    // THE escalation trigger for brief 19, and the reason the header is
    // conditional rather than always written. Writing `$INSUNITS 0` would be
    // truthful and would also move a byte in every DXF this program has ever
    // produced -- the one change a file format cannot make quietly. An absent
    // $INSUNITS reads as 0 anyway, so silence costs nothing.
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));
    REQUIRE(db.sysvars().get_int(Sysvar::InsUnits) == 0);

    const std::string dxf = write_dxf_text(db);
    CHECK(dxf.find("$INSUNITS") == std::string::npos);
}

TEST_CASE("units: a drawing that has a unit carries it, and reads it back") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));
    REQUIRE(db.sysvars().set_int(Sysvar::InsUnits, kInsMillimetres) == Sysvars::SetStatus::Ok);

    const std::string dxf = write_dxf_text(db);
    CHECK(dxf.find("$INSUNITS") != std::string::npos);

    Database back;
    REQUIRE(back.sysvars().get_int(Sysvar::InsUnits) == kInsUnitless);
    read_dxf_text(back, dxf);
    CHECK(back.sysvars().get_int(Sysvar::InsUnits) == kInsMillimetres);
}

TEST_CASE("units: the unit is saved in the drawing, not in the installation") {
    // Unlike DXFVERSION beside it in the table. Which unit a drawing is in is
    // a property OF THE DRAWING: a part that arrives from someone else must
    // bring its unit with it, or the number 8 means nothing.
    const SysvarDef* d = find_sysvar("INSUNITS");
    REQUIRE(d != nullptr);
    CHECK(d->save_in_drawing);
    CHECK(!d->read_only);
}
