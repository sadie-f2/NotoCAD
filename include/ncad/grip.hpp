// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Grips: the defining points of an entity, and what dragging one does.
//
// This exists because transform(Mat4) is the wrong shape for two things R12
// does. Dragging a line's endpoint moves *that endpoint*, leaving the other
// alone; STRETCH moves the defining points that fall inside a crossing window
// and leaves the rest fixed. Neither is a whole-entity transform, and neither
// can be expressed by composing them.
//
// Grips and STRETCH are one mechanism, not two. Dragging an endpoint grip is a
// one-point stretch; STRETCH is the same operation driven by a window instead
// of the cursor. So there is one vtable pair -- grips() to ask what the points
// are, stretch() to move a named subset -- and both features sit on it.
//
// A grip is not just a coordinate. A circle's quadrant grip and its centre grip
// are both points on the same circle, but dragging the first changes the radius
// and dragging the second moves the whole entity. That is why this is not
// osnap_points(): the coordinates often coincide, and the meanings do not.
#pragma once

#include "ncad/vec3.hpp"

#include <cstdint>

namespace ncad {

enum class GripKind : std::uint8_t {
    // Moves this defining point alone. A line endpoint, a polyline vertex.
    Stretch,

    // Moves the whole entity. A circle's centre, a line's midpoint -- R12 puts
    // a grip there precisely so there is somewhere to grab the thing bodily.
    Move,

    // Resizes about the entity's centre. A circle's quadrants: the new radius
    // is the distance from the centre to where the grip was dragged.
    Radius,
};

// Indexes a defining point within one entity. Wide enough for a polyline with
// far more vertices than anyone will draw; a bitmask would not be, which is why
// stretch() takes a list rather than a mask.
using GripIndex = std::uint32_t;

struct Grip {
    Vec3 pos{};
    GripKind kind{GripKind::Stretch};

    // What to pass to stretch() to move this point. Stable for a given entity
    // kind, so a caller may hold onto it across a drag.
    GripIndex index{0};
};

const char* grip_kind_name(GripKind k);

}  // namespace ncad
