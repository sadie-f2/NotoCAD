// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The two unnamed numbers, named.
//
// `src/core` carried forty-three bare `1e-9`s and thirty-three bare `1e-12`s.
// They are not forty-three and thirty-three decisions; they are two decisions
// written out seventy-six times, and until they had names nobody could change
// either one, or even ask what it was for, without reading every site.
//
// **NEITHER VALUE MOVES HERE.** Sadie's ruling, 2026-09-08: naming and moving
// are separate acts, and doing both at once means a corpus that shifts for
// reasons nobody can separate. This commit is provably byte-identical.
//
// WHY NCAD'S GATES STAY ABSOLUTE WHERE THE ENGINE'S BECAME RELATIVE. The
// engine's bands are fractions of the solid's own diagonal, which it has in
// hand. ncad's live in the geometry kernel -- `intersect.hpp` includes only
// `vec3.hpp` and has no `Database` in reach, and is right not to: an
// intersection is a question about two curves, not about the drawing they
// happen to sit in. Making these relative means threading a tolerance through
// the whole intersection API, which is a different change with a different
// justification.
//
// **Sadie's reopening condition, in her words: "I suspect absolute
// intersections will come back to bite us eventually."** They hold at drawing
// scales of roughly 1 to 1000 units. The drawing that proves otherwise reopens
// this, with the API change in scope.
#pragma once

namespace ncad {

// Two quantities count as THE SAME quantity.
//
// A GATE: it decides membership -- whether two curves meet, whether a point is
// on a span, whether an arc closes. `kJoinTol`, `kIntersectTol` and `kSpanEps`
// are this number under three names, kept because each reads better at its own
// call site than a single name would.
//
// It is not dimensionally one thing: it gates lengths, angles and ratios alike.
// That is a real compromise rather than an oversight, and it is what the
// reopening condition above is about.
inline constexpr double kSameEps = 1e-9;

// Below this, a quantity is ZERO as far as floating point is concerned.
//
// A FLOOR, not a gate, and the distinction is the whole of `EPSILON.md` §1: a
// floor does not scale with the model because floating point does not. Every
// site is the same question -- is this degenerate? -- asked of an arc's
// included angle, a chord's length, a triangle's area, a normal's magnitude.
//
// Deliberately three orders below `kSameEps`, so that a quantity this program
// is willing to call zero is far below one it is willing to call equal.
inline constexpr double kFloatFloor = 1e-12;

}  // namespace ncad
