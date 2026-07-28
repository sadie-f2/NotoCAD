// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/database.hpp"
#include "noto/entities.hpp"
#include "noto/render.hpp"

#include <cmath>
#include <memory>
#include <numbers>

using namespace noto;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kTol = 1e-9;

// A square, open: three straight segments.
Polyline open_square() {
    Polyline p;
    p.add({0, 0, 0});
    p.add({10, 0, 0});
    p.add({10, 10, 0});
    p.add({0, 10, 0});
    return p;
}

// Records what draw() emits, as test_render.cpp does.
class Capture final : public Renderer {
public:
    void begin_entity(const EntityProps&) override {}
    void polyline(const Vec3* pts, std::size_t count, bool closed) override {
        points.assign(pts, pts + count);
        was_closed = closed;
        ++calls;
    }
    std::vector<Vec3> points;
    bool was_closed{false};
    int calls{0};
};

bool has_snap(const std::vector<OsnapPoint>& pts, OsnapType t, const Vec3& at) {
    for (const OsnapPoint& p : pts) {
        if (p.type == t && near_equal(p.pos, at, 1e-6)) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("polyline: segment counts, open and closed") {
    Polyline p = open_square();
    CHECK(p.size() == 4);
    CHECK(p.segment_count() == 3);

    p.set_closed(true);
    CHECK(p.segment_count() == 4);

    Polyline one;
    one.add({0, 0, 0});
    CHECK(one.segment_count() == 0);  // a single vertex draws nothing
    CHECK(Polyline{}.segment_count() == 0);
}

TEST_CASE("polyline: length, straight and closed") {
    Polyline p = open_square();
    CHECK_NEAR(p.length(), 30.0, kTol);
    p.set_closed(true);
    CHECK_NEAR(p.length(), 40.0, kTol);
}

TEST_CASE("polyline: a bulge of one is a half circle") {
    // Bulge = tan(included/4); tan(pi/4) = 1, so included = pi.
    Polyline p;
    p.add({0, 0, 0}, 1.0);
    p.add({10, 0, 0});

    Vec3 c{};
    double r = 0.0;
    double a0 = 0.0;
    double a1 = 0.0;
    CHECK(p.segment_arc(0, &c, &r, &a0, &a1));

    // A half circle on a chord of ten has radius five, centred on the chord.
    CHECK_NEAR(r, 5.0, kTol);
    CHECK_VEC(c, 5.0, 0.0, 0.0, kTol);
    // Half the circumference.
    CHECK_NEAR(p.length(), kPi * 5.0, 1e-9);
}

TEST_CASE("polyline: a quarter-circle bulge, and its sign") {
    // tan(pi/8) is the bulge for a quarter turn.
    const double b = std::tan(kPi / 8.0);
    Polyline p;
    p.add({0, 0, 0}, b);
    p.add({10, 0, 0});

    Vec3 c{};
    double r = 0.0;
    double a0 = 0.0;
    double a1 = 0.0;
    CHECK(p.segment_arc(0, &c, &r, &a0, &a1));
    CHECK_NEAR(r, 10.0 / std::sqrt(2.0), 1e-9);

    // Positive bulge means counterclockwise from the first vertex to the
    // second. Travelling west to east, counterclockwise dips the arc BELOW the
    // chord -- so the centre sits above it. Easy to get backwards, and the
    // half-circle case cannot catch it because there the centre is on the
    // chord and the sign cancels.
    CHECK(c.y > 0.0);
    CHECK_VEC(c, 5.0, 5.0, 0.0, 1e-9);

    // The observable consequence: the arc's own midpoint is below the chord.
    std::vector<OsnapPoint> pts;
    p.osnap_points(pts);
    CHECK(has_snap(pts, OsnapType::Midpoint, {5.0, 5.0 - 10.0 / std::sqrt(2.0), 0.0}));

    // The same magnitude the other way mirrors both.
    Polyline q;
    q.add({0, 0, 0}, -b);
    q.add({10, 0, 0});
    CHECK(q.segment_arc(0, &c, &r, &a0, &a1));
    CHECK_VEC(c, 5.0, -5.0, 0.0, 1e-9);
}

TEST_CASE("polyline: a zero bulge is a straight segment") {
    Polyline p = open_square();
    Vec3 c{};
    double r = 0.0;
    double a0 = 0.0;
    double a1 = 0.0;
    CHECK(!p.segment_arc(0, &c, &r, &a0, &a1));
    // And so is an out-of-range index.
    CHECK(!p.segment_arc(99, &c, &r, &a0, &a1));
}

TEST_CASE("polyline: the bounding box contains the bulge, not just the vertices") {
    Polyline p;
    p.add({0, 0, 0}, 1.0);  // half circle bulging to y = -5
    p.add({10, 0, 0});

    const BBox b = p.bbox();
    CHECK(b.valid());
    // Vertices alone would give a box of zero height, and a too-small box makes
    // an entity unpickable exactly where it visibly is.
    CHECK_NEAR(b.min.y, -5.0, 1e-6);
    CHECK_NEAR(b.max.y, 0.0, 1e-6);
}

TEST_CASE("polyline: transform moves every vertex") {
    Polyline p = open_square();
    p.transform(Mat4::translation({1, 2, 3}));
    CHECK_VEC(p.vertices()[0].pos, 1.0, 2.0, 3.0, kTol);
    CHECK_VEC(p.vertices()[2].pos, 11.0, 12.0, 3.0, kTol);
    CHECK_NEAR(p.length(), 30.0, kTol);  // rigid: length preserved
}

TEST_CASE("polyline: a mirror reverses the arc sense") {
    Polyline p;
    p.add({0, 0, 0}, 1.0);
    p.add({10, 0, 0});

    p.transform(Mat4::mirror({0, 0, 0}, {0, 1, 0}));
    // The bulge's sign carries the arc's direction, so mirroring must flip it
    // or the arc would bulge the wrong way after reflection.
    CHECK_NEAR(p.vertices()[0].bulge, -1.0, kTol);
}

TEST_CASE("polyline: snaps are vertices, midpoints, and arc centres") {
    Polyline p = open_square();
    std::vector<OsnapPoint> pts;
    p.osnap_points(pts);

    CHECK(has_snap(pts, OsnapType::Endpoint, {0, 0, 0}));
    CHECK(has_snap(pts, OsnapType::Endpoint, {10, 10, 0}));
    CHECK(has_snap(pts, OsnapType::Midpoint, {5, 0, 0}));

    // An arc segment's midpoint is on the arc, not on the chord, and it brings
    // a centre snap with it.
    Polyline a;
    a.add({0, 0, 0}, 1.0);
    a.add({10, 0, 0});
    pts.clear();
    a.osnap_points(pts);
    CHECK(has_snap(pts, OsnapType::Center, {5, 0, 0}));
    CHECK(has_snap(pts, OsnapType::Midpoint, {5, -5, 0}));
    CHECK(!has_snap(pts, OsnapType::Midpoint, {5, 0, 0}));  // not the chord midpoint
}

TEST_CASE("polyline: one stretch grip per vertex") {
    Polyline p = open_square();
    std::vector<Grip> g;
    p.grips(g);
    CHECK(g.size() == 4);
    for (const Grip& x : g) CHECK(x.kind == GripKind::Stretch);

    // Moving one vertex leaves the others, which is the whole point.
    const GripIndex one = 1;
    p.stretch({0, 5, 0}, &one, 1);
    CHECK_VEC(p.vertices()[0].pos, 0.0, 0.0, 0.0, kTol);
    CHECK_VEC(p.vertices()[1].pos, 10.0, 5.0, 0.0, kTol);
}

TEST_CASE("polyline: naming every grip is the same as translating") {
    // The invariant every entity has to keep, so STRETCH degenerates into MOVE
    // rather than into nonsense.
    Polyline a = open_square();
    Polyline b = open_square();

    const GripIndex all[4] = {0, 1, 2, 3};
    a.stretch({3, -7, 2}, all, 4);
    b.transform(Mat4::translation({3, -7, 2}));

    for (std::size_t i = 0; i < 4; ++i) {
        CHECK_VEC(a.vertices()[i].pos, b.vertices()[i].pos.x, b.vertices()[i].pos.y,
                  b.vertices()[i].pos.z, kTol);
    }
}

TEST_CASE("polyline: draw emits one flattened run") {
    Polyline p = open_square();
    Capture cap;
    p.draw(DrawContext{}, cap);

    CHECK(cap.calls == 1);
    CHECK(!cap.was_closed);
    CHECK(cap.points.size() == 4);  // straight segments need no flattening

    p.set_closed(true);
    Capture closed;
    p.draw(DrawContext{}, closed);
    CHECK(closed.was_closed);
}

TEST_CASE("polyline: an arc segment is flattened into the same run") {
    Polyline p;
    p.add({0, 0, 0}, 1.0);
    p.add({10, 0, 0});

    Capture cap;
    DrawContext ctx;
    ctx.chord_tolerance = 0.01;
    p.draw(ctx, cap);

    CHECK(cap.calls == 1);
    // Many points, all on the half circle of radius five about (5,0).
    CHECK(cap.points.size() > 8);
    for (const Vec3& v : cap.points) {
        CHECK_NEAR(length(v - Vec3{5, 0, 0}), 5.0, 0.05);
    }
}

TEST_CASE("polyline: a degenerate polyline draws nothing") {
    Capture cap;
    Polyline{}.draw(DrawContext{}, cap);
    CHECK(cap.calls == 0);

    Polyline one;
    one.add({0, 0, 0});
    one.draw(DrawContext{}, cap);
    CHECK(cap.calls == 0);
}

TEST_CASE("polyline: clone is independent") {
    Polyline p = open_square();
    p.set_closed(true);
    const EntityPtr c = p.clone();

    CHECK(c->type() == EntityType::Polyline);
    const Polyline* q = static_cast<const Polyline*>(c.get());
    CHECK(q->size() == 4);
    CHECK(q->closed());

    p.vertices()[0].pos = Vec3{99, 99, 0};
    CHECK_VEC(q->vertices()[0].pos, 0.0, 0.0, 0.0, kTol);
}

TEST_CASE("polyline: it is one database entity, not one per vertex") {
    Database db;
    auto p = std::make_unique<Polyline>();
    for (int i = 0; i < 1000; ++i) p->add({static_cast<double>(i), 0, 0});
    db.add(std::move(p));

    // The whole point of owning the vertices: a thousand-vertex polyline costs
    // one handle, one undo record and one entry in the drawing order.
    CHECK(db.size() == 1);
    CHECK(db.order().size() == 1);
    CHECK(db.journal().undo_depth() == 1);
}

TEST_CASE("polyline: a major arc puts the centre on the far side") {
    // A bulge greater than one is an arc of more than half a turn. The centre
    // then crosses to the other side of the chord, which the signed apothem
    // handles with no special case -- and which a formula using cos(half)
    // instead of tan(half) would get wrong in the same way it gets the sign of
    // a minor arc wrong.
    Polyline p;
    p.add({0, 0, 0}, 2.0);
    p.add({10, 0, 0});

    Vec3 c{};
    double r = 0.0;
    double a0 = 0.0;
    double a1 = 0.0;
    CHECK(p.segment_arc(0, &c, &r, &a0, &a1));

    // Still counterclockwise, still bulging downward, but now wrapping past the
    // half turn, so the centre is below the chord rather than above it.
    CHECK(c.y < 0.0);
    CHECK_NEAR(length(Vec3{0, 0, 0} - c), r, 1e-9);
    CHECK_NEAR(length(Vec3{10, 0, 0} - c), r, 1e-9);

    // More than half a circle's worth of arc.
    CHECK(p.length() > kPi * r);
}
