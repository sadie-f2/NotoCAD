// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// 4x4 affine transform. Row-major storage, m[row][col], column-vector convention:
// a transformed point is M * p. This is the single slot every entity's transform()
// method takes, so MOVE/COPY/SCALE/MIRROR/ROTATE/ROTATE3D/ALIGN all share one path.
#pragma once

#include "noto/vec3.hpp"

namespace noto {

struct Mat4 {
    double m[4][4]{};

    static Mat4 identity();

    static Mat4 translation(const Vec3& t);
    static Mat4 scaling(const Vec3& s);
    static Mat4 uniform_scaling(double s) { return scaling({s, s, s}); }

    // Right-handed rotation of `angle` radians about `axis` through `origin`
    // (Rodrigues). This is ROTATE3D. `axis` need not be unit length.
    static Mat4 rotation(const Vec3& origin, const Vec3& axis, double angle);

    // Mirror across the plane defined by a point and its normal.
    static Mat4 mirror(const Vec3& point, const Vec3& normal);

    // Maps world coordinates into the frame with the given basis and origin.
    // The basis vectors are assumed orthonormal.
    static Mat4 from_basis(const Vec3& origin, const Vec3& ax, const Vec3& ay, const Vec3& az);

    Mat4 operator*(const Mat4& o) const;

    // Full affine transform, including translation.
    Vec3 transform_point(const Vec3& p) const;

    // Rotation/scale only; ignores translation. Use for directions and normals
    // (correct for the rigid and uniform-scale transforms the kernel produces).
    Vec3 transform_vector(const Vec3& v) const;

    // Inverse of a general affine matrix. `ok` is set false if singular, in
    // which case the identity is returned.
    Mat4 inverse(bool* ok = nullptr) const;

    Mat4 transposed() const;
};

bool near_equal(const Mat4& a, const Mat4& b, double eps = 1e-9);

}  // namespace noto
