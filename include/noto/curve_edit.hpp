// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes
//
// Cutting curves at parameters.
//
// The other half of what phase 10 rests on. intersect.hpp answers "where do
// these meet, and where on each"; this answers "given those parameters, what is
// left". BREAK is the first consumer and TRIM is the second, which is why this
// is a pair of free functions over the kernel rather than anything BREAK owns.
//
// Everything here is expressed in the normalised parameters intersect.hpp
// defines: [0, 1] spans the entity as drawn, whatever it is. That is the whole
// reason the parameterisation was made uniform -- a single implementation cuts
// a LINE, an ARC, a CIRCLE and a bulged POLYLINE without a switch per command.
#pragma once

#include "noto/entity.hpp"

#include <cstddef>
#include <vector>

namespace noto {

// The piece of `e` between parameters `ta` and `tb`, as a new entity.
//
// For an open curve `ta` must be below `tb`. For a closed one -- a CIRCLE, or a
// POLYLINE with its closed flag -- `ta` above `tb` means the span that wraps
// through the start, which is how the surviving piece of a broken circle is
// asked for.
//
// The result is always an OPEN entity: breaking a circle yields an arc, and
// breaking a closed polyline yields an open one. That is R12's behaviour and it
// is not a simplification -- a closed curve with a piece missing is not closed.
//
// Null when the span is degenerate, or when the entity has no curve. Properties
// (layer, colour, linetype, extrusion) carry over from `e`.
EntityPtr extract_curve_span(const Entity& e, double ta, double tb);

// What is left of `e` when the span between `t0` and `t1` is removed. Appends
// to `out` and returns how many pieces were added.
//
// An open curve gives up to two pieces, and fewer when a break point reaches an
// end: breaking a line beyond its endpoint shortens it rather than leaving a
// zero-length stub. A closed curve gives exactly one, because removing an arc
// from a loop leaves a loop with a gap, which is one open curve.
//
// The two parameters may arrive in either order. For an open curve they are
// sorted, since "between" is symmetric; for a closed one the ORDER IS THE
// ANSWER -- R12 breaks a circle counterclockwise from the first point to the
// second, so swapping them keeps the other piece. That asymmetry is deliberate
// and is what makes picking the two points on a circle predictable.
//
// Equal parameters split the curve in two without removing anything, which is
// R12's `@` answer at the second prompt. A closed curve cannot be split that
// way -- there is nothing to open it at -- and yields nothing.
std::size_t break_curve(const Entity& e, double t0, double t1, std::vector<EntityPtr>& out);

}  // namespace noto
