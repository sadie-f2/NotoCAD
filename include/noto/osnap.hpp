// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Object snap points.
//
// Split into two families, because they have genuinely different signatures:
//
//   Static snaps  (END, MID, CEN, QUA, NOD, INS) depend only on the entity, and
//                 are produced by Entity::osnap_points().
//   Derived snaps (PER, TAN, NEA, INT) depend on a reference point or a second
//                 entity, and are computed by the geometry kernel on demand.
//
// Only the static family exists so far.
#pragma once

#include "noto/vec3.hpp"

#include <cstdint>

namespace noto {

enum class OsnapType : std::uint8_t {
    Endpoint,
    Midpoint,
    Center,
    Quadrant,
    Node,
    Insert,
    Perpendicular,
    Tangent,
    Nearest,
    Intersection,
};

struct OsnapPoint {
    Vec3 pos;
    OsnapType type{OsnapType::Endpoint};
};

const char* osnap_name(OsnapType t);

}  // namespace noto
