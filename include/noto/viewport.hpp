// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The camera: world <-> screen for a parallel-projected wireframe view.
//
// Parallel projection only. R12's default view is parallel; DVIEW's perspective
// mode is a separate concern and not what the viewer needs to exist.
//
// The view direction is stored as azimuth/elevation rather than as a raw vector.
// A vector needs a fallback rule for the up axis when it points straight down
// the world Z axis, and any such rule snaps discontinuously mid-orbit -- exactly
// where smoothness matters. Angles make the pole an ordinary value: the basis is
// written directly in terms of azimuth, so plan view keeps whatever heading the
// orbit arrived with, and straight down is an exact, ordinary setting rather
// than a special case. That exactness matters: plan view is the 2D drafting
// view, and a tenth of a degree off vertical would leave axis-aligned geometry
// subtly askew. Elevation is clamped to the poles, so orbit stops there instead
// of tumbling over the top.
//
// No Qt here. Screen coordinates are pixels with the origin top-left and y
// downward, which is Qt's convention, but the type is ours and headless tests
// exercise every path.
#pragma once

#include "noto/bbox.hpp"
#include "noto/ecs.hpp"  // Basis
#include "noto/mat4.hpp"
#include "noto/render.hpp"
#include "noto/vec3.hpp"

namespace noto {

// Pixels. Origin top-left, y increases downward.
struct ScreenPoint {
    double x{}, y{};
};

class Viewport {
public:
    // Straight down. Reachable exactly -- plan view is this value.
    static constexpr double kMaxElevation = 1.5707963267948966;  // pi/2

    Viewport() = default;

    // --- projection setup ---------------------------------------------------

    void set_size(int width_px, int height_px);
    int width() const { return width_px_; }
    int height() const { return height_px_; }

    void set_target(const Vec3& t) { target_ = t; }
    const Vec3& target() const { return target_; }

    // World units spanned by the viewport's vertical extent. Width follows from
    // the pixel aspect ratio.
    void set_view_height(double h);
    double view_height() const { return view_height_; }

    // R12 VPOINT: the direction from the target toward the eye. Need not be
    // unit length. A direction straight up or down leaves the azimuth -- the
    // heading you are looking along -- unchanged, since it is unrecoverable.
    void set_view_direction(const Vec3& d);
    Vec3 view_direction() const;

    void set_azimuth(double radians) { azimuth_ = radians; }
    double azimuth() const { return azimuth_; }
    void set_elevation(double radians);
    double elevation() const { return elevation_; }

    // Standard R12 views.
    void set_plan_view();

    // --- derived frame ------------------------------------------------------

    // ax = screen right, ay = screen up, az = toward the eye.
    Basis basis() const;

    Mat4 world_to_view() const;

    // World units per pixel. The single scale factor of the projection.
    double world_per_pixel() const;

    // Half a pixel of sag: flattening finer than that is invisible.
    DrawContext draw_context() const;

    // --- projection ---------------------------------------------------------

    ScreenPoint project(const Vec3& world) const;

    // Screen point back to the world, landing on the plane through
    // `plane_point` with normal `plane_normal`. Returns false when that plane
    // is edge-on to the view, where the answer is a line rather than a point.
    bool unproject(const ScreenPoint& sp, const Vec3& plane_point, const Vec3& plane_normal,
                   Vec3* out) const;

    // Onto the plane through the target perpendicular to the view -- the
    // sensible default when there is no construction plane in play.
    Vec3 unproject_to_target_plane(const ScreenPoint& sp) const;

    // --- navigation ---------------------------------------------------------

    // Drags the drawing by a pixel delta, as a mouse would.
    void pan_pixels(double dx, double dy);

    // `factor` > 1 magnifies. The world point under `focus` stays put, which is
    // what makes wheel-zoom track the cursor.
    void zoom(double factor, const ScreenPoint& focus);
    void zoom(double factor);

    // Frames the box with a small margin. An empty box is left alone; a box
    // with no extent in the view plane (a single point) keeps the current
    // height and just centres.
    void zoom_extents(const BBox& box);

    // Turntable orbit: horizontal drag swings the azimuth about the world Z
    // axis, vertical drag changes elevation. Constrained rather than free
    // trackball, because CAD work has a floor and a free orbit loses it.
    void orbit_pixels(double dx, double dy);

private:
    int width_px_{1};
    int height_px_{1};
    Vec3 target_{};
    double view_height_{100.0};
    // Plan view: looking straight down, world X to the right, world Y up.
    double azimuth_{-1.5707963267948966};  // -pi/2
    double elevation_{kMaxElevation};
};

}  // namespace noto
