// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// DXF R12 serialisation for the concrete entities.
//
// Kept apart from entities.cpp so the geometry stays free of format concerns.
// This is also where the world-space internal representation is converted back
// into the entity coordinate system that R12 actually stores.
#include "noto/dxf.hpp"

#include "noto/render.hpp"

#include <algorithm>
#include "noto/ecs.hpp"
#include "noto/entities.hpp"

#include <numbers>

namespace noto {
namespace {

constexpr double kRadToDeg = 180.0 / std::numbers::pi;

}  // namespace

// LINE is the exception: R12 stores both endpoints in WORLD coordinates, not in
// the entity coordinate system, so no conversion happens here.
void Line::dxf_write(DxfWriter& w) const {
    w.write_common(*this);
    w.point(10, start_);
    w.point(11, end_);
    w.write_extrusion(props().normal);
}

void Circle::dxf_write(DxfWriter& w) const {
    w.write_common(*this);
    // Group 10 is in the entity coordinate system; its z is the elevation.
    w.point(10, world_to_ecs(props().normal).transform_point(center_));
    w.code(40, radius_);
    w.write_extrusion(props().normal);
}

void Arc::dxf_write(DxfWriter& w) const {
    w.write_common(*this);
    w.point(10, world_to_ecs(props().normal).transform_point(center_));
    w.code(40, radius_);
    // Angles are already measured in the arbitrary-axis basis, which is the same
    // basis DXF uses, so this is a units conversion and nothing more.
    w.code(50, start_angle_ * kRadToDeg);
    w.code(51, end_angle_ * kRadToDeg);
    w.write_extrusion(props().normal);
}

// POLYLINE is three records, not one: the header, a VERTEX for each point, and
// a SEQEND to close the run. That is R12's structure, and it is synthesised
// here rather than being how the entity is stored -- see SF_todo.md for why a
// 20,000-face mesh cannot afford one database entity per vertex.
//
// All three records carry the same handle. R12 gives sub-entities their own,
// but nothing in this program refers to a vertex by handle, and inventing
// handles at write time that no entity owns would put identifiers in the file
// that cannot be resolved on the way back in.
void Ellipse::dxf_write(DxfWriter& w) const {
    // R2000 can name an ELLIPSE, so it gets the curve itself: centre, the major
    // axis as a VECTOR from it, the ratio and the parameter range. Fifteen
    // numbers instead of a tessellation, and a round trip that loses nothing.
    if (dxf_has_modern_entities(w.version())) {
        if (major_length() <= kEps) return;
        w.write_common(*this);
        w.subclass("AcDbEllipse");
        w.point(10, center_);
        // Group 11 is a vector, not a point: it rotates without translating.
        w.point(11, major_);
        w.write_extrusion(props().normal);
        w.code(40, ratio_);
        w.code(41, start_param_);
        w.code(42, end_param_);
        return;
    }

    // THE DIVERGENCE, PAID FOR HERE. AC1009 has no ELLIPSE entity, so this
    // writes what R12 itself wrote: a polyline approximation. The database
    // keeps the exact curve; the file gets something every R12 reader
    // understands. CLAUDE.md's rule is that a divergence must degrade honestly
    // on the way out, and this is the whole of what that costs.
    //
    // Consequence, and it is worth stating rather than discovering: a round
    // trip through DXF is LOSSY. An ellipse written and read back is a
    // polyline, and nothing can recover what it was. That is the same bargain
    // R12 users made, and the alternative -- emitting a later DXF version --
    // would break the interchange guarantee for the whole file.
    const double a = major_length();
    if (a <= kEps) return;

    // Fixed relative tolerance rather than one from a view: a file has no zoom.
    // A thousandth of the major axis is finer than any plotter or screen will
    // resolve and keeps the vertex count sane.
    const double span = sweep();
    int segments = arc_segment_count(a, span, a * 1.0e-3);
    segments = std::clamp(segments, 8, 512);

    const Mat4 to_ecs = world_to_ecs(props().normal);
    const bool closed = is_full();

    w.write_common_as(*this, "POLYLINE");
    w.code(66, 1);
    Vec3 elevation{0.0, 0.0, 0.0};
    elevation.z = to_ecs.transform_point(point_at(start_param_)).z;
    w.point(10, elevation);
    if (closed) w.code(70, 1);
    w.write_extrusion(props().normal);

    // One fewer vertex when closed: the last would land on the first, and the
    // closed flag already joins them.
    const int count = closed ? segments : segments + 1;
    for (int i = 0; i < count; ++i) {
        const double t = start_param_ + span * (static_cast<double>(i) / segments);
        w.code(0, "VERTEX");
        w.code(8, w.layer_name(*this));
        w.point(10, to_ecs.transform_point(point_at(t)));
    }

    w.code(0, "SEQEND");
    w.code(8, w.layer_name(*this));
}

void Polyline::dxf_write(DxfWriter& w) const {
    const Mat4 to_ecs = world_to_ecs(props().normal);

    w.write_common(*this);
    // Group 66 announces that vertices follow. R12 requires it, and a reader
    // that trusts it will stop looking at the first entity if it is missing.
    w.code(66, 1);
    // The header's group 10 is unused for a polyline and R12 writes it as zero
    // with the elevation in z.
    Vec3 elevation{0.0, 0.0, 0.0};
    if (!vertices_.empty()) elevation.z = to_ecs.transform_point(vertices_.front().pos).z;
    w.point(10, elevation);
    if (closed_) w.code(70, 1);
    w.write_extrusion(props().normal);

    for (const PolyVertex& v : vertices_) {
        w.code(0, "VERTEX");
        w.code(8, w.layer_name(*this));
        w.point(10, to_ecs.transform_point(v.pos));
        // Widths are written per vertex rather than as the header's defaults,
        // because a taper is a property of the segment and the header can only
        // say one thing for the whole polyline. Omitted when zero, which is
        // both the common case and R12's own default.
        if (v.start_width != 0.0) w.code(40, v.start_width);
        if (v.end_width != 0.0) w.code(41, v.end_width);
        if (v.bulge != 0.0) w.code(42, v.bulge);
    }

    w.code(0, "SEQEND");
    w.code(8, w.layer_name(*this));
}

}  // namespace noto
