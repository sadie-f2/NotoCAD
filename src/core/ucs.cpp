// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The user coordinate system.
//
// Small, because a UCS is only a frame. What makes it worth its own file is
// `normalized()`: everything else here assumes the axes are orthonormal and
// right-handed, and the current UCS arrives through system variables that
// AutoLISP can write, so that assumption has to be enforced somewhere rather
// than hoped for.
#include "ncad/tables.hpp"

#include <cmath>

namespace ncad {
namespace {

constexpr double kUcsEps = 1e-12;

}  // namespace

Vec3 Ucs::zdir() const {
    const Vec3 z = cross(xdir, ydir);
    return is_zero(z) ? kWorldZ : normalize(z);
}

bool Ucs::is_world() const {
    return is_zero(origin, 1e-12) && near_equal(normalize(xdir), kWorldX, 1e-12) &&
           near_equal(normalize(ydir), kWorldY, 1e-12);
}

Ucs Ucs::normalized() const {
    Ucs out;
    out.origin = origin;

    Vec3 x = xdir;
    if (is_zero(x)) x = kWorldX;
    x = normalize(x);

    // Gram-Schmidt: Y loses whatever component it had along X, so a UCS defined
    // by two points that are not quite perpendicular still yields a square
    // frame rather than a sheared one.
    Vec3 y = ydir - x * dot(ydir, x);
    if (is_zero(y)) {
        // Y was parallel to X, so it says nothing. Any perpendicular will do,
        // and the one furthest from X is the best conditioned.
        const Vec3 fallback = (std::abs(x.z) < 0.9) ? kWorldZ : kWorldX;
        y = cross(fallback, x);
    }
    y = normalize(y);

    out.xdir = x;
    out.ydir = y;
    return out;
}

Mat4 Ucs::to_world() const {
    const Ucs n = normalized();
    const Vec3 z = cross(n.xdir, n.ydir);

    // Columns are the UCS axes in world terms, so a UCS coordinate multiplies
    // out to the world point it names.
    Mat4 m = Mat4::identity();
    m.m[0][0] = n.xdir.x; m.m[1][0] = n.xdir.y; m.m[2][0] = n.xdir.z;
    m.m[0][1] = n.ydir.x; m.m[1][1] = n.ydir.y; m.m[2][1] = n.ydir.z;
    m.m[0][2] = z.x;      m.m[1][2] = z.y;      m.m[2][2] = z.z;
    m.m[0][3] = n.origin.x;
    m.m[1][3] = n.origin.y;
    m.m[2][3] = n.origin.z;
    return m;
}

Mat4 Ucs::from_world() const {
    const Ucs n = normalized();
    const Vec3 z = cross(n.xdir, n.ydir);

    // The inverse of an orthonormal frame is its transpose, with the
    // translation projected out -- which is exactly what Mat4::from_basis
    // builds, and is why that function exists.
    (void)kUcsEps;
    return Mat4::from_basis(n.origin, n.xdir, n.ydir, z);
}

}  // namespace ncad
