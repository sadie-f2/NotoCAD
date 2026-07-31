// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Core 3D vector type. Trivially copyable, constexpr-friendly, doubles throughout.
#pragma once

#include <cmath>

namespace ncad {

// Global linear tolerance. AutoCAD R12 compared to roughly this scale; every
// geometric equality test in the kernel routes through here rather than ==.
inline constexpr double kEps = 1e-10;

struct Vec3 {
    double x{}, y{}, z{};

    constexpr Vec3() = default;
    constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }

    constexpr Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    constexpr Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
};

constexpr Vec3 operator*(double s, const Vec3& v) { return v * s; }

constexpr double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

constexpr double length_sq(const Vec3& v) { return dot(v, v); }

inline double length(const Vec3& v) { return std::sqrt(dot(v, v)); }

// Returns the zero vector for degenerate input; callers that care must check.
inline Vec3 normalize(const Vec3& v) {
    const double len = length(v);
    return (len > kEps) ? v / len : Vec3{};
}

inline bool near_equal(const Vec3& a, const Vec3& b, double eps = kEps) {
    return length_sq(a - b) <= eps * eps;
}

inline bool is_zero(const Vec3& v, double eps = kEps) {
    return length_sq(v) <= eps * eps;
}

// Named world axes, used by the arbitrary axis algorithm.
inline constexpr Vec3 kWorldX{1.0, 0.0, 0.0};
inline constexpr Vec3 kWorldY{0.0, 1.0, 0.0};
inline constexpr Vec3 kWorldZ{0.0, 0.0, 1.0};

}  // namespace ncad
