// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The drawing's unit, and the one place text becomes a length.
//
// **Why this exists at all, when R12 has no units.** A drafting program does
// not need them: a circle of radius 5 is 5 of whatever the drafter has in
// their head, and no dimension, layer or plot cares. But this program also
// makes THINGS -- `STLOUT` writes for a slicer, `STEPOUT` for a kernel, and
// `THREAD` resolves a designation that names an absolute physical size. `M8`
// IS 8 mm; `1/4-20` IS a quarter inch. Those are the first values in the
// program that mean something without a drafter to interpret them, and they
// are what forced this.
//
// So the split is deliberate and narrow: **the drawing has ONE unit, and the
// prompt accepts any.** Storage is single-unit, so no geometry code ever sees
// two and nothing downstream has to ask. Input is polyglot -- `1/4"`,
// `6.35mm`, `0.25in` -- because designers hold mixed inch and millimetre in
// their heads all day, and the 25.4 they are holding is exactly where an inch
// part goes 25.4x wrong. That conversion happens in EXACTLY ONE place, which
// is `parse_length` below.
//
// `INSUNITS` uses AutoCAD's own codes so a DXF interoperates rather than
// carrying a private convention, and it defaults to 0 -- unitless -- because
// that is what every drawing that already exists is, and saying nothing must
// keep meaning nothing.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ncad {

// AutoCAD's $INSUNITS codes, restricted to the ones this program can mean.
// 3 (miles) is deliberately absent: it is in AutoCAD's table and is not a
// thing anyone models a part in, and accepting a code we would then have to
// carry through STEP is worse than refusing it.
inline constexpr std::int32_t kInsUnitless = 0;
inline constexpr std::int32_t kInsInches = 1;
inline constexpr std::int32_t kInsFeet = 2;
inline constexpr std::int32_t kInsMillimetres = 4;
inline constexpr std::int32_t kInsCentimetres = 5;
inline constexpr std::int32_t kInsMetres = 6;

// True for a code this program accepts. Not a range: 3 sits inside the span
// and is not one of them.
bool insunits_valid(std::int32_t code);

// "millimetres", "inches"... and "unitless" for 0. Used in refusals, so it
// reads as a word rather than a number the user has to look up.
const char* insunits_name(std::int32_t code);

// The codes this program accepts, spelled for a refusal message:
// "0 unitless, 1 inches, 2 feet, 4 millimetres, 5 centimetres, 6 metres".
std::string insunits_choices();

// Millimetres in one unit of `code`. Zero for unitless, which is the caller's
// signal that no conversion is possible -- never a silent 1.0, because
// treating an unspecified drawing as millimetres is the exact assumption that
// sends an inch part out 25.4x small.
double mm_per_insunit(std::int32_t code);

// **The one conversion.** Reads a length written in any unit the program
// knows and returns it in the drawing's own unit.
//
// Accepts: a bare number (the workspace unit, unchanged, exactly as before
// this existed); a number with a trailing unit (`mm`, `cm`, `m`, `in`, `"`,
// `ft`, `'`), optionally spaced; and the fraction forms `1/4` and `1-1/4`
// that inch work is written in.
//
// A suffix on a UNITLESS drawing is REFUSED, naming `INSUNITS` -- there is no
// answer to "a quarter inch of what", and guessing is the failure this whole
// file exists to prevent. A bare number on a unitless drawing is fine and
// means what it always meant.
//
// Angles never come here. A bearing has no unit to carry and `30<45` must not
// start accepting `30<45mm`.
bool parse_length(std::string_view text, std::int32_t insunits, double& out,
                  std::string& error);

}  // namespace ncad
