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
// Both families exist: the static ones on the vtable, the derived ones in
// osnap_derived.hpp.
#pragma once

#include "noto/vec3.hpp"

#include <cstdint>
#include <string_view>

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

// The running-osnap set, as R12's OSMODE system variable stores it.
//
// These bit values are R12's, and they are NOT this enum's declaration order:
// Quadrant and Node are swapped, and Intersection sits mid-mask rather than
// last. `1 << int(type)` is therefore wrong, and osnap_bit() is a lookup rather
// than a shift. A drawing written with the wrong bits would silently enable the
// wrong snaps, so this is pinned by tests asserting the literal values.
using OsnapMask = std::uint16_t;

inline constexpr OsnapMask kOsnapNone = 0;
inline constexpr OsnapMask kOsnapEndpoint = 1;
inline constexpr OsnapMask kOsnapMidpoint = 2;
inline constexpr OsnapMask kOsnapCenter = 4;
inline constexpr OsnapMask kOsnapNode = 8;
inline constexpr OsnapMask kOsnapQuadrant = 16;
inline constexpr OsnapMask kOsnapIntersection = 32;
inline constexpr OsnapMask kOsnapInsert = 64;
inline constexpr OsnapMask kOsnapPerpendicular = 128;
inline constexpr OsnapMask kOsnapTangent = 256;
inline constexpr OsnapMask kOsnapNearest = 512;
inline constexpr OsnapMask kOsnapQuick = 1024;

// Every bit an OsnapType can occupy. Not 0xFFFF: OSMODE's remaining bits are
// either unused in R12 or belong to QUICK, which selects a search strategy
// rather than a snap type.
inline constexpr OsnapMask kOsnapAll =
    kOsnapEndpoint | kOsnapMidpoint | kOsnapCenter | kOsnapNode | kOsnapQuadrant |
    kOsnapIntersection | kOsnapInsert | kOsnapPerpendicular | kOsnapTangent | kOsnapNearest;

// Parses an osnap override as typed at a point prompt: "cen", "endp",
// "int,mid", "none". Case-insensitive; comma or space separated; each token may
// be the three-letter form, the full name, or any unambiguous prefix of either,
// which is how R12 takes them.
//
// "NON" / "NONE" parses to kOsnapNone with a true return -- an explicit request
// for no snap on this pick, which is a different thing from failing to parse.
// Callers that must tell those apart use the return value, not the mask.
bool parse_osnap_mask(std::string_view text, OsnapMask* out);

OsnapMask osnap_bit(OsnapType t);
bool osnap_enabled(OsnapMask mask, OsnapType t);

// The OsnapType for a single bit. False for kOsnapQuick, which names no type,
// and for any bit that is not one of the above.
bool osnap_type_from_bit(OsnapMask bit, OsnapType* out);

}  // namespace noto
