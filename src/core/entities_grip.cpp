// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// grips() and stretch() for the concrete entities.
//
// Kept out of entities.cpp for the same reason draw and DXF are: these are one
// concern across three classes, and reading them together is how you check that
// the grip indices and the stretch cases agree.
//
// The contract every implementation keeps: grips() and stretch() must agree on
// indices, stretch() ignores indices it does not recognise, and stretching
// every Stretch grip of an entity at once is the same as translating it.
#include "noto/entities.hpp"

#include "noto/ecs.hpp"

#include <cmath>

namespace noto {
namespace {

// True if `indices` names `want`. Linear, over a handful of entries -- an
// entity has three or five grips, and even a polyline is walked once per
// stretch rather than once per vertex.
bool names(const GripIndex* indices, std::size_t count, GripIndex want) {
    for (std::size_t i = 0; i < count; ++i) {
        if (indices[i] == want) return true;
    }
    return false;
}

// The component of `v` lying in the plane with the given normal. A grip is
// dragged in screen space and the delta will rarely be coplanar with the
// entity; anything out of plane would turn a circle into something a CIRCLE
// entity cannot represent, so it is dropped.
Vec3 in_plane(const Vec3& v, const Vec3& normal) {
    return v - normal * dot(v, normal);
}

}  // namespace

// --- Line -------------------------------------------------------------------

// 0 = start, 1 = end, 2 = midpoint. R12 puts a grip at the midpoint precisely
// so there is somewhere to pick the line up bodily rather than reshape it.
void Line::grips(std::vector<Grip>& out) const {
    out.push_back(Grip{start_, GripKind::Stretch, 0});
    out.push_back(Grip{end_, GripKind::Stretch, 1});
    out.push_back(Grip{midpoint(), GripKind::Move, 2});
}

void Line::stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) {
    // The midpoint grip moves the whole line, and so does naming both
    // endpoints -- which is what a crossing window enclosing the line does.
    // Those arriving at the same answer is the property that makes STRETCH
    // degenerate into MOVE rather than into nonsense.
    if (names(indices, count, 2)) {
        start_ = start_ + delta;
        end_ = end_ + delta;
        return;
    }
    if (names(indices, count, 0)) start_ = start_ + delta;
    if (names(indices, count, 1)) end_ = end_ + delta;
}

// --- Circle -----------------------------------------------------------------

// 0 = centre, 1..4 = quadrants. A circle has no stretchable geometry: it can
// only move or change size, which is why nothing here is GripKind::Stretch and
// why STRETCH treats a circle as an all-or-nothing move.
void Circle::grips(std::vector<Grip>& out) const {
    out.push_back(Grip{center_, GripKind::Move, 0});

    Vec3 q[4];
    quadrants(q);
    for (GripIndex i = 0; i < 4; ++i) {
        out.push_back(Grip{q[i], GripKind::Radius, i + 1});
    }
}

void Circle::stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) {
    if (names(indices, count, 0)) {
        center_ = center_ + delta;
        return;  // moving the centre moves the quadrants with it
    }

    Vec3 q[4];
    quadrants(q);
    for (GripIndex i = 0; i < 4; ++i) {
        if (!names(indices, count, i + 1)) continue;
        // The new radius is the distance from the centre to wherever the
        // quadrant was dragged, measured in the circle's own plane.
        const double r = length(in_plane(q[i] + delta - center_, props().normal));
        if (r > 0.0) radius_ = r;
        return;  // one quadrant decides it; a second would only fight the first
    }
}

void Ellipse::grips(std::vector<Grip>& out) const {
    out.push_back(Grip{center_, GripKind::Move, 0});

    // Radius rather than Stretch, exactly as Circle does it, and for the reason
    // recorded in SF_todo: naming every STRETCH grip has to equal translating
    // the entity, and a grip that resizes cannot honour that.
    Vec3 q[4];
    axis_points(q);
    for (GripIndex i = 0; i < 4; ++i) out.push_back(Grip{q[i], GripKind::Radius, i + 1});
}

void Ellipse::stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) {
    if (names(indices, count, 0)) {
        center_ = center_ + delta;
        return;  // the axes move with it
    }

    Vec3 q[4];
    axis_points(q);
    for (GripIndex i = 0; i < 4; ++i) {
        if (!names(indices, count, i + 1)) continue;

        const Vec3 moved = in_plane(q[i] + delta - center_, props().normal);
        if (is_zero(moved)) return;

        if (i == 0 || i == 2) {
            // A major-axis grip re-aims the axis as well as resizing it, which
            // is what makes dragging one rotate the ellipse rather than only
            // stretch it. The ratio is held so the shape is preserved.
            major_ = (i == 0) ? moved : moved * -1.0;
        } else {
            // A minor-axis grip only changes the ratio: the major axis is what
            // defines the orientation, and letting the minor one re-aim it too
            // would let the two fight.
            const double a = length(major_);
            if (a > 0.0) ratio_ = length(moved) / a;
        }
        return;  // one grip decides it
    }
}

// --- Arc --------------------------------------------------------------------

// 0 = start point, 1 = end point, 2 = midpoint of the sweep, 3 = centre.
//
// The endpoints are the stretchable ones. The midpoint and centre both move the
// whole arc: R12 uses the midpoint as the body grip, and the centre is included
// because an arc's centre is often the useful thing to grab.
void Arc::grips(std::vector<Grip>& out) const {
    out.push_back(Grip{start_point(), GripKind::Stretch, 0});
    out.push_back(Grip{end_point(), GripKind::Stretch, 1});
    out.push_back(Grip{midpoint(), GripKind::Move, 2});
    out.push_back(Grip{center_, GripKind::Move, 3});
}

void Arc::stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) {
    const bool body = names(indices, count, 2) || names(indices, count, 3);
    const bool start = names(indices, count, 0);
    const bool end = names(indices, count, 1);

    // Whole-arc move, either by a body grip or by a window that caught both
    // ends. Same rule as Line, and the same reason.
    if (body || (start && end)) {
        center_ = center_ + delta;
        return;
    }
    if (!start && !end) return;

    // An endpoint alone: it slides along the arc's own circle to the angle
    // nearest where it was dragged. Centre and radius are preserved, so the arc
    // stays an arc and the other endpoint does not move.
    //
    // This is a simplification. R12's exact rule for stretching an arc endpoint
    // is not verified here -- see SF_todo.md -- and this behaviour was chosen
    // for being predictable rather than for being proven faithful.
    const Basis b = arbitrary_axis(props().normal);
    const Vec3 from = start ? start_point() : end_point();
    const Vec3 v = in_plane(from + delta - center_, props().normal);
    if (is_zero(v)) return;  // dragged onto the centre: no angle to take

    const double angle = std::atan2(dot(v, b.ay), dot(v, b.ax));
    if (start) {
        start_angle_ = angle;
    } else {
        end_angle_ = angle;
    }
}

}  // namespace noto
