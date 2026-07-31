// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "ncad/entities.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace ncad;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kTol = 1e-12;

std::vector<Grip> grips_of(const Entity& e) {
    std::vector<Grip> g;
    e.grips(g);
    return g;
}

void pull(Entity& e, const Vec3& delta, std::initializer_list<GripIndex> which) {
    const std::vector<GripIndex> v(which);
    e.stretch(delta, v.data(), v.size());
}

const Grip* find(const std::vector<Grip>& g, GripIndex i) {
    for (const Grip& x : g) {
        if (x.index == i) return &x;
    }
    return nullptr;
}

std::size_t count_kind(const std::vector<Grip>& g, GripKind k) {
    return static_cast<std::size_t>(
        std::count_if(g.begin(), g.end(), [&](const Grip& x) { return x.kind == k; }));
}

}  // namespace

TEST_CASE("grip: a line's three grips and what they mean") {
    const Line l{{0, 0, 0}, {10, 0, 0}};
    const std::vector<Grip> g = grips_of(l);
    CHECK(g.size() == 3);

    CHECK_VEC(find(g, 0)->pos, 0.0, 0.0, 0.0, kTol);
    CHECK_VEC(find(g, 1)->pos, 10.0, 0.0, 0.0, kTol);
    CHECK_VEC(find(g, 2)->pos, 5.0, 0.0, 0.0, kTol);

    // The endpoints reshape; the midpoint is there to pick the line up bodily.
    CHECK(find(g, 0)->kind == GripKind::Stretch);
    CHECK(find(g, 1)->kind == GripKind::Stretch);
    CHECK(find(g, 2)->kind == GripKind::Move);
}

TEST_CASE("grip: stretching one endpoint of a line leaves the other alone") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    pull(l, {0, 5, 0}, {0});

    CHECK_VEC(l.start(), 0.0, 5.0, 0.0, kTol);
    CHECK_VEC(l.end(), 10.0, 0.0, 0.0, kTol);  // untouched

    // This is the operation transform(Mat4) cannot express, which is the whole
    // reason the vtable grew.
    pull(l, {0, -5, 0}, {1});
    CHECK_VEC(l.start(), 0.0, 5.0, 0.0, kTol);
    CHECK_VEC(l.end(), 10.0, -5.0, 0.0, kTol);
}

TEST_CASE("grip: naming every stretch grip is the same as translating") {
    // The property that makes STRETCH degenerate into MOVE rather than into
    // nonsense when the selection is not a crossing window.
    Line a{{1, 2, 3}, {10, -4, 6}};
    Line b{{1, 2, 3}, {10, -4, 6}};

    const Vec3 d{3, -7, 2};
    pull(a, d, {0, 1});
    b.transform(Mat4::translation(d));

    CHECK_VEC(a.start(), b.start().x, b.start().y, b.start().z, kTol);
    CHECK_VEC(a.end(), b.end().x, b.end().y, b.end().z, kTol);
}

TEST_CASE("grip: a line's midpoint grip moves the whole line") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    pull(l, {0, 0, 4}, {2});
    CHECK_VEC(l.start(), 0.0, 0.0, 4.0, kTol);
    CHECK_VEC(l.end(), 10.0, 0.0, 4.0, kTol);
    CHECK_NEAR(l.length(), 10.0, kTol);  // length preserved: it moved, not stretched
}

TEST_CASE("grip: unknown indices are ignored, not an error") {
    Line l{{0, 0, 0}, {10, 0, 0}};
    pull(l, {5, 5, 5}, {99});
    CHECK_VEC(l.start(), 0.0, 0.0, 0.0, kTol);
    CHECK_VEC(l.end(), 10.0, 0.0, 0.0, kTol);

    // And an empty list does nothing at all.
    l.stretch({5, 5, 5}, nullptr, 0);
    CHECK_VEC(l.start(), 0.0, 0.0, 0.0, kTol);
}

TEST_CASE("grip: a circle has a centre and four quadrants") {
    const Circle c{{0, 0, 0}, 10.0};
    const std::vector<Grip> g = grips_of(c);
    CHECK(g.size() == 5);

    // A circle cannot be reshaped, only moved or resized, so nothing is Stretch.
    CHECK(count_kind(g, GripKind::Stretch) == 0);
    CHECK(count_kind(g, GripKind::Move) == 1);
    CHECK(count_kind(g, GripKind::Radius) == 4);

    CHECK(find(g, 0)->kind == GripKind::Move);
    CHECK_VEC(find(g, 0)->pos, 0.0, 0.0, 0.0, kTol);
    CHECK_VEC(find(g, 1)->pos, 10.0, 0.0, 0.0, kTol);
}

TEST_CASE("grip: a circle's centre grip moves it without resizing") {
    Circle c{{0, 0, 0}, 10.0};
    pull(c, {3, 4, 0}, {0});
    CHECK_VEC(c.center(), 3.0, 4.0, 0.0, kTol);
    CHECK_NEAR(c.radius(), 10.0, kTol);
}

TEST_CASE("grip: a circle's quadrant grip resizes it about the centre") {
    Circle c{{0, 0, 0}, 10.0};

    // Drag the +X quadrant five units further out.
    pull(c, {5, 0, 0}, {1});
    CHECK_NEAR(c.radius(), 15.0, kTol);
    CHECK_VEC(c.center(), 0.0, 0.0, 0.0, kTol);  // the centre stays put

    // And back in.
    pull(c, {-10, 0, 0}, {1});
    CHECK_NEAR(c.radius(), 5.0, kTol);
}

TEST_CASE("grip: a quadrant drag out of plane is projected into it") {
    Circle c{{0, 0, 0}, 10.0};

    // A pure Z drag has no component in the circle's plane, so the radius is
    // unchanged rather than the circle becoming something CIRCLE cannot store.
    pull(c, {0, 0, 7}, {1});
    CHECK_NEAR(c.radius(), 10.0, kTol);
    CHECK_VEC(c.props().normal, 0.0, 0.0, 1.0, kTol);
}

TEST_CASE("grip: a tilted circle resizes in its own plane") {
    // Normal along X, so the circle lies in the YZ plane.
    Circle c{{0, 0, 0}, 10.0, {1, 0, 0}};
    const std::vector<Grip> g = grips_of(c);

    // The first quadrant is one radius along the plane's own X axis.
    const Vec3 q = find(g, 1)->pos;
    CHECK_NEAR(length(q - c.center()), 10.0, 1e-9);

    // Dragging it outward along its own direction doubles the radius.
    pull(c, normalize(q - c.center()) * 10.0, {1});
    CHECK_NEAR(c.radius(), 20.0, 1e-9);
}

TEST_CASE("grip: an arc's four grips") {
    const Arc a{{0, 0, 0}, 10.0, 0.0, kPi * 0.5};
    const std::vector<Grip> g = grips_of(a);
    CHECK(g.size() == 4);

    CHECK(find(g, 0)->kind == GripKind::Stretch);  // start
    CHECK(find(g, 1)->kind == GripKind::Stretch);  // end
    CHECK(find(g, 2)->kind == GripKind::Move);     // midpoint of the sweep
    CHECK(find(g, 3)->kind == GripKind::Move);     // centre

    CHECK_VEC(find(g, 0)->pos, 10.0, 0.0, 0.0, 1e-9);
    CHECK_VEC(find(g, 1)->pos, 0.0, 10.0, 0.0, 1e-9);
    CHECK_VEC(find(g, 3)->pos, 0.0, 0.0, 0.0, kTol);
}

TEST_CASE("grip: an arc's centre and midpoint grips move the whole arc") {
    Arc a{{0, 0, 0}, 10.0, 0.0, kPi * 0.5};
    const double sweep_before = a.sweep();

    pull(a, {5, 0, 0}, {3});
    CHECK_VEC(a.center(), 5.0, 0.0, 0.0, kTol);
    CHECK_NEAR(a.radius(), 10.0, kTol);
    CHECK_NEAR(a.sweep(), sweep_before, kTol);

    pull(a, {0, 5, 0}, {2});
    CHECK_VEC(a.center(), 5.0, 5.0, 0.0, kTol);
    CHECK_NEAR(a.sweep(), sweep_before, kTol);
}

TEST_CASE("grip: an arc endpoint slides along its own circle") {
    // Quarter arc from +X round to +Y.
    Arc a{{0, 0, 0}, 10.0, 0.0, kPi * 0.5};

    // Drag the end point round toward -X. Centre and radius are preserved and
    // the start does not move; only the included angle changes.
    pull(a, {-10, -10, 0}, {1});

    CHECK_VEC(a.center(), 0.0, 0.0, 0.0, kTol);
    CHECK_NEAR(a.radius(), 10.0, kTol);
    CHECK_VEC(a.start_point(), 10.0, 0.0, 0.0, 1e-9);
    CHECK_NEAR(a.end_angle(), kPi, 1e-9);
    CHECK_NEAR(a.sweep(), kPi, 1e-9);
}

TEST_CASE("grip: an arc caught at both ends moves rather than reshaping") {
    Arc a{{0, 0, 0}, 10.0, 0.0, kPi * 0.5};
    const double sweep_before = a.sweep();

    // What a crossing window enclosing the whole arc produces.
    pull(a, {2, 3, 0}, {0, 1});
    CHECK_VEC(a.center(), 2.0, 3.0, 0.0, kTol);
    CHECK_NEAR(a.radius(), 10.0, kTol);
    CHECK_NEAR(a.sweep(), sweep_before, kTol);
}

TEST_CASE("grip: dragging an arc endpoint onto the centre is refused") {
    Arc a{{0, 0, 0}, 10.0, 0.0, kPi * 0.5};

    // There is no angle to take, so nothing changes rather than a NaN landing
    // in the angle.
    pull(a, {-10, 0, 0}, {0});
    CHECK_NEAR(a.start_angle(), 0.0, kTol);
    CHECK(std::isfinite(a.start_angle()));
}

TEST_CASE("grip: every entity's grips and stretch agree on indices") {
    // The contract: every index grips() reports is one stretch() acts on. A
    // grip nothing responds to would be invisible until someone dragged it.
    Line l{{0, 0, 0}, {10, 0, 0}};
    Circle c{{0, 0, 0}, 10.0};
    Arc arc{{0, 0, 0}, 10.0, 0.0, kPi * 0.5};
    Entity* all[3] = {&l, &c, &arc};

    for (Entity* e : all) {
        const std::vector<Grip> g = grips_of(*e);
        CHECK(!g.empty());

        for (const Grip& grip : g) {
            const EntityPtr before = e->clone();
            const GripIndex idx = grip.index;
            e->stretch({1, 1, 0}, &idx, 1);

            // Something must have changed: bboxes differing is a cheap proxy
            // that does not need to know which kind of entity this is.
            const BBox a = before->bbox();
            const BBox b = e->bbox();
            const bool moved = length(a.min - b.min) > 1e-9 || length(a.max - b.max) > 1e-9;
            CHECK(moved);

            // Restore for the next grip.
            e->stretch({-1, -1, 0}, &idx, 1);
        }
    }
}
