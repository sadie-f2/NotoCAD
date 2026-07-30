// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The geometry helpers AutoLISP scripts are written in.
//
// `polar`, `distance` and `angle` appear in very nearly every drafting script
// ever written -- they are how a script says "a point 40 units at 30 degrees
// from there". Without them a script fails on its third line no matter what
// else is present, which is why these came before the larger gaps.
//
// COORDINATES ARE WORLD, NOT UCS. AutoCAD works these in the current UCS; this
// layer has no UCS anywhere in it -- `command` passes LISP points through
// unconverted and `entmake` converts only through the entity's own extrusion --
// so doing these in UCS would mean `(command "LINE" (polar p a d))` silently
// mixed two frames. When `trans` lands the relationship becomes expressible and
// this is worth revisiting. With the UCS at world, which is the default and the
// common case, the two agree exactly.
#pragma once

#include "noto/lisp/eval.hpp"

namespace noto::lisp {

// (polar pt angle distance) -> point, in the world XY plane. Z is carried
// through from `pt` rather than zeroed, so a script working at a height stays
// there.
bool subr_polar(Interp& in, const Value* args, std::size_t n, Value& out);

// (distance pt1 pt2) -> real. True 3D distance.
bool subr_distance(Interp& in, const Value* args, std::size_t n, Value& out);

// (angle pt1 pt2) -> real, radians in [0, 2*pi), measured in the world XY plane
// from the X axis, counterclockwise.
bool subr_angle(Interp& in, const Value* args, std::size_t n, Value& out);

// (inters pt1 pt2 pt3 pt4 [onseg]) -> point or nil.
//
// Omitted or non-nil `onseg` means the crossing must lie on both SEGMENTS;
// explicit nil treats them as infinite lines. That is R12's convention, and it
// is the one people get backwards, so it is spelled out here and tested.
bool subr_inters(Interp& in, const Value* args, std::size_t n, Value& out);

// (osnap pt "mid,end") -> point or nil. The nearest snap of the named kinds.
//
// DIVERGENCE, and a deliberate one: AutoCAD limits this to entities within the
// aperture, which is a number of screen pixels. A script has no screen and no
// zoom, so an aperture would mean either inventing one or refusing to work
// headlessly. This searches the whole drawing and returns the nearest match,
// which is both what a script means by the question and the only answer that
// does not depend on where someone happened to have scrolled to.
bool subr_osnap(Interp& in, const Value* args, std::size_t n, Value& out);

}  // namespace noto::lisp
