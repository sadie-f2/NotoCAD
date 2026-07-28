// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Curve intersection: where two entities meet, and where on each of them.
//
// This is phase 10's foundation. TRIM, EXTEND, FILLET, CHAMFER and BREAK are
// one question wearing five hats -- where does curve A meet curve B, and which
// side of that point am I on -- so the question is answered once, here, rather
// than five times with five sets of tolerances.
//
// It generalises `intersect_entities` in osnap_derived.hpp, which came first
// and answered the narrower question osnap INT asks: bounded only, points only,
// coplanar circles only. That function now delegates here, so there is one
// implementation of the arithmetic and not two that can drift.
//
// What is new, and what phase 10 needs that osnap did not:
//
//   *Parameters.* An intersection is not just a point. BREAK has to split a
//   curve there and TRIM has to know which piece to discard, and neither can be
//   done from a coordinate alone -- a circle crosses a line at two points and
//   "the one nearer the pick" is a question about parameters.
//
//   *Extension.* TRIM and EXTEND work against a cutting edge's carrier, not
//   only against the edge as drawn: EXTEND exists precisely to reach a boundary
//   that stops short. So a caller can ask for intersections of the unbounded
//   line and the whole circle, and is told which ones actually landed on the
//   entities.
//
//   *Non-coplanar circles.* Two circles in space meet where each crosses the
//   other's plane at the other's radius. osnap declined to answer because no
//   cursor can usefully track it; TRIM is given exact input and can.
//
// True 3-space throughout, never view-projected -- the same rule osnap INT
// follows, and for the same reason: two skew lines that merely cross on screen
// do not meet, and a kernel that says they do produces geometry that is wrong
// from every other angle.
#pragma once

#include "noto/entity.hpp"
#include "noto/vec3.hpp"

#include <cstdint>
#include <vector>

namespace noto {

// How close two curves must come to count as meeting, in drawing units.
//
// One number, shared, because the alternative is each command choosing its own
// and TRIM disagreeing with FILLET about whether a corner is closed.
inline constexpr double kIntersectTol = 1e-9;

enum class IntersectMode : std::uint8_t {
    // Only where the entities actually are. What osnap INT wants.
    Bounded,

    // Against the unbounded carrier of each entity: a line becomes its infinite
    // extension, an arc becomes its whole circle. What TRIM and EXTEND want,
    // because "extend this line to that arc" is a question about carriers.
    //
    // A polyline extends only at its TERMINAL segments, and only when it is
    // open. An interior segment's extension runs into its own neighbours and
    // means nothing, and a closed polyline has no free end -- but the two end
    // segments do have carriers, and growing one is precisely what EXTEND does
    // to a polyline.
    Extended,
};

// One point where two curves meet.
//
// The parameters are normalised so that [0, 1] spans the entity as drawn,
// whatever it is:
//
//   LINE      0 at the start point, 1 at the end.
//   CIRCLE    the angle in the entity's own plane over a full turn, from the
//             ECS X axis derived by arbitrary_axis(). Always within.
//   ARC       0 at the start angle, 1 at the end, measured along the sweep.
//   POLYLINE  segment i spans [i/n, (i+1)/n], so the whole runs 0 to 1 with
//             arc segments taking their share by index rather than by length.
//
// Values outside [0, 1] are reachable only in Extended mode and say how far
// beyond the entity the meeting is -- which is exactly what EXTEND needs to
// know to decide whether a boundary is reachable at all.
struct Intersection {
    Vec3 point{};

    double t0{0.0};  // parameter on the first entity
    double t1{0.0};  // parameter on the second

    // Whether the meeting lies on the entity as drawn, rather than on its
    // extension. Always true in Bounded mode, which is what makes that mode a
    // filter rather than a different calculation.
    bool within0{true};
    bool within1{true};
};

using IntersectionList = std::vector<Intersection>;

// Where two entities meet. Appends to `out` and returns how many were added.
//
// Returns 0 for a pair it has no geometry for -- TEXT, SOLID, 3DFACE, POINT and
// PROXY are all "no intersections" rather than errors, because a caller sweeping
// a selection set should skip them rather than stop.
std::size_t intersect(const Entity& a, const Entity& b, IntersectMode mode,
                      IntersectionList& out);

// The point at parameter `t` on an entity, using the parameterisation above.
// False for an entity with no curve, or for a t outside [0, 1] on a polyline,
// whose segments do not extend.
//
// BREAK and TRIM both need it: an intersection gives a parameter, and splitting
// the curve means evaluating the pieces either side of it.
bool curve_point_at(const Entity& e, double t, Vec3* out);

// The inverse: the parameter of the point on `e` nearest to `p`.
//
// BREAK is why it exists. A pick gives a coordinate, and every decision after
// that -- which piece to keep, which way round the two points are, whether the
// second point fell off the end -- is a comparison of parameters. Answering it
// by coordinate instead means re-deriving the same ordering at every step.
//
// The point need not be on the curve: it is projected first, which is what makes
// a pick a little off the line still break where the user meant. False for an
// entity with no curve.
bool curve_parameter_at(const Entity& e, const Vec3& p, double* out);

// Whether the entity's curve closes on itself -- a CIRCLE always, a POLYLINE
// when its closed flag is set. Breaking one leaves a single piece rather than
// two, which is the whole difference between the closed and open cases.
bool curve_is_closed(const Entity& e);

// --- the primitives ---------------------------------------------------------
//
// Exposed because FILLET and CHAMFER construct geometry that is not yet an
// entity -- a trial arc tangent to two curves -- and need to intersect it
// before deciding to keep it. Each appends to `out`.
//
// Parameters here are the raw ones: t along the line from p0 to p1, and the
// fraction of a full turn around the circle from its own ECS X axis. Sweep
// filtering for arcs is the caller's job, which is what `intersect` does.

void intersect_line_line(const Vec3& a0, const Vec3& a1, const Vec3& b0, const Vec3& b1,
                         IntersectionList& out);

void intersect_line_circle(const Vec3& a0, const Vec3& a1, const Vec3& centre, double radius,
                           const Vec3& normal, IntersectionList& out);

void intersect_circle_circle(const Vec3& c0, double r0, const Vec3& n0, const Vec3& c1, double r1,
                             const Vec3& n1, IntersectionList& out);

}  // namespace noto
