// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The draw() half of the entity vtable, kept apart from entities.cpp for the
// same reason entities_dxf.cpp is: geometry, serialisation and display are
// three independent reasons for a file to change.
//
// Every entity emits world-space polylines. Curves are flattened here rather
// than by the backend -- see render.hpp for why.

#include "noto/entities.hpp"

#include "noto/ecs.hpp"
#include "noto/render.hpp"

#include <cmath>
#include <numbers>
#include <vector>

namespace noto {
namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

// Flattens a circular arc in the plane of `normal` into `out`, appending
// `segments + 1` points from start_angle through start_angle + sweep. Callers
// wanting a closed loop drop the duplicated final point and pass closed=true.
void tessellate_arc(std::vector<Vec3>& out, const Vec3& center, double radius,
                    const Vec3& normal, double start_angle, double sweep, int segments) {
    const Basis b = arbitrary_axis(normal);
    const double step = sweep / static_cast<double>(segments);
    out.reserve(out.size() + static_cast<std::size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
        const double a = start_angle + step * static_cast<double>(i);
        out.push_back(center + (b.ax * std::cos(a) + b.ay * std::sin(a)) * radius);
    }
}

}  // namespace

void Line::draw(const DrawContext&, Renderer& r) const {
    const Vec3 pts[2] = {start_, end_};
    r.polyline(pts, 2, false);
}

void Circle::draw(const DrawContext& ctx, Renderer& r) const {
    if (radius_ <= kEps) return;

    const int segments = arc_segment_count(radius_, kTwoPi, ctx.chord_tolerance);

    std::vector<Vec3> pts;
    tessellate_arc(pts, center_, radius_, props().normal, 0.0, kTwoPi, segments);
    // The loop closes itself: the last point coincides with the first, so drop
    // it and let the renderer join the ends.
    pts.pop_back();
    r.polyline(pts.data(), pts.size(), true);
}

void Ellipse::draw(const DrawContext& ctx, Renderer& r) const {
    const double a = major_length();
    if (a <= kEps) return;

    // Segmented against the MAJOR axis, so the flattest part of the curve is
    // not the one that decides. Using the minor would under-segment the ends,
    // which is exactly where an ellipse turns most sharply.
    const double span = sweep();
    const int segments = arc_segment_count(a, span, ctx.chord_tolerance);

    std::vector<Vec3> pts;
    pts.reserve(static_cast<std::size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
        pts.push_back(point_at(start_param_ + span * (static_cast<double>(i) / segments)));
    }

    if (is_full()) {
        // The loop closes itself, so drop the repeated point and let the
        // renderer join the ends -- same as Circle.
        pts.pop_back();
        r.polyline(pts.data(), pts.size(), true);
        return;
    }
    r.polyline(pts.data(), pts.size(), false);
}

void Arc::draw(const DrawContext& ctx, Renderer& r) const {
    if (radius_ <= kEps) return;

    const double s = sweep();
    const int segments = arc_segment_count(radius_, s, ctx.chord_tolerance);

    std::vector<Vec3> pts;
    tessellate_arc(pts, center_, radius_, props().normal, start_angle_, s, segments);
    r.polyline(pts.data(), pts.size(), false);
}

}  // namespace noto
