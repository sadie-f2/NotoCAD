// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The symbol tables: layers and linetypes.
//
// Split out of database.hpp so that undo.hpp can name them without the two
// headers including each other. The journal has to record table changes for the
// same reason it records system variables -- anything mutable that is not
// journalled becomes a hole in undo.
#pragma once

#include "noto/entity.hpp"

#include <string>
#include <vector>

namespace noto {

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

}  // namespace noto
