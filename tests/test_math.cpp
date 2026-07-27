// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/ecs.hpp"
#include "noto/mat4.hpp"
#include "noto/vec3.hpp"

#include <numbers>

using namespace noto;

namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kTol = 1e-12;
}  // namespace

TEST_CASE("vec3: dot and cross follow the right-hand rule") {
    CHECK_NEAR(dot(kWorldX, kWorldY), 0.0, kTol);
    CHECK_VEC(cross(kWorldX, kWorldY), 0.0, 0.0, 1.0, kTol);
    CHECK_VEC(cross(kWorldY, kWorldZ), 1.0, 0.0, 0.0, kTol);
    CHECK_NEAR(length(Vec3{3, 4, 0}), 5.0, kTol);
    CHECK(is_zero(normalize(Vec3{0, 0, 0})));
}

TEST_CASE("mat4: rotation about an off-origin axis") {
    // 90 degrees about a Z-parallel axis through (1,0,0) sends the origin to (1,-1,0).
    const Mat4 r = Mat4::rotation(Vec3{1, 0, 0}, kWorldZ, kPi / 2.0);
    CHECK_VEC(r.transform_point(Vec3{0, 0, 0}), 1.0, -1.0, 0.0, 1e-12);
    CHECK_VEC(r.transform_point(Vec3{1, 0, 0}), 1.0, 0.0, 0.0, 1e-12);
}

TEST_CASE("mat4: ROTATE3D about an arbitrary axis is a rigid motion") {
    const Vec3 axis{1, 1, 1};
    const Mat4 r = Mat4::rotation(Vec3{2, -3, 5}, axis, 0.7);

    // Lengths are preserved.
    const Vec3 a = r.transform_point(Vec3{4, 1, -2});
    const Vec3 b = r.transform_point(Vec3{-1, 3, 6});
    CHECK_NEAR(length(a - b), length(Vec3{4, 1, -2} - Vec3{-1, 3, 6}), 1e-12);

    // Three 120-degree turns about (1,1,1) return to the identity.
    const Mat4 t = Mat4::rotation(Vec3{}, axis, 2.0 * kPi / 3.0);
    CHECK(near_equal(t * t * t, Mat4::identity(), 1e-12));
}

TEST_CASE("mat4: inverse round-trips an affine transform") {
    const Mat4 m = Mat4::translation({3, -2, 7}) *
                   Mat4::rotation(Vec3{}, Vec3{0.3, 1.0, -0.5}, 1.1) *
                   Mat4::scaling({2.0, 2.0, 2.0});
    bool ok = false;
    const Mat4 inv = m.inverse(&ok);
    CHECK(ok);
    CHECK(near_equal(m * inv, Mat4::identity(), 1e-10));

    bool singular_ok = true;
    Mat4::scaling({1, 0, 1}).inverse(&singular_ok);
    CHECK(!singular_ok);
}

TEST_CASE("mat4: mirror across a plane is an involution") {
    const Mat4 m = Mat4::mirror(Vec3{0, 0, 0}, kWorldZ);
    CHECK_VEC(m.transform_point(Vec3{1, 2, 3}), 1.0, 2.0, -3.0, kTol);
    CHECK(near_equal(m * m, Mat4::identity(), 1e-12));
}

TEST_CASE("ecs: world Z normal reproduces the world basis") {
    // The canonical case: an entity in the world XY plane must round-trip
    // unchanged, or every 2D drawing serialises wrong.
    const Basis b = arbitrary_axis(kWorldZ);
    CHECK_VEC(b.ax, 1.0, 0.0, 0.0, kTol);
    CHECK_VEC(b.ay, 0.0, 1.0, 0.0, kTol);
    CHECK_VEC(b.az, 0.0, 0.0, 1.0, kTol);
}

TEST_CASE("ecs: negative world Z takes the world-Y branch") {
    const Basis b = arbitrary_axis(Vec3{0, 0, -1});
    CHECK_VEC(b.ax, -1.0, 0.0, 0.0, kTol);
    CHECK_VEC(b.ay, 0.0, 1.0, 0.0, kTol);
    CHECK_VEC(b.az, 0.0, 0.0, -1.0, kTol);
}

TEST_CASE("ecs: derived basis is always orthonormal and right-handed") {
    const Vec3 normals[] = {
        {0, 0, 1},   {0, 0, -1},  {1, 0, 0},      {0, 1, 0},
        {1, 1, 1},   {-2, 5, -3}, {0.001, 0.001, 1.0},  // just inside the 1/64 branch
        {0.02, 0.0, 1.0},                              // just outside it
        {0.4, -0.9, 0.2},
    };
    for (const Vec3& n : normals) {
        const Basis b = arbitrary_axis(n);
        CHECK_NEAR(length(b.ax), 1.0, 1e-12);
        CHECK_NEAR(length(b.ay), 1.0, 1e-12);
        CHECK_NEAR(length(b.az), 1.0, 1e-12);
        CHECK_NEAR(dot(b.ax, b.ay), 0.0, 1e-12);
        CHECK_NEAR(dot(b.ay, b.az), 0.0, 1e-12);
        CHECK_NEAR(dot(b.ax, b.az), 0.0, 1e-12);
        // Right-handed: ax cross ay == az.
        CHECK_VEC(cross(b.ax, b.ay), b.az.x, b.az.y, b.az.z, 1e-12);
        // az is the normalised input normal.
        CHECK_VEC(b.az, normalize(n).x, normalize(n).y, normalize(n).z, 1e-12);
    }
}

TEST_CASE("ecs: world<->ecs transforms are mutual inverses") {
    const Vec3 normal{0.3, -0.7, 0.5};
    const Mat4 w2e = world_to_ecs(normal);
    const Mat4 e2w = ecs_to_world(normal);
    CHECK(near_equal(w2e * e2w, Mat4::identity(), 1e-12));

    // A point on the entity plane has zero ECS z, which is exactly what lets
    // R12 store it as a 2D coordinate plus group code 210.
    const Basis b = arbitrary_axis(normal);
    const Vec3 on_plane = b.ax * 3.0 + b.ay * -4.0;
    const Vec3 ecs = w2e.transform_point(on_plane);
    CHECK_NEAR(ecs.x, 3.0, 1e-12);
    CHECK_NEAR(ecs.y, -4.0, 1e-12);
    CHECK_NEAR(ecs.z, 0.0, 1e-12);
}
