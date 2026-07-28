// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/database.hpp"
#include "noto/entities.hpp"
#include "noto/pick.hpp"
#include "noto/viewport.hpp"

#include <memory>

using namespace noto;

namespace {

// One world unit per pixel, centred on the origin, so a pixel distance and a
// world distance are the same number and the arithmetic stays checkable by eye.
Viewport plan_800x600() {
    Viewport v;
    v.set_size(800, 600);
    v.set_view_height(600.0);
    v.set_plan_view();
    v.set_target({0, 0, 0});
    return v;
}

// The screen point the world origin lands on: the middle of the viewport.
ScreenPoint centre() {
    return ScreenPoint{400.0, 300.0};
}

// Offset from the world origin in pixels. y is negated because screen y runs
// downward while world y runs up.
ScreenPoint at(double dx_px, double dy_px) {
    return ScreenPoint{400.0 + dx_px, 300.0 - dy_px};
}

double distance_to(const Entity& e, const Viewport& v, const ScreenPoint& p) {
    double d = -1.0;
    CHECK(entity_pick_distance(e, v, p, &d));
    return d;
}

}  // namespace

TEST_CASE("pick: pixel distance to a line") {
    const Viewport v = plan_800x600();
    const Line l{{-100, 0, 0}, {100, 0, 0}};

    // On the line.
    CHECK_NEAR(distance_to(l, v, centre()), 0.0, 1e-9);

    // Directly above its middle.
    CHECK_NEAR(distance_to(l, v, at(0, 10)), 10.0, 1e-9);
    CHECK_NEAR(distance_to(l, v, at(0, -10)), 10.0, 1e-9);

    // Beyond an end, so the nearest point is the endpoint itself and the
    // distance is the hypotenuse -- not the perpendicular to the infinite line.
    CHECK_NEAR(distance_to(l, v, at(130, 40)), 50.0, 1e-9);
}

TEST_CASE("pick: the measurement is to the segment, not the infinite line") {
    const Viewport v = plan_800x600();
    const Line l{{-100, 0, 0}, {-60, 0, 0}};

    // The perpendicular foot is far off the end of the segment. Measured
    // against the infinite line this would be 0; against the segment it is the
    // distance to the nearer endpoint.
    CHECK_NEAR(distance_to(l, v, at(0, 0)), 60.0, 1e-9);
}

TEST_CASE("pick: a circle is picked on its rim, not at its centre") {
    const Viewport v = plan_800x600();
    const Circle c{{0, 0, 0}, 100.0};

    // The rim. Tolerance is loose enough to absorb the chord sag, which is what
    // flattening costs and is far below any usable pick box.
    CHECK_NEAR(distance_to(c, v, at(100, 0)), 0.0, 0.5);
    CHECK_NEAR(distance_to(c, v, at(0, 100)), 0.0, 0.5);

    // The centre is a whole radius from the nearest drawn geometry. This is the
    // consequence of measuring the wireframe, and it is R12's behaviour.
    CHECK_NEAR(distance_to(c, v, centre()), 100.0, 0.5);

    // Just outside the rim.
    CHECK_NEAR(distance_to(c, v, at(110, 0)), 10.0, 0.5);
}

TEST_CASE("pick: an arc is not picked outside its sweep") {
    const Viewport v = plan_800x600();
    // The first quadrant only.
    const Arc a{{0, 0, 0}, 100.0, 0.0, 1.5707963267948966};

    CHECK_NEAR(distance_to(a, v, at(100, 0)), 0.0, 0.5);   // start point
    CHECK_NEAR(distance_to(a, v, at(0, 100)), 0.0, 0.5);   // end point

    // Where the arc would be if it were a whole circle. The nearest drawn point
    // is the start or end of the sweep, 100*sqrt(2) away from (-100, 0).
    CHECK_NEAR(distance_to(a, v, at(-100, 0)), 141.4213562, 0.5);
}

TEST_CASE("pick: distance in pixels is invariant under zoom") {
    Viewport v = plan_800x600();
    const Line l{{-100, 0, 0}, {100, 0, 0}};

    // Ten pixels above the line at one world unit per pixel.
    const double before = distance_to(l, v, at(0, 10));
    CHECK_NEAR(before, 10.0, 1e-9);

    // Zoom in by four. The line is four times longer on screen, but a cursor
    // ten pixels above it is still ten pixels above it.
    v.set_view_height(150.0);
    CHECK_NEAR(distance_to(l, v, at(0, 10)), 10.0, 1e-9);
}

TEST_CASE("pick: the broad phase rejects a distant cursor") {
    const Viewport v = plan_800x600();
    const Line l{{-10, 0, 0}, {10, 0, 0}};

    CHECK(entity_near_cursor(l, v, centre(), 3.0));
    CHECK(entity_near_cursor(l, v, at(12, 0), 3.0));   // inside the padded box
    CHECK(!entity_near_cursor(l, v, at(20, 0), 3.0));  // outside it
    CHECK(!entity_near_cursor(l, v, at(0, 100), 3.0));

    // The pad is what makes a zero-thickness horizontal line pickable at all:
    // its screen box has no height until it is inflated.
    CHECK(!entity_near_cursor(l, v, at(0, 5), 3.0));
    CHECK(entity_near_cursor(l, v, at(0, 2), 3.0));
}

TEST_CASE("pick: finds an entity within the pick box and misses outside it") {
    Database db;
    const Viewport v = plan_800x600();
    const Handle h = db.add(std::make_unique<Line>(Vec3{-100, 0, 0}, Vec3{100, 0, 0}));

    const PickResult on = pick_entity(db, v, centre(), 3.0);
    CHECK(on.hit());
    CHECK(on.entity == h);
    CHECK_NEAR(on.distance_px, 0.0, 1e-9);

    // Two pixels away is inside a three-pixel box; ten is not.
    CHECK(pick_entity(db, v, at(0, 2), 3.0).hit());
    CHECK(!pick_entity(db, v, at(0, 10), 3.0).hit());

    // A bigger pick box reaches further, which is the whole point of PICKBOX.
    CHECK(pick_entity(db, v, at(0, 10), 12.0).hit());
}

TEST_CASE("pick: an empty drawing hits nothing") {
    Database db;
    const Viewport v = plan_800x600();
    const PickResult r = pick_entity(db, v, centre(), 3.0);
    CHECK(!r.hit());
    CHECK(r.entity == kNullHandle);
}

TEST_CASE("pick: the topmost entity wins, not the nearest") {
    Database db;
    const Viewport v = plan_800x600();

    // Two lines through the same place. The second is drawn on top.
    db.add(std::make_unique<Line>(Vec3{-100, 0, 0}, Vec3{100, 0, 0}));
    const Handle top = db.add(std::make_unique<Line>(Vec3{-100, 1, 0}, Vec3{100, 1, 0}));

    // The cursor is nearer the first line, but the second is on top of it.
    const PickResult r = pick_entity(db, v, centre(), 5.0);
    CHECK(r.hit());
    CHECK(r.entity == top);
    CHECK_NEAR(r.distance_px, 1.0, 1e-9);
}

TEST_CASE("pick: entities on layers that are off or frozen are not pickable") {
    Database db;
    const Viewport v = plan_800x600();

    const LayerId hidden = db.add_layer("HIDDEN");
    auto line = std::make_unique<Line>(Vec3{-100, 0, 0}, Vec3{100, 0, 0});
    line->props().layer = hidden;
    const Handle h = db.add(std::move(line));

    CHECK(pick_entity(db, v, centre(), 3.0).entity == h);

    // Off: R12 spells it as a negative colour.
    db.set_layer_color(hidden, -7);
    CHECK(!pick_entity(db, v, centre(), 3.0).hit());

    db.set_layer_color(hidden, 7);
    db.set_layer_frozen(hidden, true);
    CHECK(!pick_entity(db, v, centre(), 3.0).hit());

    // Locked is still pickable: R12 permits the selection and refuses the edit.
    db.set_layer_frozen(hidden, false);
    db.set_layer_locked(hidden, true);
    CHECK(pick_entity(db, v, centre(), 3.0).entity == h);
}

TEST_CASE("pick: an edge-on circle is still pickable along its projection") {
    Viewport v = plan_800x600();
    // A circle in the XZ plane, seen from directly above: it projects to a
    // horizontal line through the origin, 200 pixels long.
    Circle c{{0, 0, 0}, 100.0};
    c.props().normal = Vec3{0, 1, 0};

    CHECK_NEAR(distance_to(c, v, centre()), 0.0, 0.5);
    CHECK_NEAR(distance_to(c, v, at(0, 20)), 20.0, 0.5);

    // And beyond the end of that projected line.
    CHECK_NEAR(distance_to(c, v, at(130, 0)), 30.0, 0.5);
}
