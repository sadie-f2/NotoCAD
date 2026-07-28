// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The derived object snaps: NEA, PER, TAN, INT.
//
// The other half of osnap.hpp. Static snaps (END/MID/CEN/QUA) depend only on
// the entity and live on the vtable; these depend on a reference point or a
// second entity, so they cannot, and are free functions over the kernel instead.
//
// This is the prerequisite for phase 4 of the shell. Osnap cursor tracking is
// mostly geometry rather than Qt work, and none of it existed -- so it is here,
// headless and tested, before anything tries to draw a marker with it.
//
// Everything works in world space and in three dimensions. The reference point
// is where the cursor unprojected to; the viewport supplies it.
#pragma once

#include "noto/entity.hpp"
#include "noto/vec3.hpp"

namespace noto {

// Two is enough for every pair in the current entity set: line/line gives one,
// line/circle and circle/circle give two, and arcs only ever narrow that.
inline constexpr int kMaxIntersections = 2;
inline constexpr int kMaxTangents = 2;

// NEAREST. The point on the entity closest to `ref`. Always succeeds for a
// non-degenerate entity, which is why NEA is the fallback snap in R12.
bool nearest_point(const Entity& e, const Vec3& ref, Vec3* out);

// PERPENDICULAR. The foot of the perpendicular from `ref`.
//
// Not clamped to the entity for a LINE: R12 snaps to the perpendicular point on
// the line's extension, which is what makes PER useful for construction. For a
// circle or arc the foot is radial, and the nearer of the two candidates wins.
bool perpendicular_point(const Entity& e, const Vec3& ref, Vec3* out);

// TANGENT. Tangent points from `ref` to a circular entity, written into `out`.
// Returns how many there are: two from outside, one when `ref` is on the
// circle, none from inside. `ref` is projected into the entity's plane first --
// a cursor is rarely exactly coplanar with the thing it is pointing at.
//
// Zero for a LINE, which has no tangent construction.
int tangent_points(const Entity& e, const Vec3& ref, Vec3 out[kMaxTangents]);

// INTERSECTION. True geometric intersections of two entities, not apparent
// (view-projected) ones -- R12 has APPINT for those, and it needs a viewport,
// which the kernel does not have.
//
// Arcs are filtered to their sweep and lines to their segment, so this answers
// "where do these two entities actually meet", not where their host circles or
// infinite lines would.
//
// Delegates to intersect.hpp, which is the general facility; this is the shape
// osnap wants -- points only, bounded only, and at most two, because a cursor
// cannot usefully be offered the twelve places two polylines cross. Duplicates
// are collapsed, so a polyline meeting a line at a shared vertex offers that
// point once rather than once per adjoining segment.
//
// Formerly limited to coplanar circles. It no longer is: two circles in space
// meet where each crosses the other's plane at the other's radius, and the
// kernel solves that, so a circle in the XY plane and one in YZ correctly meet
// at two points rather than being declined.
int intersect_entities(const Entity& a, const Entity& b, Vec3 out[kMaxIntersections]);

}  // namespace noto
