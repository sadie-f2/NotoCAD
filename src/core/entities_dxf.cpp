// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// DXF R12 serialisation for the concrete entities.
//
// Kept apart from entities.cpp so the geometry stays free of format concerns.
// This is also where the world-space internal representation is converted back
// into the entity coordinate system that R12 actually stores.
#include "noto/dxf.hpp"
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

}  // namespace noto
