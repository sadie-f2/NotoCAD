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

class UndoJournal;

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

    // Crosshair length, as a percentage of the smaller viewport dimension.
    // AutoCAD's, not R12's -- R12 had a full-screen crosshair and no way to
    // shorten it -- and 100 reproduces exactly that, which is why the range
    // runs all the way there rather than stopping somewhere tidy.
    CursorSize,
    // Current entity properties. New geometry takes these, which is how R12
    // decides what layer and colour something is drawn on without asking.
    CLayer,
    CEColor,
    CELtype,
    LtScale,
    // Drawing limits, and whether points outside them are refused.
    LimMin,
    LimMax,
    LimCheck,
    // Where this drawing is grabbed by when it is inserted into another one.
    // Set by BASE, and read by nothing yet -- there is no command that inserts
    // a whole drawing. It is written to DXF regardless, because a drawing whose
    // base point is silently dropped inserts wrong in the program that opens it.
    InsBase,

    // The current user coordinate system, carried as DXF carries it: three
    // points in world terms, plus a name and a flag. Read-only to the user
    // because the UCS command owns them, exactly as R12 has it.
    UcsOrg,
    UcsXDir,
    UcsYDir,
    UcsName,
    WorldUcs,
    // Whether changing the UCS automatically switches to a plan view of it.
    UcsFollow,
    // Whether the UCS icon is shown, and whether at the origin. UCSICON sets it.
    UcsIcon,
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

    // Writes a variable the USER may not write.
    //
    // R12 marks UCSORG and its siblings read-only because a command owns them,
    // not because nothing may change them -- something has to, or the UCS
    // command could not do its job. This is that owner's door.
    //
    // It bypasses the read-only flag and nothing else. In particular it still
    // journals, because a write that escapes the journal is how undo grows
    // holes, and that reasoning does not care who is doing the writing.
    SetStatus set_owned(Sysvar id, const SysvarValue& v);

    // The name-driven path, for getvar and setvar. get() is false for an unknown
    // name, which is how getvar knows to return nil.
    bool get(std::string_view name, SysvarValue& out) const;
    SetStatus set(std::string_view name, const SysvarValue& v);

    // NEW and OPEN: restores the variables a drawing owns, leaving the
    // configuration ones alone.
    void reset_drawing_vars();

    // Every successful set is journalled through this. Wired by Database at
    // construction, so a sysvar cannot be changed without the change being
    // undoable -- routing writes past the journal is exactly how undo grows
    // holes.
    void set_journal(UndoJournal* j) { journal_ = j; }

private:
    void journal_write(Sysvar id, const SysvarValue& before);

    SysvarValue values_[static_cast<std::size_t>(Sysvar::kCount)];
    UndoJournal* journal_{nullptr};
};

// For error text. Never null.
const char* sysvar_set_status_message(Sysvars::SetStatus s);

}  // namespace noto
