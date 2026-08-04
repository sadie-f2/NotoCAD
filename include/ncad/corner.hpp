// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Corners: what FILLET and CHAMFER put where two lines meet.
//
// They are one construction with two endings. Both find where the carriers
// cross, both decide from the picks which side of that crossing survives, and
// both cut each line short of it -- FILLET by the tangent length of an arc,
// CHAMFER by a distance given outright. Only what fills the gap differs, so the
// deciding is written once here and the commands add the arc or the line.
//
// R12 makes the same equivalence explicit: FILLET with radius 0 is not a
// special case but the limit, and it is how you close a corner that overshoots
// or falls short. That falls out of this construction for free -- a zero radius
// puts both cuts at the crossing and leaves nothing to fill.
//
// LINES ONLY, for now. R12 fillets arcs and circles too, and the construction
// generalises -- offset both curves by the radius, intersect the offsets, and
// the crossing is the arc's centre. What it needs is a tangent-point solver per
// curve pair, which is the same shape of work as the intersection kernel itself
// and deserves its own pass rather than being wedged in beside the line case.
#pragma once

#include "ncad/entity.hpp"
#include "ncad/vec3.hpp"

namespace ncad {

class Line;

// What to do to the two lines, and what to put between them.
struct CornerFit {
    // Where each line is cut. The surviving piece runs from `keep` to `cut`.
    Vec3 cut_a{};
    Vec3 cut_b{};

    // The endpoint of each line that the pick kept. A line whose crossing lies
    // beyond its end is EXTENDED to reach the corner rather than refused --
    // which is what makes FILLET the tool for closing a gap as well as for
    // rounding a meeting.
    Vec3 keep_a{};
    Vec3 keep_b{};

    // The fillet arc, in the plane the corner was solved in. False for CHAMFER
    // and for a zero-radius fillet, where the two cuts coincide and there is
    // nothing to fill.
    bool has_arc{false};
    Vec3 centre{};
    double radius{0.0};
    double start_angle{0.0};  // radians in the plane's basis, sweeping CCW
    double end_angle{0.0};
};

// The corner an arc of `radius` makes between two lines, or false when there
// is none: parallel carriers, a pick that lands on the crossing itself, or a
// radius too large for the lines to accommodate.
bool fillet_lines(const Line& a, const Vec3& pick_a, const Line& b, const Vec3& pick_b,
                  double radius, const Vec3& plane, CornerFit* out);

// The same, bevelled: `dist_a` back along the first line from the crossing and
// `dist_b` along the second. Zero distances give the same closed corner a
// zero-radius fillet does.
bool chamfer_lines(const Line& a, const Vec3& pick_a, const Line& b, const Vec3& pick_b,
                   double dist_a, double dist_b, const Vec3& plane, CornerFit* out);

}  // namespace ncad
