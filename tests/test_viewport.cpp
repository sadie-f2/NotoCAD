// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/entities.hpp"
#include "noto/database.hpp"
#include "noto/scene.hpp"
#include "noto/render.hpp"
#include "noto/viewport.hpp"

#include <cmath>
#include <numbers>

#include <memory>

using namespace noto;

namespace {

Viewport plan_800x600() {
    Viewport v;
    v.set_size(800, 600);
    v.set_view_height(600.0);  // one world unit per pixel, to keep sums obvious
    v.set_plan_view();
    return v;
}

}  // namespace

TEST_CASE("the default view is plan, exactly") {
    // Not approximately: 2D drafting happens here, and a fraction of a degree
    // off vertical would leave axis-aligned geometry askew on screen.
    Viewport v = plan_800x600();
    const Basis b = v.basis();
    CHECK_VEC(b.az, 0.0, 0.0, 1.0, 1e-15);
    CHECK_VEC(b.ax, 1.0, 0.0, 0.0, 1e-15);
    CHECK_VEC(b.ay, 0.0, 1.0, 0.0, 1e-15);
}

TEST_CASE("the view basis is orthonormal and right-handed at any orientation") {
    Viewport v;
    v.set_size(800, 600);
    for (double az = -3.0; az < 3.0; az += 0.7) {
        for (double el = -1.5; el < 1.5; el += 0.4) {
            v.set_azimuth(az);
            v.set_elevation(el);
            const Basis b = v.basis();
            CHECK_NEAR(length(b.ax), 1.0, 1e-12);
            CHECK_NEAR(length(b.ay), 1.0, 1e-12);
            CHECK_NEAR(length(b.az), 1.0, 1e-12);
            CHECK_NEAR(dot(b.ax, b.ay), 0.0, 1e-12);
            CHECK_NEAR(dot(b.ay, b.az), 0.0, 1e-12);
            CHECK_NEAR(dot(b.az, b.ax), 0.0, 1e-12);
            CHECK(dot(cross(b.ax, b.ay), b.az) > 0.0);
        }
    }
}

TEST_CASE("world Z projects upward from every non-polar elevation") {
    // The reason `right` is written from the azimuth rather than derived by a
    // cross product: the horizon must stay horizontal as you orbit.
    Viewport v;
    v.set_size(800, 600);
    for (double el = -1.4; el < 1.4; el += 0.35) {
        v.set_azimuth(0.9);
        v.set_elevation(el);
        const Basis b = v.basis();
        CHECK_NEAR(b.ax.z, 0.0, 1e-12);  // screen-right is horizontal
        CHECK(b.ay.z > 0.0);             // world up is on-screen up
    }
}

TEST_CASE("the target projects to the centre of the viewport") {
    Viewport v = plan_800x600();
    v.set_target({12.0, -7.0, 3.0});
    const ScreenPoint sp = v.project(v.target());
    CHECK_NEAR(sp.x, 400.0, 1e-9);
    CHECK_NEAR(sp.y, 300.0, 1e-9);
}

TEST_CASE("screen y runs downward while world up runs up") {
    Viewport v = plan_800x600();
    // In plan view, +Y in the world must come out ABOVE centre, i.e. smaller y.
    CHECK(v.project({0.0, 10.0, 0.0}).y < 300.0);
    CHECK(v.project({10.0, 0.0, 0.0}).x > 400.0);
}

TEST_CASE("project and unproject round-trip through an arbitrary view") {
    Viewport v;
    v.set_size(1024, 768);
    v.set_view_height(250.0);
    v.set_target({5.0, 5.0, 5.0});
    v.set_azimuth(0.6);
    v.set_elevation(0.4);

    const ScreenPoint sp{321.0, 654.0};
    const Vec3 world = v.unproject_to_target_plane(sp);
    const ScreenPoint back = v.project(world);
    CHECK_NEAR(back.x, sp.x, 1e-9);
    CHECK_NEAR(back.y, sp.y, 1e-9);
}

TEST_CASE("unproject lands on the requested plane") {
    Viewport v;
    v.set_size(800, 600);
    v.set_view_height(100.0);
    v.set_azimuth(0.3);
    v.set_elevation(0.5);

    const Vec3 plane_point{0.0, 0.0, 12.0};
    Vec3 hit{};
    CHECK(v.unproject({100.0, 500.0}, plane_point, kWorldZ, &hit));
    CHECK_NEAR(hit.z, 12.0, 1e-9);
    // And it must still project back to where it was picked.
    const ScreenPoint back = v.project(hit);
    CHECK_NEAR(back.x, 100.0, 1e-9);
    CHECK_NEAR(back.y, 500.0, 1e-9);
}

TEST_CASE("unproject fails on a plane seen edge-on") {
    Viewport v = plan_800x600();
    // Looking straight down, a vertical plane is a line on screen: there is no
    // single point to return, and silently returning one would put picked
    // points anywhere along it.
    Vec3 hit{};
    CHECK(!v.unproject({10.0, 10.0}, Vec3{0, 0, 0}, kWorldX, &hit));
    CHECK(!v.unproject({10.0, 10.0}, Vec3{0, 0, 0}, Vec3{0, 0, 0}, &hit));
}

TEST_CASE("pan moves the drawing with the cursor") {
    Viewport v = plan_800x600();  // 1 world unit per pixel
    const Vec3 marker{0.0, 0.0, 0.0};
    const ScreenPoint before = v.project(marker);
    v.pan_pixels(30.0, 20.0);
    const ScreenPoint after = v.project(marker);
    CHECK_NEAR(after.x - before.x, 30.0, 1e-9);
    CHECK_NEAR(after.y - before.y, 20.0, 1e-9);
}

TEST_CASE("zoom about a point holds that point still") {
    // This is what makes wheel zoom feel anchored rather than drifting.
    Viewport v;
    v.set_size(800, 600);
    v.set_view_height(100.0);
    v.set_target({3.0, 4.0, 0.0});
    v.set_azimuth(0.8);
    v.set_elevation(0.9);

    const ScreenPoint focus{620.0, 130.0};
    const Vec3 anchored = v.unproject_to_target_plane(focus);
    v.zoom(2.5, focus);

    const ScreenPoint after = v.project(anchored);
    CHECK_NEAR(after.x, focus.x, 1e-8);
    CHECK_NEAR(after.y, focus.y, 1e-8);
    CHECK_NEAR(v.view_height(), 40.0, 1e-12);
}

TEST_CASE("zoom ignores degenerate factors") {
    Viewport v = plan_800x600();
    const double h = v.view_height();
    v.zoom(0.0);
    v.zoom(-3.0);
    CHECK_NEAR(v.view_height(), h, 1e-12);
}

TEST_CASE("zoom extents frames the whole drawing") {
    Viewport v = plan_800x600();

    BBox box;
    box.expand({-10.0, -5.0, 0.0});
    box.expand({30.0, 25.0, 0.0});
    v.zoom_extents(box);

    CHECK_VEC(v.target(), box.center().x, box.center().y, box.center().z, 1e-12);

    // Every corner must land inside the viewport.
    for (int i = 0; i < 4; ++i) {
        const Vec3 corner{(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y, 0.0};
        const ScreenPoint sp = v.project(corner);
        CHECK(sp.x >= 0.0 && sp.x <= 800.0);
        CHECK(sp.y >= 0.0 && sp.y <= 600.0);
    }
}

TEST_CASE("zoom extents fits a box that is wide rather than tall") {
    // The aspect-ratio branch: a box wider than the viewport must be fitted by
    // width, which means a view HEIGHT larger than the box's own height.
    Viewport v = plan_800x600();
    BBox box;
    box.expand({-500.0, -1.0, 0.0});
    box.expand({500.0, 1.0, 0.0});
    v.zoom_extents(box);

    for (int i = 0; i < 4; ++i) {
        const Vec3 corner{(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y, 0.0};
        const ScreenPoint sp = v.project(corner);
        CHECK(sp.x >= 0.0 && sp.x <= 800.0);
        CHECK(sp.y >= 0.0 && sp.y <= 600.0);
    }
}

TEST_CASE("zoom extents leaves an empty or extentless drawing alone") {
    Viewport v = plan_800x600();
    const double h = v.view_height();

    v.zoom_extents(BBox{});  // nothing drawn yet
    CHECK_NEAR(v.view_height(), h, 1e-12);

    BBox point;
    point.expand({5.0, 6.0, 7.0});
    v.zoom_extents(point);
    CHECK_NEAR(v.view_height(), h, 1e-12);  // zoom kept
    CHECK_VEC(v.target(), 5.0, 6.0, 7.0, 1e-12);  // but centred
}

TEST_CASE("set_view_direction is VPOINT and round-trips") {
    Viewport v;
    v.set_size(800, 600);
    const Vec3 d = normalize(Vec3{1.0, -2.0, 3.0});
    v.set_view_direction(d);
    CHECK_VEC(v.view_direction(), d.x, d.y, d.z, 1e-12);
}

TEST_CASE("a straight-down view direction keeps the heading it had") {
    // atan2(0, 0) would invent an azimuth; VPOINT 0,0,1 must instead preserve
    // the orientation already on screen.
    Viewport v;
    v.set_size(800, 600);
    v.set_azimuth(1.234);
    v.set_view_direction(kWorldZ);
    CHECK_NEAR(v.azimuth(), 1.234, 1e-12);
    CHECK_NEAR(v.elevation(), Viewport::kMaxElevation, 1e-12);
}

TEST_CASE("a degenerate view direction is ignored") {
    Viewport v = plan_800x600();
    v.set_view_direction({0.0, 0.0, 0.0});
    CHECK_VEC(v.view_direction(), 0.0, 0.0, 1.0, 1e-15);
}

TEST_CASE("orbit drags the near surface with the cursor") {
    Viewport v;
    v.set_size(800, 600);
    v.set_azimuth(0.0);
    v.set_elevation(0.0);

    v.orbit_pixels(10.0, 0.0);
    CHECK(v.azimuth() > std::numbers::pi);  // wrapped: decreased below zero

    v.set_azimuth(0.0);
    v.set_elevation(0.0);
    v.orbit_pixels(0.0, 10.0);
    CHECK(v.elevation() > 0.0);  // dragging down tips the top into view
}

TEST_CASE("orbit stops at the poles instead of tumbling over") {
    Viewport v;
    v.set_size(800, 600);
    v.orbit_pixels(0.0, 10000.0);
    CHECK_NEAR(v.elevation(), Viewport::kMaxElevation, 1e-12);
    v.orbit_pixels(0.0, -20000.0);
    CHECK_NEAR(v.elevation(), -Viewport::kMaxElevation, 1e-12);
}

TEST_CASE("a zero-sized viewport still projects finitely") {
    // Qt hands out zero-sized widgets during startup and teardown; that is a
    // state to survive, not a caller error.
    Viewport v;
    v.set_size(0, 0);
    CHECK(v.width() >= 1);
    CHECK(v.height() >= 1);
    CHECK(std::isfinite(v.world_per_pixel()));
    CHECK(std::isfinite(v.project({1.0, 2.0, 3.0}).x));
}

TEST_CASE("the draw context tightens as the view zooms in") {
    Viewport v = plan_800x600();
    const double coarse = v.draw_context().chord_tolerance;
    v.zoom(10.0);
    CHECK(v.draw_context().chord_tolerance < coarse);
    // Half a pixel of sag, in world units.
    CHECK_NEAR(v.draw_context().chord_tolerance, v.world_per_pixel() * 0.5, 1e-15);
}

TEST_CASE("view height is clamped away from denormals") {
    Viewport v = plan_800x600();
    for (int i = 0; i < 200; ++i) v.zoom(10.0);
    CHECK(v.view_height() > 0.0);
    CHECK(std::isfinite(v.world_per_pixel()));
    CHECK(v.world_per_pixel() > 0.0);
}

TEST_CASE("viewport: the cached basis follows the camera") {
    // project() runs once per POINT, so basis() is cached against the two
    // angles it derives from. The cache validates itself by comparing them
    // rather than being invalidated by every mutator -- the one mutator that
    // forgot would render the whole drawing through a stale basis.
    Viewport vp;
    vp.set_size(800, 600);

    const Basis plan = vp.basis();
    CHECK(vp.basis().ax.x == plan.ax.x);  // repeated calls agree

    vp.set_azimuth(vp.azimuth() + 1.0);
    const Basis turned = vp.basis();
    CHECK(std::abs(turned.ax.x - plan.ax.x) > 1e-9);

    // And back again gives the original, so the cache is not merely sticky.
    vp.set_azimuth(vp.azimuth() - 1.0);
    CHECK_NEAR(vp.basis().ax.x, plan.ax.x, 1e-12);
    CHECK_NEAR(vp.basis().ay.y, plan.ay.y, 1e-12);
}

// --- view culling -----------------------------------------------------------
//
// draw_database used to flatten and project every entity in the drawing
// whatever the viewport was showing, and let QPainter throw the pixels away.

namespace {

struct CountingRenderer final : Renderer {
    void begin_entity(const EntityProps&) override {}
    void polyline(const Vec3*, std::size_t, bool) override { ++runs; }
    std::size_t runs{0};
};

std::size_t drawn(const Database& db, const DrawContext& ctx) {
    CountingRenderer r;
    draw_database(db, ctx, r);
    return r.runs;
}

}  // namespace

TEST_CASE("culling: what is off screen is not drawn, what is on screen is") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 10, 0}));       // at the target
    db.add(std::make_unique<Line>(Vec3{9000, 9000, 0}, Vec3{9010, 9010, 0}));  // far away

    Viewport vp;
    vp.set_size(800, 600);
    vp.set_target(Vec3{5, 5, 0});
    vp.set_view_height(100.0);

    CHECK(drawn(db, vp.draw_context()) == 1);

    // Move the view to the other one and the answer swaps, rather than both
    // being drawn or neither.
    vp.set_target(Vec3{9005, 9005, 0});
    CHECK(drawn(db, vp.draw_context()) == 1);

    // Wide enough for both.
    vp.set_target(Vec3{4500, 4500, 0});
    vp.set_view_height(30000.0);
    CHECK(drawn(db, vp.draw_context()) == 2);
}

TEST_CASE("culling: an entity straddling the edge is drawn") {
    Database db;
    Viewport vp;
    vp.set_size(800, 600);
    vp.set_target(Vec3{0, 0, 0});
    vp.set_view_height(100.0);

    // Half in, half out. Culling it would lose the visible half, which is the
    // failure that looks like corrupt geometry rather than a fast redraw.
    const double half_h = 50.0;
    db.add(std::make_unique<Line>(Vec3{0, half_h - 5.0, 0}, Vec3{0, half_h + 500.0, 0}));
    CHECK(drawn(db, vp.draw_context()) == 1);
}

TEST_CASE("culling: a rotated view is why the clip is in VIEW space") {
    // The case a world-space box test gets wrong. Turned off axis, the visible
    // volume is a slab whose world AABB is enormous -- so a world test either
    // culls nothing or culls things that are plainly on screen.
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 1, 0}));

    Viewport vp;
    vp.set_size(800, 600);
    vp.set_target(Vec3{0, 0, 0});
    vp.set_view_height(100.0);
    vp.set_azimuth(0.7);

    // At the target, so it is on screen at every rotation.
    CHECK(drawn(db, vp.draw_context()) == 1);
    vp.set_azimuth(2.4);
    CHECK(drawn(db, vp.draw_context()) == 1);
    vp.set_azimuth(-1.1);
    CHECK(drawn(db, vp.draw_context()) == 1);
}

TEST_CASE("culling: a context with no viewport draws everything") {
    // The default DrawContext has no clip, which is what the DXF write and the
    // hit-test probes rely on -- they drive draw() with no viewport at all and
    // must see every entity.
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 1, 0}));
    db.add(std::make_unique<Line>(Vec3{99999, 99999, 0}, Vec3{99999, 99998, 0}));

    CHECK(drawn(db, DrawContext{}) == 2);
}

TEST_CASE("culling: the skip list is honoured, however large") {
    Database db;
    std::vector<Handle> skip;
    for (int i = 0; i < 50; ++i) {
        const Handle h = db.add(std::make_unique<Line>(Vec3{0, double(i), 0}, Vec3{5, double(i), 0}));
        if (i % 2 == 0) skip.push_back(h);
    }

    Viewport vp;
    vp.set_size(800, 600);
    vp.set_target(Vec3{2, 25, 0});
    vp.set_view_height(200.0);

    CountingRenderer r;
    draw_database(db, vp.draw_context(), r, skip);
    CHECK(r.runs == 25);
}
