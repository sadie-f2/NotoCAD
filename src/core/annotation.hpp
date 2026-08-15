// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The line work annotation entities have in common.
//
// DIMENSION and LEADER both generate their own geometry, and both begin it with
// an arrowhead. HANDOFF named "a second thing that generates arrowheads" as the
// cost of giving LEADER its own entity rather than folding it into DimKind --
// this is that cost paid once. Two copies is how the two would come to disagree
// about barb spread, and a drawing carrying both would show it.
//
// Internal to src/core: these are shapes, not database concepts, and nothing
// outside the entity implementations has any business making one.
#pragma once

#include "ncad/entity.hpp"
#include "ncad/vec3.hpp"

namespace ncad {

// How far the arrow barbs open, in radians. The same spread MEASUREGEOM's ghost
// uses, so the preview and the committed annotation look like each other.
inline constexpr double kBarbSpread = 0.30;

// A filled arrowhead, as a triangular SOLID, with its point at `tip` and its
// barbs opening around `back` in the plane `normal` names.
//
// SOLID rather than three lines because that is what R12 puts in a dimension
// block, and it is what makes AutoCAD draw a filled head. Our own viewport
// draws it as an open triangle, since the renderer has no fill primitive and a
// SOLID has always drawn as its outline here -- a known and consistent
// difference rather than one these entities invent.
EntityPtr make_arrowhead(const Vec3& tip, const Vec3& back, const Vec3& normal, double size,
                         const EntityProps& props);

// A LINE carrying `props`. Both annotations build their runs out of these.
EntityPtr make_segment(const Vec3& a, const Vec3& b, const EntityProps& props);

}  // namespace ncad
