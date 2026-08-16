// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// CP1252 and UTF-8, in both directions.
//
// AutoCAD's two lock files disagree about their encoding: `.dwl` is CP1252 and
// `.dwl2` is UTF-8. The reader has needed CP1252 -> UTF-8 since the formats were
// measured; a WRITER needs the inverse, and the inverse is where the interesting
// case lives -- a machine named "Sadie's MacBook Pro" has to come back out as
// the single byte 0x92 or the file is not what AutoCAD wrote.
//
// Internal to src/core. Nothing outside the lock code has an encoding problem,
// and a general-purpose text conversion module is not what this is: it knows one
// legacy codepage because one file format demands it.
#pragma once

#include <string>

namespace ncad::text {

// Whether the bytes are already valid UTF-8.
//
// The encoding is SNIFFED rather than taken from the extension, so a writer that
// differs from AutoCAD does not silently produce mojibake.
bool is_utf8(const std::string& s);

// Whatever the bytes are, as UTF-8 -- which is what both front ends display.
// Already-UTF-8 input is returned unchanged.
std::string to_utf8(const std::string& s);

// UTF-8 back to CP1252, for the `.dwl` file only.
//
// A code point with no CP1252 spelling becomes '?'. Input that is not valid
// UTF-8 is passed through unchanged: it is either already CP1252 or something
// this has no business mangling.
std::string from_utf8(const std::string& utf8);

}  // namespace ncad::text
