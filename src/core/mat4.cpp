// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/mat4.hpp"

#include <cmath>

namespace noto {

Mat4 Mat4::identity() {
    Mat4 r;
    for (int i = 0; i < 4; ++i) r.m[i][i] = 1.0;
    return r;
}

Mat4 Mat4::translation(const Vec3& t) {
    Mat4 r = identity();
    r.m[0][3] = t.x;
    r.m[1][3] = t.y;
    r.m[2][3] = t.z;
    return r;
}

Mat4 Mat4::scaling(const Vec3& s) {
    Mat4 r;
    r.m[0][0] = s.x;
    r.m[1][1] = s.y;
    r.m[2][2] = s.z;
    r.m[3][3] = 1.0;
    return r;
}

Mat4 Mat4::rotation(const Vec3& origin, const Vec3& axis, double angle) {
    const Vec3 u = normalize(axis);
    if (is_zero(u)) return identity();

    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double t = 1.0 - c;

    Mat4 r = identity();
    r.m[0][0] = t * u.x * u.x + c;
    r.m[0][1] = t * u.x * u.y - s * u.z;
    r.m[0][2] = t * u.x * u.z + s * u.y;
    r.m[1][0] = t * u.x * u.y + s * u.z;
    r.m[1][1] = t * u.y * u.y + c;
    r.m[1][2] = t * u.y * u.z - s * u.x;
    r.m[2][0] = t * u.x * u.z - s * u.y;
    r.m[2][1] = t * u.y * u.z + s * u.x;
    r.m[2][2] = t * u.z * u.z + c;

    // Conjugate by the translation so the axis passes through `origin`.
    return translation(origin) * r * translation(-origin);
}

Mat4 Mat4::mirror(const Vec3& point, const Vec3& normal) {
    const Vec3 n = normalize(normal);
    if (is_zero(n)) return identity();

    Mat4 r = identity();
    for (int i = 0; i < 3; ++i) {
        const double ni = (i == 0) ? n.x : (i == 1) ? n.y : n.z;
        for (int j = 0; j < 3; ++j) {
            const double nj = (j == 0) ? n.x : (j == 1) ? n.y : n.z;
            r.m[i][j] = ((i == j) ? 1.0 : 0.0) - 2.0 * ni * nj;
        }
    }
    return translation(point) * r * translation(-point);
}

Mat4 Mat4::from_basis(const Vec3& origin, const Vec3& ax, const Vec3& ay, const Vec3& az) {
    Mat4 r = identity();
    r.m[0][0] = ax.x; r.m[0][1] = ax.y; r.m[0][2] = ax.z;
    r.m[1][0] = ay.x; r.m[1][1] = ay.y; r.m[1][2] = ay.z;
    r.m[2][0] = az.x; r.m[2][1] = az.y; r.m[2][2] = az.z;
    r.m[0][3] = -dot(ax, origin);
    r.m[1][3] = -dot(ay, origin);
    r.m[2][3] = -dot(az, origin);
    return r;
}

Mat4 Mat4::operator*(const Mat4& o) const {
    Mat4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) sum += m[i][k] * o.m[k][j];
            r.m[i][j] = sum;
        }
    }
    return r;
}

Vec3 Mat4::transform_point(const Vec3& p) const {
    return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
            m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
            m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3]};
}

Vec3 Mat4::transform_vector(const Vec3& v) const {
    return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
}

Mat4 Mat4::transposed() const {
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) r.m[i][j] = m[j][i];
    return r;
}

Mat4 Mat4::inverse(bool* ok) const {
    // Affine only: invert the 3x3 linear part, then re-apply to the translation.
    const double a = m[0][0], b = m[0][1], c = m[0][2];
    const double d = m[1][0], e = m[1][1], f = m[1][2];
    const double g = m[2][0], h = m[2][1], i = m[2][2];

    const double c00 =  (e * i - f * h);
    const double c01 = -(d * i - f * g);
    const double c02 =  (d * h - e * g);

    const double det = a * c00 + b * c01 + c * c02;
    if (std::fabs(det) < 1e-14) {
        if (ok) *ok = false;
        return identity();
    }
    if (ok) *ok = true;

    const double inv = 1.0 / det;
    Mat4 r = identity();
    r.m[0][0] = c00 * inv;
    r.m[0][1] = -(b * i - c * h) * inv;
    r.m[0][2] =  (b * f - c * e) * inv;
    r.m[1][0] = c01 * inv;
    r.m[1][1] =  (a * i - c * g) * inv;
    r.m[1][2] = -(a * f - c * d) * inv;
    r.m[2][0] = c02 * inv;
    r.m[2][1] = -(a * h - b * g) * inv;
    r.m[2][2] =  (a * e - b * d) * inv;

    const Vec3 t{m[0][3], m[1][3], m[2][3]};
    const Vec3 nt = r.transform_vector(t);
    r.m[0][3] = -nt.x;
    r.m[1][3] = -nt.y;
    r.m[2][3] = -nt.z;
    return r;
}

bool near_equal(const Mat4& a, const Mat4& b, double eps) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (std::fabs(a.m[i][j] - b.m[i][j]) > eps) return false;
    return true;
}

}  // namespace noto
