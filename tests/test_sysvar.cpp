// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/database.hpp"
#include "noto/osnap.hpp"
#include "noto/sysvar.hpp"

using namespace noto;

using SetStatus = Sysvars::SetStatus;

TEST_CASE("sysvar: defaults") {
    Sysvars sv;
    // 53 = END|CEN|QUA|INT. R12 ships 0; we do not, because there is no OSNAP
    // command or status line yet to turn snapping on with.
    CHECK(sv.get_int(Sysvar::OsMode) == 53);
    CHECK(sv.get_int(Sysvar::OsMode) ==
          (kOsnapEndpoint | kOsnapCenter | kOsnapQuadrant | kOsnapIntersection));
    // Deliberately absent: MID competes with END all along a line, and NEA
    // matches everywhere.
    CHECK(!osnap_enabled(static_cast<OsnapMask>(sv.get_int(Sysvar::OsMode)), OsnapType::Midpoint));
    CHECK(!osnap_enabled(static_cast<OsnapMask>(sv.get_int(Sysvar::OsMode)), OsnapType::Nearest));
    CHECK(sv.get_int(Sysvar::PickBox) == 3);
    CHECK(sv.get_int(Sysvar::Aperture) == 10);
}

TEST_CASE("sysvar: the table and the enum agree") {
    std::size_t count = 0;
    const SysvarDef* table = sysvar_table(count);
    CHECK(count == static_cast<std::size_t>(Sysvar::kCount));

    // Every row must sit at the index its id names, or the array storage and
    // the typed accessors disagree silently.
    for (std::size_t i = 0; i < count; ++i) {
        CHECK(static_cast<std::size_t>(table[i].id) == i);
        CHECK(&sysvar_def(table[i].id) == &table[i]);
    }
}

TEST_CASE("sysvar: name lookup is case-insensitive") {
    CHECK(find_sysvar("OSMODE") != nullptr);
    CHECK(find_sysvar("osmode") != nullptr);
    CHECK(find_sysvar("OsMode") != nullptr);
    CHECK(find_sysvar("OSMODE") == find_sysvar("osmode"));

    // A prefix is not a match, and neither is an extension.
    CHECK(find_sysvar("OSMOD") == nullptr);
    CHECK(find_sysvar("OSMODEX") == nullptr);
    CHECK(find_sysvar("") == nullptr);
    CHECK(find_sysvar("NOSUCHVAR") == nullptr);
}

TEST_CASE("sysvar: unknown names") {
    Sysvars sv;
    SysvarValue out;
    CHECK(!sv.get("NOSUCHVAR", out));
    CHECK(sv.set("NOSUCHVAR", SysvarValue::of_int(1)) == SetStatus::Unknown);
}

TEST_CASE("sysvar: out of range is rejected, not clamped") {
    Sysvars sv;
    CHECK(sv.set_int(Sysvar::PickBox, 900) == SetStatus::OutOfRange);
    CHECK(sv.get_int(Sysvar::PickBox) == 3);  // the old value survives

    CHECK(sv.set_int(Sysvar::PickBox, -1) == SetStatus::OutOfRange);
    CHECK(sv.get_int(Sysvar::PickBox) == 3);

    // APERTURE's minimum is 1, not 0: a zero-size aperture cannot be hit.
    CHECK(sv.set_int(Sysvar::Aperture, 0) == SetStatus::OutOfRange);
    CHECK(sv.set_int(Sysvar::Aperture, 1) == SetStatus::Ok);

    // The bounds themselves are inclusive.
    CHECK(sv.set_int(Sysvar::PickBox, 0) == SetStatus::Ok);
    CHECK(sv.set_int(Sysvar::PickBox, 50) == SetStatus::Ok);
    CHECK(sv.set_int(Sysvar::OsMode, 2047) == SetStatus::Ok);
    CHECK(sv.set_int(Sysvar::OsMode, 2048) == SetStatus::OutOfRange);
}

TEST_CASE("sysvar: type mismatch is rejected") {
    Sysvars sv;
    CHECK(sv.set("OSMODE", SysvarValue::of_string("47")) == SetStatus::WrongType);
    CHECK(sv.set("OSMODE", SysvarValue::of_point({1, 2, 3})) == SetStatus::WrongType);

    // A real is not silently truncated into an integer variable.
    CHECK(sv.set("OSMODE", SysvarValue::of_real(47.0)) == SetStatus::WrongType);
    CHECK(sv.get_int(Sysvar::OsMode) == 53);

    CHECK(sv.set_real(Sysvar::OsMode, 1.0) == SetStatus::WrongType);
    CHECK(sv.set_string(Sysvar::OsMode, "x") == SetStatus::WrongType);
}

TEST_CASE("sysvar: name-driven round trip") {
    Sysvars sv;
    CHECK(sv.set("osmode", SysvarValue::of_int(47)) == SetStatus::Ok);

    SysvarValue out;
    CHECK(sv.get("OSMODE", out));
    CHECK(out.type == SysvarType::Int);
    CHECK(out.integer == 47);
    CHECK(sv.get_int(Sysvar::OsMode) == 47);
}

TEST_CASE("sysvar: reset_drawing_vars leaves configuration alone") {
    Sysvars sv;
    CHECK(sv.set_int(Sysvar::OsMode, 47) == SetStatus::Ok);
    CHECK(sv.set_int(Sysvar::PickBox, 7) == SetStatus::Ok);

    sv.reset_drawing_vars();

    // OSMODE belongs to the drawing; PICKBOX follows the installation.
    CHECK(sv.get_int(Sysvar::OsMode) == 53);
    CHECK(sv.get_int(Sysvar::PickBox) == 7);
}

TEST_CASE("sysvar: a database starts with defaults and is mutable") {
    Database db;
    CHECK(db.sysvars().get_int(Sysvar::OsMode) == 53);
    CHECK(db.sysvars().set_int(Sysvar::OsMode, 33) == SetStatus::Ok);

    const Database& cdb = db;
    CHECK(cdb.sysvars().get_int(Sysvar::OsMode) == 33);
}

// The bit values are R12's and are NOT this enum's declaration order. If
// osnap_bit() is ever rewritten as `1 << int(t)` these are what catch it.
TEST_CASE("osnap: OSMODE bit values are R12's, not the enum order") {
    CHECK(osnap_bit(OsnapType::Endpoint) == 1);
    CHECK(osnap_bit(OsnapType::Midpoint) == 2);
    CHECK(osnap_bit(OsnapType::Center) == 4);
    CHECK(osnap_bit(OsnapType::Node) == 8);
    CHECK(osnap_bit(OsnapType::Quadrant) == 16);
    CHECK(osnap_bit(OsnapType::Intersection) == 32);
    CHECK(osnap_bit(OsnapType::Insert) == 64);
    CHECK(osnap_bit(OsnapType::Perpendicular) == 128);
    CHECK(osnap_bit(OsnapType::Tangent) == 256);
    CHECK(osnap_bit(OsnapType::Nearest) == 512);

    // The three that a naive shift would get wrong. Node is declared fifth but
    // is bit 3; Quadrant is declared fourth but is bit 4; Intersection is
    // declared last but sits mid-mask.
    CHECK(osnap_bit(OsnapType::Node) != (1u << 4));
    CHECK(osnap_bit(OsnapType::Quadrant) != (1u << 3));
    CHECK(osnap_bit(OsnapType::Intersection) != (1u << 9));
}

TEST_CASE("osnap: mask membership") {
    // OSMODE 47 = END|MID|CEN|NOD|INT, the combination R12 users reach for.
    constexpr OsnapMask m = 47;
    CHECK(osnap_enabled(m, OsnapType::Endpoint));
    CHECK(osnap_enabled(m, OsnapType::Midpoint));
    CHECK(osnap_enabled(m, OsnapType::Center));
    CHECK(osnap_enabled(m, OsnapType::Node));
    CHECK(osnap_enabled(m, OsnapType::Intersection));

    CHECK(!osnap_enabled(m, OsnapType::Quadrant));
    CHECK(!osnap_enabled(m, OsnapType::Nearest));
    CHECK(!osnap_enabled(m, OsnapType::Tangent));
    CHECK(!osnap_enabled(m, OsnapType::Perpendicular));

    CHECK(!osnap_enabled(kOsnapNone, OsnapType::Endpoint));
    CHECK(osnap_enabled(kOsnapAll, OsnapType::Endpoint));
    CHECK(osnap_enabled(kOsnapAll, OsnapType::Nearest));

    // 47 is exactly those five bits, which is the point of the constant names.
    CHECK((kOsnapEndpoint | kOsnapMidpoint | kOsnapCenter | kOsnapNode |
           kOsnapIntersection) == m);
}

TEST_CASE("osnap: every type round-trips through its bit") {
    const OsnapType all[] = {
        OsnapType::Endpoint, OsnapType::Midpoint,      OsnapType::Center,
        OsnapType::Quadrant, OsnapType::Node,          OsnapType::Insert,
        OsnapType::Tangent,  OsnapType::Perpendicular, OsnapType::Nearest,
        OsnapType::Intersection,
    };

    OsnapMask seen = kOsnapNone;
    for (OsnapType t : all) {
        const OsnapMask bit = osnap_bit(t);
        CHECK(bit != kOsnapNone);

        // No two types share a bit.
        CHECK((seen & bit) == 0);
        seen = static_cast<OsnapMask>(seen | bit);

        OsnapType back{};
        CHECK(osnap_type_from_bit(bit, &back));
        CHECK(back == t);
        CHECK(osnap_enabled(bit, t));
    }
    CHECK(seen == kOsnapAll);

    // QUICK names a search strategy, not a snap type.
    OsnapType unused{};
    CHECK(!osnap_type_from_bit(kOsnapQuick, &unused));
    CHECK(!osnap_type_from_bit(kOsnapNone, &unused));
    CHECK(!osnap_type_from_bit(47, &unused));  // a combination, not a single bit
}
