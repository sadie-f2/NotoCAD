// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/ecs.hpp"

#include <cmath>

namespace noto {

Basis arbitrary_axis(const Vec3& normal) {
    const Vec3 az = normalize(normal);
    if (is_zero(az)) return {kWorldX, kWorldY, kWorldZ};

    // If the normal is close to the world Z axis (either direction), the world Z
    // axis is a poor reference to cross against, so use world Y instead. This
    // branch is what keeps the derived basis continuous and repeatable.
    const Vec3 ax = normalize(
        (std::fabs(az.x) < kArbitraryAxisThreshold && std::fabs(az.y) < kArbitraryAxisThreshold)
            ? cross(kWorldY, az)
            : cross(kWorldZ, az));

    const Vec3 ay = normalize(cross(az, ax));
    return {ax, ay, az};
}

Mat4 world_to_ecs(const Vec3& normal) {
    const Basis b = arbitrary_axis(normal);
    return Mat4::from_basis(Vec3{}, b.ax, b.ay, b.az);
}

Mat4 ecs_to_world(const Vec3& normal) {
    const Basis b = arbitrary_axis(normal);
    Mat4 r = Mat4::identity();
    r.m[0][0] = b.ax.x; r.m[0][1] = b.ay.x; r.m[0][2] = b.az.x;
    r.m[1][0] = b.ax.y; r.m[1][1] = b.ay.y; r.m[1][2] = b.az.y;
    r.m[2][0] = b.ax.z; r.m[2][1] = b.ay.z; r.m[2][2] = b.az.z;
    return r;
}

}  // namespace noto
