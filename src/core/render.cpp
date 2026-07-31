// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/render.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace noto {

bool DrawContext::visible(const BBox& box) const {
    if (!clip_active) return true;
    // An entity with no extent -- nothing has one yet, but a future one might --
    // is drawn rather than guessed about.
    if (!box.valid()) return true;

    const Vec3 centre = (box.min + box.max) * 0.5;
    const Vec3 half = (box.max - box.min) * 0.5;
    const Vec3 d = centre - clip_origin;

    // The exact extent of an axis-aligned box along an arbitrary direction:
    // its centre's projection, give or take the half-extents weighted by the
    // magnitudes of the direction's components. Two of these replace projecting
    // all eight corners.
    const double cx = dot(d, clip_x);
    const double rx = half.x * std::abs(clip_x.x) + half.y * std::abs(clip_x.y) +
                      half.z * std::abs(clip_x.z);
    if (cx + rx < clip_min_x || cx - rx > clip_max_x) return false;

    const double cy = dot(d, clip_y);
    const double ry = half.x * std::abs(clip_y.x) + half.y * std::abs(clip_y.y) +
                      half.z * std::abs(clip_y.z);
    if (cy + ry < clip_min_y || cy - ry > clip_max_y) return false;

    return true;
}

namespace {

// One segment per 45 degrees, whatever the tolerance says. A circle three
// pixels wide is still recognisably round rather than a triangle, and the cost
// of the floor is trivial.
constexpr double kMaxSegmentAngle = std::numbers::pi / 4.0;

}  // namespace

int arc_segment_count(double radius, double sweep, double chord_tolerance) {
    const double abs_sweep = std::abs(sweep);
    if (abs_sweep < kEps) return kMinArcSegments;

    // The angular floor, before tolerance is considered at all.
    double n = std::ceil(abs_sweep / kMaxSegmentAngle);

    // Sagitta of a chord subtending angle t on radius r is r*(1 - cos(t/2)),
    // so the largest t within tolerance is t = 2*acos(1 - tol/r). That form is
    // written here as 4*asin(sqrt(x/2)) via acos(1-x) = 2*asin(sqrt(x/2)):
    // computing 1 - tol/r directly cancels to exactly 1.0 once the ratio drops
    // below the double epsilon, and acos(1.0) is 0, which would silently
    // discard the tolerance and flatten a huge circle into eight segments.
    //
    // A tolerance at or beyond the radius constrains nothing, and a degenerate
    // radius has no curvature to resolve; the angular floor covers both.
    if (chord_tolerance > 0.0 && radius > kEps && chord_tolerance < radius) {
        const double x = chord_tolerance / radius;
        const double max_angle = 4.0 * std::asin(std::sqrt(x * 0.5));
        if (max_angle > 0.0) n = std::max(n, std::ceil(abs_sweep / max_angle));
    }

    if (!(n >= static_cast<double>(kMinArcSegments))) return kMinArcSegments;  // NaN-safe
    if (n > static_cast<double>(kMaxArcSegments)) return kMaxArcSegments;
    return static_cast<int>(n);
}

}  // namespace noto
