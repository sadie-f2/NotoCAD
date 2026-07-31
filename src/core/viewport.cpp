// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/viewport.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ncad {
namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

// Smallest view height allowed. Zooming is multiplicative, so without a floor a
// long enough zoom-in reaches denormals and the projection loses all precision.
constexpr double kMinViewHeight = 1e-9;
constexpr double kMaxViewHeight = 1e12;

// Leaves a little air around ZOOM Extents, as R12 does.
constexpr double kExtentsMargin = 1.05;

// Radians of orbit per pixel dragged. A full turn in roughly 720 pixels.
constexpr double kOrbitRadiansPerPixel = kTwoPi / 720.0;

double wrap_angle(double a) {
    a = std::fmod(a, kTwoPi);
    return (a < 0.0) ? a + kTwoPi : a;
}

}  // namespace

void Viewport::set_size(int width_px, int height_px) {
    // A zero-sized widget is a real state during Qt startup and teardown, not an
    // error; clamping keeps world_per_pixel finite so callers need no guard.
    width_px_ = std::max(1, width_px);
    height_px_ = std::max(1, height_px);
}

void Viewport::set_view_height(double h) {
    view_height_ = std::clamp(h, kMinViewHeight, kMaxViewHeight);
}

void Viewport::set_elevation(double radians) {
    elevation_ = std::clamp(radians, -kMaxElevation, kMaxElevation);
}

void Viewport::set_view_direction(const Vec3& d) {
    const Vec3 u = normalize(d);
    if (is_zero(u)) return;

    set_elevation(std::asin(std::clamp(u.z, -1.0, 1.0)));

    // Straight up or down carries no heading: atan2(0, 0) would invent one, so
    // the existing azimuth is kept instead. This is what makes VPOINT 0,0,1
    // preserve the orientation you were already looking from.
    const double horizontal = std::hypot(u.x, u.y);
    if (horizontal > kEps) azimuth_ = std::atan2(u.y, u.x);
}

Vec3 Viewport::view_direction() const {
    const double ce = std::cos(elevation_);
    return {ce * std::cos(azimuth_), ce * std::sin(azimuth_), std::sin(elevation_)};
}

void Viewport::set_plan_view() {
    azimuth_ = -std::numbers::pi / 2.0;
    elevation_ = kMaxElevation;
}

Basis Viewport::basis() const {
    // Recomputed only when the angles it depends on have moved; see the
    // members for why the cache validates itself rather than being invalidated.
    if (azimuth_ == cached_azimuth_ && elevation_ == cached_elevation_) return cached_basis_;

    // `right` is horizontal by construction, which is what keeps the world Z
    // axis vertical on screen at every elevation. Writing it from the azimuth
    // rather than as cross(worldZ, dir) is what keeps it defined at the poles.
    const Vec3 az = view_direction();
    const Vec3 ax{-std::sin(azimuth_), std::cos(azimuth_), 0.0};
    const Vec3 ay = cross(az, ax);

    cached_basis_ = {ax, ay, az};
    cached_azimuth_ = azimuth_;
    cached_elevation_ = elevation_;
    return cached_basis_;
}

Mat4 Viewport::world_to_view() const {
    const Basis b = basis();
    return Mat4::from_basis(target_, b.ax, b.ay, b.az);
}

double Viewport::world_per_pixel() const {
    return view_height_ / static_cast<double>(height_px_);
}

DrawContext Viewport::draw_context() const {
    DrawContext ctx;
    ctx.chord_tolerance = world_per_pixel() * 0.5;

    // The visible rectangle, in view space and in world units. A couple of
    // pixels of slack on every side, so an entity straddling the edge is drawn
    // rather than popping in and out as the view moves by a fraction.
    const Basis b = basis();
    const double half_w = world_per_pixel() * static_cast<double>(width_px_) * 0.5;
    const double half_h = world_per_pixel() * static_cast<double>(height_px_) * 0.5;
    const double slack = world_per_pixel() * 2.0;

    ctx.clip_active = true;
    ctx.clip_origin = target_;
    ctx.clip_x = b.ax;
    ctx.clip_y = b.ay;
    ctx.clip_min_x = -half_w - slack;
    ctx.clip_max_x = half_w + slack;
    ctx.clip_min_y = -half_h - slack;
    ctx.clip_max_y = half_h + slack;
    return ctx;
}

ScreenPoint Viewport::project(const Vec3& world) const {
    const Basis b = basis();
    const Vec3 offset = world - target_;
    const double scale = 1.0 / world_per_pixel();
    return {static_cast<double>(width_px_) * 0.5 + dot(offset, b.ax) * scale,
            // Negated: screen y runs downward while the view basis runs up.
            static_cast<double>(height_px_) * 0.5 - dot(offset, b.ay) * scale};
}

bool Viewport::unproject(const ScreenPoint& sp, const Vec3& plane_point, const Vec3& plane_normal,
                         Vec3* out) const {
    const Vec3 n = normalize(plane_normal);
    if (is_zero(n)) return false;

    const Basis b = basis();
    // Parallel projection: every screen point is a ray along the view axis, so
    // the origin varies and the direction does not.
    const Vec3 origin = unproject_to_target_plane(sp);
    const Vec3 dir = -b.az;

    const double denom = dot(dir, n);
    if (std::abs(denom) < kEps) return false;  // plane seen edge-on

    const double t = dot(plane_point - origin, n) / denom;
    if (out) *out = origin + dir * t;
    return true;
}

Vec3 Viewport::unproject_to_target_plane(const ScreenPoint& sp) const {
    const Basis b = basis();
    const double wpp = world_per_pixel();
    const double vx = (sp.x - static_cast<double>(width_px_) * 0.5) * wpp;
    const double vy = (static_cast<double>(height_px_) * 0.5 - sp.y) * wpp;
    return target_ + b.ax * vx + b.ay * vy;
}

void Viewport::pan_pixels(double dx, double dy) {
    const Basis b = basis();
    const double wpp = world_per_pixel();
    // The camera moves opposite the drag, so the drawing follows the cursor.
    target_ -= b.ax * (dx * wpp);
    target_ += b.ay * (dy * wpp);
}

void Viewport::zoom(double factor) {
    if (factor > kEps) set_view_height(view_height_ / factor);
}

void Viewport::zoom(double factor, const ScreenPoint& focus) {
    if (factor <= kEps) return;

    // Hold the world point under the cursor still: find it, rescale, then shift
    // the target by however far it drifted.
    const Vec3 before = unproject_to_target_plane(focus);
    set_view_height(view_height_ / factor);
    const Vec3 after = unproject_to_target_plane(focus);
    target_ += before - after;
}

void Viewport::zoom_extents(const BBox& box) {
    if (!box.valid()) return;

    target_ = box.center();

    // Measure the box in view space by projecting its eight corners; the
    // extents of a rotated box are not its axis-aligned size.
    const Basis b = basis();
    double half_w = 0.0;
    double half_h = 0.0;
    for (int i = 0; i < 8; ++i) {
        const Vec3 corner{(i & 1) ? box.max.x : box.min.x,
                          (i & 2) ? box.max.y : box.min.y,
                          (i & 4) ? box.max.z : box.min.z};
        const Vec3 offset = corner - target_;
        half_w = std::max(half_w, std::abs(dot(offset, b.ax)));
        half_h = std::max(half_h, std::abs(dot(offset, b.ay)));
    }

    const double aspect = static_cast<double>(width_px_) / static_cast<double>(height_px_);
    // Height needed to fit each axis; the wider requirement wins.
    const double needed = std::max(half_h * 2.0, (half_w * 2.0) / aspect);

    // A single point, or a box edge-on to the view, has no extent to frame --
    // centring on it and keeping the current zoom is the useful answer.
    if (needed > kEps) set_view_height(needed * kExtentsMargin);
}

void Viewport::orbit_pixels(double dx, double dy) {
    // Signs chosen so the surface under the cursor follows the cursor, matching
    // pan: drag right and the near face swings right, bringing the model's left
    // side to front, which means the eye moves left -- azimuth decreases. Drag
    // down and the near face swings down, tipping the top into view, so the eye
    // rises and elevation increases.
    azimuth_ = wrap_angle(azimuth_ - dx * kOrbitRadiansPerPixel);
    set_elevation(elevation_ + dy * kOrbitRadiansPerPixel);
}

}  // namespace ncad
