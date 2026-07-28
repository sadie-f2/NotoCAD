// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// PLINE. The prompt sequencing is ordinary; the bulges are not, so most of
// what is pinned here is the arithmetic that turns each arc option into a
// group 42 -- and above all its SIGN, which is what decides whether an arc
// bows left or right of its chord.

#include "test.hpp"

#include "noto/command.hpp"
#include "noto/commands.hpp"
#include "noto/database.hpp"
#include "noto/dxf.hpp"
#include "noto/dxf_read.hpp"
#include "noto/entities.hpp"

#include <cmath>
#include <memory>
#include <numbers>
#include <sstream>

using namespace noto;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kTol = 1e-9;

const Polyline* last_polyline(const Database& db) {
    const Entity* e = db.get(db.last());
    if (!e || e->type() != EntityType::Polyline) return nullptr;
    return static_cast<const Polyline*>(e);
}

}  // namespace

TEST_CASE("pline: straight segments make one entity, not one per segment") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLINE"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_point({10, 10, 0}));
    engine.supply(InputValue::none());

    CHECK(db.size() == 1);
    const Polyline* p = last_polyline(db);
    REQUIRE(p != nullptr);
    CHECK(p->size() == 3);
    CHECK(!p->closed());
    CHECK(p->vertices()[0].bulge == 0.0);
}

TEST_CASE("pline: Close sets the flag and needs no repeat of the first point") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLINE"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_point({10, 10, 0}));
    engine.supply(InputValue::of_keyword("CLOSE"));

    const Polyline* p = last_polyline(db);
    REQUIRE(p != nullptr);
    CHECK(p->closed());
    CHECK(p->size() == 3);
    CHECK(p->segment_count() == 3);
}

TEST_CASE("pline: Undo pops a vertex and leaves the entity in place") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLINE"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_point({10, 10, 0}));
    const Handle before = db.last();
    engine.supply(InputValue::of_keyword("UNDO"));

    const Polyline* p = last_polyline(db);
    REQUIRE(p != nullptr);
    CHECK(p->size() == 2);
    // The handle survives, because the polyline is replaced rather than rebuilt.
    CHECK(db.last() == before);

    engine.supply(InputValue::none());
    CHECK(db.size() == 1);
}

TEST_CASE("pline: undoing back to one vertex removes the entity entirely") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLINE"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    CHECK(db.size() == 1);

    engine.supply(InputValue::of_keyword("UNDO"));
    // One point is not a polyline, and leaving a one-vertex entity behind
    // would be an unpickable, unrenderable thing in the drawing.
    CHECK(db.size() == 0);
}

// --- the arc sub-mode -------------------------------------------------------

TEST_CASE("pline: a half turn is bulge 1") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLINE"));

    // Along +X, then an arc turning left onto (10,10). The tangent is +X and
    // the chord is +Y, so the included angle is twice the 90 degrees between
    // them: a half turn, whose quarter-angle tangent is exactly 1.
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_keyword("ARC"));
    engine.supply(InputValue::of_point({10, 10, 0}));
    engine.supply(InputValue::none());

    const Polyline* p = last_polyline(db);
    REQUIRE(p != nullptr);
    REQUIRE(p->size() == 3);
    CHECK(std::abs(p->vertices()[1].bulge - 1.0) < kTol);
}

TEST_CASE("pline: an arc turning the other way gets the opposite bulge") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLINE"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_keyword("ARC"));
    engine.supply(InputValue::of_point({10, -10, 0}));
    engine.supply(InputValue::none());

    const Polyline* p = last_polyline(db);
    REQUIRE(p != nullptr);
    CHECK(std::abs(p->vertices()[1].bulge + 1.0) < kTol);
}

TEST_CASE("pline: Angle gives the included angle outright") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLINE"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_keyword("ARC"));
    engine.supply(InputValue::of_keyword("ANGLE"));
    engine.supply(InputValue::of_real(90.0));  // degrees, as R12 talks to people
    engine.supply(InputValue::of_point({10, 10, 0}));
    engine.supply(InputValue::none());

    const Polyline* p = last_polyline(db);
    REQUIRE(p != nullptr);
    // tan(90/4 degrees) = tan(22.5 degrees).
    CHECK(std::abs(p->vertices()[0].bulge - std::tan(kPi * 0.125)) < kTol);
}

TEST_CASE("pline: CEnter takes the short way round") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLINE"));

    // Start at (10,0) with the centre at the origin, ending at (0,10): a
    // quarter turn counterclockwise.
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_keyword("ARC"));
    engine.supply(InputValue::of_keyword("CENTER"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({0, 10, 0}));
    engine.supply(InputValue::none());

    const Polyline* p = last_polyline(db);
    REQUIRE(p != nullptr);
    CHECK(std::abs(p->vertices()[0].bulge - std::tan(kPi * 0.125)) < kTol);
}

TEST_CASE("pline: three points through Second describe the arc between them") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLINE"));

    // (10,0) -> (0,10) -> (-10,0): a half turn counterclockwise about the
    // origin, so bulge 1 again -- reached without any tangent to continue.
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_keyword("ARC"));
    engine.supply(InputValue::of_keyword("SECOND"));
    engine.supply(InputValue::of_point({0, 10, 0}));
    engine.supply(InputValue::of_point({-10, 0, 0}));
    engine.supply(InputValue::none());

    const Polyline* p = last_polyline(db);
    REQUIRE(p != nullptr);
    CHECK(std::abs(p->vertices()[0].bulge - 1.0) < kTol);
}

TEST_CASE("pline: the arc a bulge describes really passes through the points") {
    // The bulge is only meaningful if segment_arc() reads back what the
    // command wrote, so this closes the loop against the entity itself rather
    // than against another copy of the same formula.
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLINE"));

    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_keyword("ARC"));
    engine.supply(InputValue::of_keyword("SECOND"));
    engine.supply(InputValue::of_point({0, 10, 0}));
    engine.supply(InputValue::of_point({-10, 0, 0}));
    engine.supply(InputValue::none());

    const Polyline* p = last_polyline(db);
    REQUIRE(p != nullptr);

    Vec3 centre{};
    double radius = 0.0;
    double a0 = 0.0;
    double a1 = 0.0;
    REQUIRE(p->segment_arc(0, &centre, &radius, &a0, &a1));
    CHECK(near_equal(centre, Vec3{0, 0, 0}, 1e-6));
    CHECK(std::abs(radius - 10.0) < 1e-6);
}

TEST_CASE("pline: Radius refuses an endpoint the circle cannot reach") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLINE"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_keyword("ARC"));
    engine.supply(InputValue::of_keyword("RADIUS"));
    engine.supply(InputValue::of_real(1.0));
    // A chord of 10 cannot lie on a circle of radius 1.
    const EngineStatus status = engine.supply(InputValue::of_point({10, 0, 0}));
    CHECK(status == EngineStatus::Failed);
}

TEST_CASE("pline: Line returns to straight segments") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLINE"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_keyword("ARC"));
    engine.supply(InputValue::of_point({10, 10, 0}));
    engine.supply(InputValue::of_keyword("LINE"));
    engine.supply(InputValue::of_point({0, 10, 0}));
    engine.supply(InputValue::none());

    const Polyline* p = last_polyline(db);
    REQUIRE(p != nullptr);
    REQUIRE(p->size() == 4);
    CHECK(p->vertices()[0].bulge == 0.0);           // straight
    CHECK(std::abs(p->vertices()[1].bulge) > 0.5);  // the arc
    CHECK(p->vertices()[2].bulge == 0.0);           // straight again
}

// --- width ------------------------------------------------------------------

TEST_CASE("pline: Width applies to the segments drawn after it") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLINE"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_keyword("WIDTH"));
    engine.supply(InputValue::of_real(2.0));  // starting
    engine.supply(InputValue::of_real(2.0));  // ending
    engine.supply(InputValue::of_point({20, 0, 0}));
    engine.supply(InputValue::none());

    const Polyline* p = last_polyline(db);
    REQUIRE(p != nullptr);
    REQUIRE(p->size() == 3);
    // The first segment was drawn before the width was set.
    CHECK(p->vertices()[0].start_width == 0.0);
    CHECK(p->vertices()[1].start_width == 2.0);
    CHECK(p->has_width());
}

TEST_CASE("pline: Width tapers when the two answers differ") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLINE"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_keyword("WIDTH"));
    engine.supply(InputValue::of_real(4.0));
    engine.supply(InputValue::of_real(0.0));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::none());

    const Polyline* p = last_polyline(db);
    REQUIRE(p != nullptr);
    CHECK(p->vertices()[0].start_width == 4.0);
    CHECK(p->vertices()[0].end_width == 0.0);
}

TEST_CASE("pline: Halfwidth is half of Width, by definition") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLINE"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_keyword("HALFWIDTH"));
    engine.supply(InputValue::of_real(1.5));
    engine.supply(InputValue::of_real(1.5));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::none());

    const Polyline* p = last_polyline(db);
    REQUIRE(p != nullptr);
    CHECK(std::abs(p->vertices()[0].start_width - 3.0) < kTol);
}

TEST_CASE("pline: Length continues along the last segment's direction") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLINE"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({3, 4, 0}));  // direction (0.6, 0.8)
    engine.supply(InputValue::of_keyword("LENGTH"));
    engine.supply(InputValue::of_real(5.0));
    engine.supply(InputValue::none());

    const Polyline* p = last_polyline(db);
    REQUIRE(p != nullptr);
    REQUIRE(p->size() == 3);
    CHECK(near_equal(p->vertices()[2].pos, Vec3{6, 8, 0}, 1e-9));
}

TEST_CASE("pline: widths survive a DXF round trip") {
    // Width was added to PolyVertex for PLINE and PEDIT, which means groups 40
    // and 41 on every VERTEX. The reader has to give them back, or a wide
    // polyline silently becomes a hairline on reopening -- and a taper, which
    // uses both groups differently, is what catches a reader that only handles
    // the uniform case.
    Database source;
    auto p = std::make_unique<Polyline>();
    p->add({0, 0, 0}, 0.0, 2.0, 4.0);
    p->add({10, 0, 0}, 0.0, 4.0, 4.0);
    source.add(std::move(p));

    std::ostringstream out;
    DxfWriter w(out, source);
    w.write_document();

    Database loaded;
    const DxfReadResult r = read_dxf_text(loaded, out.str());
    CHECK(r.ok);
    REQUIRE(loaded.size() == 1);

    const Polyline* back = last_polyline(loaded);
    REQUIRE(back != nullptr);
    REQUIRE(back->size() == 2);
    CHECK_NEAR(back->vertices()[0].start_width, 2.0, 1e-9);
    CHECK_NEAR(back->vertices()[0].end_width, 4.0, 1e-9);
    CHECK_NEAR(back->vertices()[1].start_width, 4.0, 1e-9);
}

TEST_CASE("pline: a scale transform scales the widths with the geometry") {
    Polyline p;
    p.add({0, 0, 0}, 0.0, 2.0, 2.0);
    p.add({10, 0, 0}, 0.0, 2.0, 2.0);

    p.transform(Mat4::uniform_scaling(3.0));

    CHECK(std::abs(p.vertices()[0].start_width - 6.0) < 1e-9);
    CHECK(near_equal(p.vertices()[1].pos, Vec3{30, 0, 0}, 1e-9));
}
