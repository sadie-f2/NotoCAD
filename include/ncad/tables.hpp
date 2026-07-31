// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The symbol tables: layers, linetypes, and named coordinate systems.
//
// Split out of database.hpp so that undo.hpp can name them without the two
// headers including each other. The journal has to record table changes for the
// same reason it records system variables -- anything mutable that is not
// journalled becomes a hole in undo.
//
// The UCS is here rather than in ecs.hpp on purpose, because the two are
// different things that DXF is careful to keep apart and this codebase should
// too. An ECS is per-entity: an extrusion vector on a CIRCLE saying which plane
// that one circle lives in. A UCS is global and momentary: the frame typed
// coordinates are read in while it is current. They meet at exactly one instant
// -- when a command creates geometry -- and never again, which is why no entity
// stores a UCS and why adding UCS changes no entity record.
#pragma once

#include "ncad/entity.hpp"

#include <string>
#include <vector>

namespace ncad {

inline constexpr LayerId kInvalidLayer = 0xFFFF;
inline constexpr LinetypeId kInvalidLinetype = 0xFFFF;

struct Layer {
    std::string name;
    // Negative means the layer is off, per R12. The magnitude is still the
    // colour, so turning a layer off and on again does not forget it.
    std::int16_t color{7};
    LinetypeId linetype{kLinetypeContinuous};
    bool frozen{false};
    bool locked{false};

    bool off() const { return color < 0; }
    std::int16_t visible_color() const { return color < 0 ? -color : color; }
};

struct Linetype {
    std::string name;
    std::string description;
    // Positive is a dash, negative a gap, zero a dot. Empty means continuous.
    std::vector<double> pattern;

    // Total length of one repeat, before LTSCALE is applied.
    double pattern_length() const;
};

using UcsId = std::uint16_t;
inline constexpr UcsId kInvalidUcs = 0xFFFF;

// A user coordinate system: an origin and two axes, always expressed in WORLD
// coordinates.
//
// In world terms and not relative to whatever UCS preceded it, which is what
// DXF stores and what keeps this simple -- every definition resolves to world
// as it is made, so there is never a chain of frames to walk and no way for one
// to drift from another.
struct Ucs {
    Vec3 origin{0.0, 0.0, 0.0};
    Vec3 xdir{1.0, 0.0, 0.0};
    Vec3 ydir{0.0, 1.0, 0.0};

    // The construction plane's normal, which is what most of the kernel
    // actually wants from a UCS.
    Vec3 zdir() const;

    // True when this is the world system, within tolerance. DXF's $WORLDUCS.
    bool is_world() const;

    // UCS coordinates into world, and back. A point typed at a prompt is in the
    // first direction; a point reported by DIST or ID is in the second.
    Mat4 to_world() const;
    Mat4 from_world() const;

    // Axes forced orthonormal, right-handed, and non-degenerate.
    //
    // Applied on the way out of the database rather than trusted on the way in:
    // the current UCS is carried in system variables, which AutoLISP can write,
    // and a UCS whose axes are not perpendicular would put geometry somewhere
    // no transform could undo.
    Ucs normalized() const;
};

struct UcsDef {
    std::string name;
    Ucs ucs;
};

}  // namespace ncad
