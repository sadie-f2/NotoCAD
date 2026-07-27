// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The draw() half of the entity vtable: an abstract sink of wireframe geometry.
//
// Everything an entity emits is a WORLD-SPACE polyline. Curves are flattened in
// the kernel rather than handed to the backend as arcs, because under orbit a
// circle projects to an ellipse and an arc to an elliptical segment -- so a
// backend-native arc call only helps in plan view and needs the general path
// anyway. Flattening once here gives QPainter, a later QOpenGLWidget, and
// entity hit-testing one code path instead of three.
//
// Nothing in this header depends on Qt: the core stays headless and the tests
// render into a recording sink with no display.
#pragma once

#include "noto/vec3.hpp"

#include <cstddef>

namespace noto {

struct EntityProps;

// How finely to flatten curves. Carried into draw() rather than baked into the
// entity so that tessellation density tracks zoom -- a circle filling the
// screen and the same circle three pixels wide should not cost the same.
struct DrawContext {
    // Maximum sagitta, in world units, allowed between a chord and the true
    // curve. A viewport sets this from its scale; see Viewport::draw_context().
    double chord_tolerance{0.0};
};

class Renderer {
public:
    virtual ~Renderer() = default;

    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Called by the scene walker before each entity's draw(), so that draw()
    // itself stays pure geometry and carries no styling. Colour resolution
    // (BYLAYER/BYBLOCK) belongs to the backend, which is what knows the layer
    // table and the display palette.
    virtual void begin_entity(const EntityProps& props) = 0;

    // `pts` are world coordinates. `closed` joins the last point back to the
    // first without duplicating it in the array.
    virtual void polyline(const Vec3* pts, std::size_t count, bool closed) = 0;
};

// Segments needed to flatten a circular arc of the given radius and sweep
// (radians) within `chord_tolerance`. Always at least one segment per 45
// degrees, so a coarse tolerance still yields a recognisable circle rather
// than a triangle, and capped so a degenerate tolerance cannot allocate
// without bound.
inline constexpr int kMinArcSegments = 1;
inline constexpr int kMaxArcSegments = 4096;

int arc_segment_count(double radius, double sweep, double chord_tolerance);

}  // namespace noto
