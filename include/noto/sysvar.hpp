// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// System variables: R12's SETVAR/GETVAR table.
//
// R12 keeps its settings in named, typed variables rather than in a settings
// object -- OSMODE is the running object snap, PICKBOX the selection aperture,
// LTSCALE the linetype scale -- and AutoLISP reaches all of them through two
// functions. Anything that wants to be settable ends up here, so the table is
// built to grow: adding a variable is one row in sysvar_table().
//
// Two access paths, deliberately. The typed one (Sysvar enum -> get_int) is a
// direct array index and is what the kernel and the viewport use, because it
// runs on every mouse move. The name-driven one is for getvar/setvar and does a
// linear scan, which is right: getvar is not a hot path, and a static table
// keeps the definitions constexpr.
#pragma once

#include "noto/vec3.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace noto {

enum class SysvarType : std::uint8_t { Int, Real, String, Point };

// Tagged struct with every field present, mirroring InputValue rather than
// reaching for std::variant. It is the shape already used at this boundary, and
// like InputValue it holds a std::string -- the trivially-copyable rule applies
// to geometry types, which this is not.
struct SysvarValue {
    SysvarType type{SysvarType::Int};
    std::int32_t integer{0};
    double real{0.0};
    Vec3 point{};
    std::string text;

    static SysvarValue of_int(std::int32_t v);
    static SysvarValue of_real(double v);
    static SysvarValue of_string(std::string s);
    static SysvarValue of_point(const Vec3& p);
};

// The typed handle. Keep in step with sysvar_table(); kCount sizes the storage.
enum class Sysvar : std::uint16_t {
    OsMode,
    PickBox,
    Aperture,
    kCount,
};

struct SysvarDef {
    const char* name;
    Sysvar id;
    SysvarType type;
    bool read_only;

    // R12 splits its variables between the drawing (written to the DXF HEADER
    // section, restored on OPEN) and the configuration file, which follows the
    // installation rather than the drawing. NEW/OPEN resets the first kind only.
    bool save_in_drawing;

    std::int32_t int_default;
    std::int32_t int_min;  // inclusive; ignored unless type is Int
    std::int32_t int_max;
    double real_default;
    const char* string_default;
    Vec3 point_default;
};

const SysvarDef* sysvar_table(std::size_t& count);

// Case-insensitive, as R12 is. Null when there is no such variable.
const SysvarDef* find_sysvar(std::string_view name);

const SysvarDef& sysvar_def(Sysvar id);

class Sysvars {
public:
    // Every variable starts at the default in its table row.
    Sysvars();

    // Typed access. The type must match the definition; a mismatch returns zero
    // rather than throwing, because these run in the render and input paths
    // where exceptions are not control flow.
    std::int32_t get_int(Sysvar id) const;
    double get_real(Sysvar id) const;
    const std::string& get_string(Sysvar id) const;
    Vec3 get_point(Sysvar id) const;

    enum class SetStatus : std::uint8_t { Ok, Unknown, ReadOnly, WrongType, OutOfRange };

    SetStatus set_int(Sysvar id, std::int32_t v);
    SetStatus set_real(Sysvar id, double v);
    SetStatus set_string(Sysvar id, std::string v);
    SetStatus set_point(Sysvar id, const Vec3& p);

    // The name-driven path, for getvar and setvar. get() is false for an unknown
    // name, which is how getvar knows to return nil.
    bool get(std::string_view name, SysvarValue& out) const;
    SetStatus set(std::string_view name, const SysvarValue& v);

    // NEW and OPEN: restores the variables a drawing owns, leaving the
    // configuration ones alone.
    void reset_drawing_vars();

private:
    SysvarValue values_[static_cast<std::size_t>(Sysvar::kCount)];
};

// For error text. Never null.
const char* sysvar_set_status_message(Sysvars::SetStatus s);

}  // namespace noto
