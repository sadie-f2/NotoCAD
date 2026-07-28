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

// TRIM's question: which stretch of `e` does the pick at parameter `at` fall in,
// given that the curve is cut at every parameter in `cuts`?
//
// That stretch is what TRIM removes. On an OPEN curve the ends count as cuts, so
// picking beyond the outermost intersection trims the dangling tail -- which is
// what makes TRIM useful for cleaning up overshoots and is most of what it is
// used for. On a CLOSED curve there are no ends, so two cuts are the minimum and
// the stretch is found by walking round; one cut leaves nothing to trim to and
// is refused.
//
// `cuts` is taken by value and sorted internally. Duplicates -- two cutting
// edges meeting the curve at the same place -- collapse, because a stretch of
// zero length is not something to remove.
//
// False when there is no stretch to remove: no cuts at all, or a closed curve
// with fewer than two.
bool trim_span(const Entity& e, std::vector<double> cuts, double at, double* lo, double* hi);

// `e` lengthened so its curve reaches parameter `t`, which lies outside [0, 1].
//
// EXTEND's half of the job. Which end grows follows from the sign: below zero
// extends the start, above one extends the end. A LINE grows along itself, an
// ARC grows its sweep, and a POLYLINE grows its terminal segment -- including
// the bulge, when that segment is an arc.
//
// Null for a curve that cannot be extended: a CIRCLE or a closed POLYLINE has
// no end to grow, and R12 refuses those too.
EntityPtr extend_curve(const Entity& e, double t);

}  // namespace noto
