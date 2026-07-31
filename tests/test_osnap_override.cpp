// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "ncad/command.hpp"
#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/input_text.hpp"
#include "ncad/osnap.hpp"

#include <string>

using namespace ncad;

namespace {

OsnapMask parse(const char* text) {
    OsnapMask m = kOsnapAll;  // a sentinel the parser must overwrite
    return parse_osnap_mask(text, &m) ? m : static_cast<OsnapMask>(0xFFFF);
}

bool rejects(const char* text) {
    OsnapMask m = kOsnapNone;
    return !parse_osnap_mask(text, &m);
}

Prompt point_prompt() {
    Prompt p;
    p.kind = PromptKind::Point;
    p.message = "Specify point";
    // A relative coordinate needs something to be relative to.
    p.last_point = Vec3{1, 1, 0};
    p.has_last_point = true;
    return p;
}

}  // namespace

TEST_CASE("osnap override: the three-letter codes") {
    CHECK(parse("END") == kOsnapEndpoint);
    CHECK(parse("MID") == kOsnapMidpoint);
    CHECK(parse("CEN") == kOsnapCenter);
    CHECK(parse("NOD") == kOsnapNode);
    CHECK(parse("QUA") == kOsnapQuadrant);
    CHECK(parse("INT") == kOsnapIntersection);
    CHECK(parse("INS") == kOsnapInsert);
    CHECK(parse("PER") == kOsnapPerpendicular);
    CHECK(parse("TAN") == kOsnapTangent);
    CHECK(parse("NEA") == kOsnapNearest);
}

TEST_CASE("osnap override: case and full names") {
    CHECK(parse("cen") == kOsnapCenter);
    CHECK(parse("Cen") == kOsnapCenter);
    CHECK(parse("CENTER") == kOsnapCenter);
    CHECK(parse("center") == kOsnapCenter);
    CHECK(parse("endpoint") == kOsnapEndpoint);
    CHECK(parse("perp") == kOsnapPerpendicular);
    CHECK(parse("tangent") == kOsnapTangent);
}

TEST_CASE("osnap override: combinations") {
    CHECK(parse("end,mid") == (kOsnapEndpoint | kOsnapMidpoint));
    CHECK(parse("END,MID,CEN") == (kOsnapEndpoint | kOsnapMidpoint | kOsnapCenter));
    CHECK(parse("end mid") == (kOsnapEndpoint | kOsnapMidpoint));
    CHECK(parse("end, mid") == (kOsnapEndpoint | kOsnapMidpoint));

    // Repeating one is harmless.
    CHECK(parse("end,end") == kOsnapEndpoint);
}

TEST_CASE("osnap override: NONE is a successful parse to an empty mask") {
    // The distinction that matters: "none" means snap to nothing, which is not
    // the same as failing to understand the input.
    OsnapMask m = kOsnapAll;
    CHECK(parse_osnap_mask("non", &m));
    CHECK(m == kOsnapNone);

    m = kOsnapAll;
    CHECK(parse_osnap_mask("NONE", &m));
    CHECK(m == kOsnapNone);

    // And it wins over anything it is combined with: the safe reading of a
    // contradiction is to snap to nothing.
    m = kOsnapAll;
    CHECK(parse_osnap_mask("cen,none", &m));
    CHECK(m == kOsnapNone);
}

TEST_CASE("osnap override: what is not an override") {
    CHECK(rejects(""));
    CHECK(rejects("12"));
    CHECK(rejects("3,4"));
    CHECK(rejects("@5,0"));
    CHECK(rejects("zzz"));
    CHECK(rejects("endz"));

    // Under three characters is never a snap name. This is what keeps "C" at a
    // prompt offering Close and Center an ambiguous keyword rather than CEN.
    CHECK(rejects("C"));
    CHECK(rejects("EN"));
    CHECK(rejects("N"));
}

TEST_CASE("osnap override: typed at a point prompt, it is not a point") {
    const Prompt p = point_prompt();
    InputValue v;
    std::string err;

    CHECK(parse_input(p, "cen", v, err));
    CHECK(v.kind == InputKind::OsnapOverride);
    CHECK(static_cast<OsnapMask>(v.integer) == kOsnapCenter);

    CHECK(parse_input(p, "non", v, err));
    CHECK(v.kind == InputKind::OsnapOverride);
    CHECK(static_cast<OsnapMask>(v.integer) == kOsnapNone);

    // Coordinates still parse as coordinates. Nothing is ambiguous, because a
    // coordinate starts with a digit, sign, dot or '@' and a snap name never
    // does.
    CHECK(parse_input(p, "3,4", v, err));
    CHECK(v.kind == InputKind::Point);
    CHECK(parse_input(p, "@5,0", v, err));
    CHECK(v.kind == InputKind::Point);
    CHECK(parse_input(p, "-2.5,1", v, err));
    CHECK(v.kind == InputKind::Point);
}

TEST_CASE("osnap override: the engine absorbs it and re-asks") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LINE"));
    CHECK(engine.active());
    CHECK(!engine.has_osnap_override());

    const std::string asked = engine.prompt().text();

    // An override does not answer the prompt: the command never sees it, the
    // engine stays waiting, and the same question stands.
    CHECK(engine.supply(InputValue::of_osnap_override(kOsnapCenter)) == EngineStatus::Waiting);
    CHECK(engine.active());
    CHECK(engine.prompt().text() == asked);
    CHECK(engine.has_osnap_override());
    CHECK(engine.osnap_override() == kOsnapCenter);
}

TEST_CASE("osnap override: one pick is its whole lifetime") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LINE"));

    engine.supply(InputValue::of_osnap_override(kOsnapCenter));
    CHECK(engine.has_osnap_override());

    // The point that follows spends it.
    engine.supply(InputValue::of_point({0, 0, 0}));
    CHECK(!engine.has_osnap_override());
    CHECK(engine.osnap_override() == kOsnapNone);

    // A later override is independent, and the last one typed wins.
    engine.supply(InputValue::of_osnap_override(kOsnapMidpoint));
    CHECK(engine.osnap_override() == kOsnapMidpoint);
    engine.supply(InputValue::of_osnap_override(kOsnapTangent));
    CHECK(engine.osnap_override() == kOsnapTangent);
}

TEST_CASE("osnap override: NON is a real override, not an absent one") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LINE"));

    // The distinction the has_/get_ pair exists for: an override of kOsnapNone
    // means "snap to nothing this pick", and a viewport must not fall back to
    // OSMODE when it sees one.
    engine.supply(InputValue::of_osnap_override(kOsnapNone));
    CHECK(engine.has_osnap_override());
    CHECK(engine.osnap_override() == kOsnapNone);
}

TEST_CASE("osnap override: a keyword also spends it") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));

    engine.supply(InputValue::of_osnap_override(kOsnapCenter));
    CHECK(engine.has_osnap_override());

    // Anything that answers the prompt clears it, not only a point -- a stale
    // override surviving into the next prompt would move a point the user did
    // not ask to move.
    engine.supply(InputValue::of_keyword("UNDO"));
    CHECK(!engine.has_osnap_override());
}
