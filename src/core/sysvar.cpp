// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/sysvar.hpp"

#include "ncad/undo.hpp"

#include <cstddef>

namespace ncad {
namespace {

// R12 defaults. The ranges are R12's own limits, and rejecting rather than
// clamping is deliberate: (setvar "PICKBOX" 900) should say so, not quietly do
// something else.
//
// PICKBOX and APERTURE are half-heights in pixels -- the box R12 draws is twice
// the value on a side -- and both live in the configuration rather than the
// drawing. OSMODE is drawing state and is written to the DXF header as $OSMODE.
// OSMODE 37 = END|CEN|INT. R12 ships with 0 -- no running snap at all -- but
// that is a decision made when a status line and an OSNAP command existed to
// change it, and neither does here yet.
//
// Three modes, not more, because every extra running snap is another thing
// competing under the cursor. MID competes with END all along a line for no
// gain. NEA matches everywhere. QUA sits on the rim of every circle and beats
// CEN there on distance, which is backwards for how circles are actually used
// -- CEN, NEA and TAN are wanted more often, and QUA is better reached as a
// one-shot override.
constexpr SysvarDef kTable[] = {
    {"OSMODE", Sysvar::OsMode, SysvarType::Int, false, true, 37, 0, 2047, 0.0, "", {}},
    {"PICKBOX", Sysvar::PickBox, SysvarType::Int, false, false, 3, 0, 50, 0.0, "", {}},
    {"APERTURE", Sysvar::Aperture, SysvarType::Int, false, false, 10, 1, 50, 0.0, "", {}},
    // Not journalled, like PICKBOX and APERTURE beside it: a display preference
    // is not drawing state, and undoing back through one would be surprising.
    {"CURSORSIZE", Sysvar::CursorSize, SysvarType::Int, false, false, 5, 1, 100, 0.0, "", {}},
    // Journalled, unlike the three above: R12 saves ORTHOMODE in the drawing,
    // so it is drawing state rather than a display preference.
    {"ORTHOMODE", Sysvar::OrthoMode, SysvarType::Int, false, true, 0, 0, 1, 0.0, "", {}},

    // Current layer, by name rather than by id: that is what R12 stores, what
    // getvar returns, and what survives a table whose indices have shifted.
    {"CLAYER", Sysvar::CLayer, SysvarType::String, false, true, 0, 0, 0, 0.0, "0", {}},
    // 256 is BYLAYER, 0 is BYBLOCK -- the same encoding DXF group 62 uses, so
    // no translation is needed anywhere.
    {"CECOLOR", Sysvar::CEColor, SysvarType::Int, false, true, 256, 0, 256, 0.0, "", {}},
    {"CELTYPE", Sysvar::CELtype, SysvarType::String, false, true, 0, 0, 0, 0.0, "BYLAYER", {}},
    {"LTSCALE", Sysvar::LtScale, SysvarType::Real, false, true, 0, 0, 0, 1.0, "", {}},

    {"LIMMIN", Sysvar::LimMin, SysvarType::Point, false, true, 0, 0, 0, 0.0, "", {0.0, 0.0, 0.0}},
    {"LIMMAX", Sysvar::LimMax, SysvarType::Point, false, true, 0, 0, 0, 0.0, "",
     {12.0, 9.0, 0.0}},
    {"LIMCHECK", Sysvar::LimCheck, SysvarType::Int, false, true, 0, 0, 1, 0.0, "", {}},
    {"INSBASE", Sysvar::InsBase, SysvarType::Point, false, true, 0, 0, 0, 0.0, "",
     {0.0, 0.0, 0.0}},

    // The current UCS. Read-only to the user, as R12 has them: the UCS command
    // owns them and writes through Sysvars::set_owned.
    {"UCSORG", Sysvar::UcsOrg, SysvarType::Point, true, true, 0, 0, 0, 0.0, "",
     {0.0, 0.0, 0.0}},
    {"UCSXDIR", Sysvar::UcsXDir, SysvarType::Point, true, true, 0, 0, 0, 0.0, "",
     {1.0, 0.0, 0.0}},
    {"UCSYDIR", Sysvar::UcsYDir, SysvarType::Point, true, true, 0, 0, 0, 0.0, "",
     {0.0, 1.0, 0.0}},
    {"UCSNAME", Sysvar::UcsName, SysvarType::String, true, true, 0, 0, 0, 0.0, "", {}},
    {"WORLDUCS", Sysvar::WorldUcs, SysvarType::Int, true, true, 1, 0, 1, 0.0, "", {}},

    // These two the user does set.
    {"UCSFOLLOW", Sysvar::UcsFollow, SysvarType::Int, false, true, 0, 0, 1, 0.0, "", {}},
    // 0 off, 1 on, 2 on and at the origin -- R12 packs both answers into one.
    {"UCSICON", Sysvar::UcsIcon, SysvarType::Int, false, true, 1, 0, 2, 0.0, "", {}},
    {"DWGNAME", Sysvar::DwgName, SysvarType::String, true, true, 0, 0, 0, 0.0, "", {}},
    {"DWGPREFIX", Sysvar::DwgPrefix, SysvarType::String, true, true, 0, 0, 0, 0.0, "", {}},
    // Writable, unlike the two above, and NOT saved in the drawing: it says how
    // to WRITE a file, not anything about the drawing's contents. Making it a
    // drawing variable means OPEN resets it, so choosing R2000 and then opening
    // something silently reverts to R12 -- which is exactly the surprise it
    // caused before this comment existed.
    {"DXFVERSION", Sysvar::DxfVersionVar, SysvarType::String, false, false, 0, 0, 0, 0.0, "R12", {}},
};

static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<std::size_t>(Sysvar::kCount),
              "sysvar table and the Sysvar enum have drifted apart");

char upper(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

// Not strcasecmp: that is locale-sensitive, and a Turkish locale would stop
// "osmode" matching "OSMODE".
bool iequals(std::string_view a, const char* b) {
    std::size_t i = 0;
    for (; i < a.size(); ++i) {
        if (b[i] == '\0' || upper(a[i]) != upper(b[i])) return false;
    }
    return b[i] == '\0';
}

std::size_t index_of(Sysvar id) {
    return static_cast<std::size_t>(id);
}

const std::string& empty_string() {
    static const std::string s;
    return s;
}

}  // namespace

SysvarValue SysvarValue::of_int(std::int32_t v) {
    SysvarValue s;
    s.type = SysvarType::Int;
    s.integer = v;
    return s;
}

SysvarValue SysvarValue::of_real(double v) {
    SysvarValue s;
    s.type = SysvarType::Real;
    s.real = v;
    return s;
}

SysvarValue SysvarValue::of_string(std::string v) {
    SysvarValue s;
    s.type = SysvarType::String;
    s.text = std::move(v);
    return s;
}

SysvarValue SysvarValue::of_point(const Vec3& p) {
    SysvarValue s;
    s.type = SysvarType::Point;
    s.point = p;
    return s;
}

const SysvarDef* sysvar_table(std::size_t& count) {
    count = sizeof(kTable) / sizeof(kTable[0]);
    return kTable;
}

const SysvarDef* find_sysvar(std::string_view name) {
    for (const SysvarDef& d : kTable) {
        if (iequals(name, d.name)) return &d;
    }
    return nullptr;
}

const SysvarDef& sysvar_def(Sysvar id) {
    return kTable[index_of(id)];
}

namespace {

SysvarValue default_value(const SysvarDef& d) {
    switch (d.type) {
        case SysvarType::Int: return SysvarValue::of_int(d.int_default);
        case SysvarType::Real: return SysvarValue::of_real(d.real_default);
        case SysvarType::String: return SysvarValue::of_string(d.string_default);
        case SysvarType::Point: return SysvarValue::of_point(d.point_default);
    }
    return SysvarValue::of_int(0);
}

}  // namespace

Sysvars::Sysvars() {
    for (const SysvarDef& d : kTable) values_[index_of(d.id)] = default_value(d);
}

std::int32_t Sysvars::get_int(Sysvar id) const {
    const SysvarValue& v = values_[index_of(id)];
    return v.type == SysvarType::Int ? v.integer : 0;
}

double Sysvars::get_real(Sysvar id) const {
    const SysvarValue& v = values_[index_of(id)];
    return v.type == SysvarType::Real ? v.real : 0.0;
}

const std::string& Sysvars::get_string(Sysvar id) const {
    const SysvarValue& v = values_[index_of(id)];
    return v.type == SysvarType::String ? v.text : empty_string();
}

Vec3 Sysvars::get_point(Sysvar id) const {
    const SysvarValue& v = values_[index_of(id)];
    return v.type == SysvarType::Point ? v.point : Vec3{};
}

Sysvars::SetStatus Sysvars::set_int(Sysvar id, std::int32_t v) {
    const SysvarDef& d = sysvar_def(id);
    if (d.read_only) return SetStatus::ReadOnly;
    if (d.type != SysvarType::Int) return SetStatus::WrongType;
    if (v < d.int_min || v > d.int_max) return SetStatus::OutOfRange;
    const SysvarValue before = values_[index_of(id)];
    values_[index_of(id)] = SysvarValue::of_int(v);
    journal_write(id, before);
    return SetStatus::Ok;
}

Sysvars::SetStatus Sysvars::set_real(Sysvar id, double v) {
    const SysvarDef& d = sysvar_def(id);
    if (d.read_only) return SetStatus::ReadOnly;
    if (d.type != SysvarType::Real) return SetStatus::WrongType;
    const SysvarValue before = values_[index_of(id)];
    values_[index_of(id)] = SysvarValue::of_real(v);
    journal_write(id, before);
    return SetStatus::Ok;
}

Sysvars::SetStatus Sysvars::set_string(Sysvar id, std::string v) {
    const SysvarDef& d = sysvar_def(id);
    if (d.read_only) return SetStatus::ReadOnly;
    if (d.type != SysvarType::String) return SetStatus::WrongType;
    const SysvarValue before = values_[index_of(id)];
    values_[index_of(id)] = SysvarValue::of_string(std::move(v));
    journal_write(id, before);
    return SetStatus::Ok;
}

Sysvars::SetStatus Sysvars::set_point(Sysvar id, const Vec3& p) {
    const SysvarDef& d = sysvar_def(id);
    if (d.read_only) return SetStatus::ReadOnly;
    if (d.type != SysvarType::Point) return SetStatus::WrongType;
    const SysvarValue before = values_[index_of(id)];
    values_[index_of(id)] = SysvarValue::of_point(p);
    journal_write(id, before);
    return SetStatus::Ok;
}

Sysvars::SetStatus Sysvars::set_owned(Sysvar id, const SysvarValue& v) {
    const SysvarDef& d = sysvar_def(id);
    if (d.type != v.type) return SetStatus::WrongType;

    const SysvarValue before = values_[index_of(id)];
    values_[index_of(id)] = v;
    journal_write(id, before);
    return SetStatus::Ok;
}

Sysvars::SetStatus Sysvars::set_metadata(Sysvar id, const SysvarValue& v) {
    const SysvarDef& d = sysvar_def(id);
    if (d.type != v.type) return SetStatus::WrongType;

    // No journal_write: see the header for why these specifically are outside
    // undo rather than escaping it.
    values_[index_of(id)] = v;
    return SetStatus::Ok;
}

bool Sysvars::get(std::string_view name, SysvarValue& out) const {
    const SysvarDef* d = find_sysvar(name);
    if (d == nullptr) return false;
    out = values_[index_of(d->id)];
    return true;
}

Sysvars::SetStatus Sysvars::set(std::string_view name, const SysvarValue& v) {
    const SysvarDef* d = find_sysvar(name);
    if (d == nullptr) return SetStatus::Unknown;

    switch (d->type) {
        case SysvarType::Int:
            // No Real -> Int coercion. Truncating a value the caller wrote out
            // in full is worse than telling them the variable is an integer.
            if (v.type != SysvarType::Int) return SetStatus::WrongType;
            return set_int(d->id, v.integer);

        case SysvarType::Real:
            // Int -> Real is safe and R12 accepts it: (setvar "LTSCALE" 2).
            if (v.type == SysvarType::Int) return set_real(d->id, static_cast<double>(v.integer));
            if (v.type != SysvarType::Real) return SetStatus::WrongType;
            return set_real(d->id, v.real);

        case SysvarType::String:
            if (v.type != SysvarType::String) return SetStatus::WrongType;
            return set_string(d->id, v.text);

        case SysvarType::Point:
            if (v.type != SysvarType::Point) return SetStatus::WrongType;
            return set_point(d->id, v.point);
    }
    return SetStatus::WrongType;
}

void Sysvars::journal_write(Sysvar id, const SysvarValue& before) {
    if (journal_) journal_->record_sysvar(id, before, values_[index_of(id)]);
}

void Sysvars::reset_drawing_vars() {
    for (const SysvarDef& d : kTable) {
        if (d.save_in_drawing) values_[index_of(d.id)] = default_value(d);
    }
}

const char* sysvar_set_status_message(Sysvars::SetStatus s) {
    switch (s) {
        case Sysvars::SetStatus::Ok: return "ok";
        case Sysvars::SetStatus::Unknown: return "no such system variable";
        case Sysvars::SetStatus::ReadOnly: return "system variable is read-only";
        case Sysvars::SetStatus::WrongType: return "wrong type for this system variable";
        case Sysvars::SetStatus::OutOfRange: return "value out of range";
    }
    return "unknown error";
}

}  // namespace ncad
