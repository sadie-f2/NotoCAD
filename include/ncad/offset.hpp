// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Offsetting: the parallel copy of a curve at a distance.
//
// The third consumer of phase 10's kernel, after the cutting primitives. It
// needs the same two answers intersect.hpp gives -- where do these carriers
// meet, and where on each -- because offsetting a POLYLINE is not offsetting
// its segments: each segment moves sideways on its own, and the corners are
// then wherever the moved segments MEET. Without that intersection the corners
// come apart, which is the whole difference between a parallel outline and a
// pile of disconnected pieces.
//
// R12's entity set for OFFSET is LINE, CIRCLE, ARC and 2D POLYLINE, and that is
// what this covers. ELLIPSE and SPLINE decline honestly rather than being
// approximated: the true offset of either is a higher-order curve that neither
// entity can hold, so producing one would mean silently substituting a shape
// the drawing would then claim was exact.
//
// SELF-INTERSECTION IS NOT REMOVED. Offsetting a concave corner inward by more
// than its local clearance produces a loop, exactly as R12 does. Detecting and
// pruning those loops is a different algorithm -- a full polygon offset -- and
// pretending to it here would fail silently on the cases it did not catch.
#pragma once

#include "ncad/entity.hpp"
#include "ncad/vec3.hpp"

namespace ncad {

// The parallel copy of `e` at `distance`, on the side `side` falls on.
//
// `distance` is positive; which way it goes is decided by the side point, as
// R12's "Side to offset?" decides it. That is also what makes the Through
// option nothing but a different way to arrive here: the caller measures its
// own distance to the picked point and passes it in.
//
// `plane` is the construction plane, used only where the entity does not carry
// one of its own. A LINE in space has no unique perpendicular, so the current
// UCS supplies it; a CIRCLE, ARC or POLYLINE offsets in its own plane, because
// that is the only plane in which the result is still that kind of entity.
//
// Properties -- layer, colour, linetype, extrusion -- carry over, which is R12's
// behaviour: an offset lands beside its source, not on the current layer.
//
// Null when there is no offset to make: an entity with no curve, a circle or
// arc offset inward past its own centre, or a polyline whose every segment
// collapses. The caller reports; the kernel does not guess a fallback.
EntityPtr offset_curve(const Entity& e, double distance, const Vec3& side, const Vec3& plane);

}  // namespace ncad
