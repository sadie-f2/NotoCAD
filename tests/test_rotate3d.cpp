// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/commands.hpp"
#include "noto/database.hpp"
#include "noto/dxf.hpp"
#include "noto/dxf_read.hpp"
#include "noto/entities.hpp"
#include "noto/view_control.hpp"

#include <memory>
#include <numbers>
#include <sstream>

using namespace noto;

namespace {

constexpr double kPi = std::numbers::pi;

const Line* line_at(const Database& db, std::size_t i) {
    return static_cast<const Line*>(db.get(db.order()[i]));
}

// Selects everything and stops at the axis prompt.
void select_all(CommandEngine& engine) {
    engine.begin(make_command("ROTATE3D"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
}

class FixedView final : public ViewControl {
public:
    void set_plan_view(const Vec3&) override {}
    void zoom_extents() override {}
    void zoom_window(const Vec3&, const Vec3&) override {}
    void zoom_scale(double) override {}
    bool zoom_previous() override { return false; }
    void pan(const Vec3&, const Vec3&) override {}
    void set_view_direction(const Vec3&) override {}
    Vec3 view_direction() const override { return kWorldZ; }
    Basis view_basis() const override { return Basis{{1, 0, 0}, {0, 0, 1}, {0, -1, 0}}; }
    DrawContext draw_context() const override { return DrawContext{}; }
};

}  // namespace

TEST_CASE("rotate3d: two points define the axis") {
    Database db;
    CommandEngine engine(db);
    // A line along +X, to be tipped up about the X axis itself.
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 5, 0}, Vec3{0, 5, 0}));  // a point-like probe
    db.erase(db.order().back());

    db.add(std::make_unique<Line>(Vec3{0, 5, 0}, Vec3{10, 5, 0}));

    select_all(engine);
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({1, 0, 0}));  // the world X axis
    engine.supply(InputValue::of_real(90.0));
    CHECK(engine.status() == EngineStatus::Finished);

    // The line at y = 5 swings up to z = 5. This is the whole point of the
    // command: geometry deliberately leaving the construction plane.
    CHECK_VEC(line_at(db, 1)->start(), 0.0, 0.0, 5.0, 1e-9);
    // The line on the axis does not move.
    CHECK_VEC(line_at(db, 0)->end(), 10.0, 0.0, 0.0, 1e-9);
}

TEST_CASE("rotate3d: the named axes, through a point") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 1, 0}, Vec3{0, 1, 0} + Vec3{1, 0, 0}));

    select_all(engine);
    engine.supply(InputValue::of_keyword("XAXIS"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(90.0));

    CHECK_VEC(line_at(db, 0)->start(), 0.0, 0.0, 1.0, 1e-9);
}

TEST_CASE("rotate3d: Zaxis matches what plain ROTATE would do") {
    Database a;
    CommandEngine ea(a);
    a.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));
    select_all(ea);
    ea.supply(InputValue::of_keyword("ZAXIS"));
    ea.supply(InputValue::of_point({0, 0, 0}));
    ea.supply(InputValue::of_real(90.0));

    Database b;
    CommandEngine eb(b);
    b.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));
    eb.begin(make_command("ROTATE"));
    eb.supply(InputValue::of_keyword("ALL"));
    eb.supply(InputValue::none());
    eb.supply(InputValue::of_point({0, 0, 0}));
    eb.supply(InputValue::of_real(90.0));

    // ROTATE is the special case of ROTATE3D where the axis is the plane
    // normal, so the two must agree exactly.
    const Vec3 x = line_at(a, 0)->end();
    CHECK_VEC(line_at(b, 0)->end(), x.x, x.y, x.z, 1e-12);
}

TEST_CASE("rotate3d: an axis taken from an entity") {
    Database db;
    CommandEngine engine(db);
    const Handle axis = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{5, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 2, 0}, Vec3{4, 2, 0}));

    engine.begin(make_command("ROTATE3D"));
    engine.supply(InputValue::of_entity(db.order()[1]));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_keyword("ENTITY"));
    engine.supply(InputValue::of_entity(axis));
    engine.supply(InputValue::of_real(90.0));

    CHECK_VEC(line_at(db, 1)->start(), 0.0, 0.0, 2.0, 1e-9);
}

TEST_CASE("rotate3d: a circle's axis is the one it turns about") {
    Database db;
    CommandEngine engine(db);
    const Handle c = db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 5.0, Vec3{1, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 2, 0}, Vec3{0, 2, 0} + Vec3{0, 0, 1}));

    engine.begin(make_command("ROTATE3D"));
    engine.supply(InputValue::of_entity(db.order()[1]));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_keyword("ENTITY"));
    engine.supply(InputValue::of_entity(c));
    engine.supply(InputValue::of_real(90.0));

    // The circle's normal is +X, so this is a rotation about the world X axis.
    CHECK_VEC(line_at(db, 1)->start(), 0.0, 0.0, 2.0, 1e-9);
}

TEST_CASE("rotate3d: an entity with no axis is refused") {
    Database db;
    CommandEngine engine(db);
    const Handle t = db.add(std::make_unique<Text>(Vec3{0, 0, 0}, "no axis", 1.0));
    db.add(std::make_unique<Line>(Vec3{0, 2, 0}, Vec3{4, 2, 0}));

    engine.begin(make_command("ROTATE3D"));
    engine.supply(InputValue::of_entity(db.order()[1]));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_keyword("ENTITY"));
    engine.supply(InputValue::of_entity(t));
    CHECK(engine.status() == EngineStatus::Failed);
}

TEST_CASE("rotate3d: the View axis comes from the view") {
    Database db;
    CommandEngine engine(db);
    FixedView view;
    engine.set_view_control(&view);
    db.add(std::make_unique<Line>(Vec3{1, 0, 0}, Vec3{2, 0, 0}));

    select_all(engine);
    engine.supply(InputValue::of_keyword("VIEW"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(90.0));

    // The view looks along -Y, so a spin about it turns things in the XZ plane.
    CHECK_NEAR(line_at(db, 0)->start().y, 0.0, 1e-9);
    CHECK_NEAR(length(line_at(db, 0)->start()), 1.0, 1e-9);
}

TEST_CASE("rotate3d: View without a view says so") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));

    select_all(engine);
    engine.supply(InputValue::of_keyword("VIEW"));
    // `ncad` has no display, and reporting a rotation that did not happen would
    // be a lie a script could not detect.
    CHECK(engine.status() == EngineStatus::Failed);
}

TEST_CASE("rotate3d: two identical points are not an axis") {
    Database db;
    CommandEngine engine(db);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));

    select_all(engine);
    engine.supply(InputValue::of_point({3, 3, 3}));
    engine.supply(InputValue::of_point({3, 3, 3}));
    CHECK(engine.status() == EngineStatus::Failed);
}

TEST_CASE("rotate3d: it is one undo step, and handles survive") {
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 1, 0}, Vec3{4, 1, 0}));

    select_all(engine);
    engine.supply(InputValue::of_keyword("XAXIS"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(90.0));
    CHECK_VEC(line_at(db, 0)->start(), 0.0, 0.0, 1.0, 1e-9);

    CHECK(db.get(h) != nullptr);  // rotated in place, so an ename stays valid
    CHECK(db.journal().undo(db));
    CHECK_VEC(line_at(db, 0)->start(), 0.0, 1.0, 0.0, 1e-9);
}

TEST_CASE("rotate3d: a bulged polyline out of plane survives a DXF round trip") {
    Database db;
    CommandEngine engine(db);
    auto p = std::make_unique<Polyline>();
    p->add({0, 0, 0}, 0.5);
    p->add({10, 0, 0});
    p->add({10, 10, 0});
    db.add(std::move(p));

    select_all(engine);
    engine.supply(InputValue::of_keyword("XAXIS"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(60.0));

    const Polyline* before = static_cast<const Polyline*>(db.get(db.order()[0]));
    const Vec3 v2 = before->vertices()[2].pos;
    const double bulge = before->vertices()[0].bulge;
    const double len = before->length();

    std::ostringstream out;
    DxfWriter w(out, db);
    w.write_document();

    Database loaded;
    read_dxf_text(loaded, out.str());
    const Polyline* after = static_cast<const Polyline*>(loaded.get(loaded.order()[0]));

    // The reason this command is worth having as a test and not only as a
    // feature: it exercises ECS, the bulge sign and the arbitrary-axis
    // algorithm at once, and any of the three being subtly wrong shows up as a
    // shape that changes when saved.
    CHECK(after->size() == 3);
    CHECK_VEC(after->vertices()[2].pos, v2.x, v2.y, v2.z, 1e-9);
    CHECK_NEAR(after->vertices()[0].bulge, bulge, 1e-9);
    CHECK_NEAR(after->length(), len, 1e-9);
}

TEST_CASE("rotate3d: Last reuses the axis of the previous rotation") {
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 5, 0}, Vec3{10, 5, 0}));

    // A quarter turn about the world X axis through the origin.
    engine.begin(make_command("ROTATE3D"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_keyword("XAXIS"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(90.0));

    const Line* once = static_cast<const Line*>(db.get(h));
    REQUIRE(once != nullptr);
    CHECK_NEAR(once->start().z, 5.0, 1e-9);

    // The same axis again, named only as Last.
    engine.begin(make_command("ROTATE3D"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_keyword("LAST"));
    engine.supply(InputValue::of_real(90.0));

    const Line* twice = static_cast<const Line*>(db.get(h));
    REQUIRE(twice != nullptr);
    // Two quarter turns about X: y = 5 has become y = -5.
    CHECK_NEAR(twice->start().y, -5.0, 1e-9);
    CHECK_NEAR(twice->start().z, 0.0, 1e-9);
}

TEST_CASE("rotate3d: Last with no previous rotation fails") {
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    engine.begin(make_command("ROTATE3D"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());
    const EngineStatus status = engine.supply(InputValue::of_keyword("LAST"));
    CHECK(status == EngineStatus::Failed);
}

TEST_CASE("rotate3d: a cancelled rotation leaves no last axis") {
    // Recorded on apply, not when the axis was chosen.
    Database db;
    CommandEngine engine(db);
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    engine.begin(make_command("ROTATE3D"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_keyword("XAXIS"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::cancel());

    CHECK(!engine.memory().has_last_axis);
}
