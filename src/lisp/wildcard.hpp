// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// AutoCAD's wildcard patterns, as `wcmatch` and as ssget's filters use them.
//
// Not the shell's globbing and not a regular expression -- a third dialect with
// its own metacharacters, and the differences are the kind that look like bugs:
//
//   *  any sequence, including none, ANYWHERE in the pattern
//   ?  any single character
//   #  any single DIGIT
//   @  any single ALPHABETIC character
//   .  any single NON-alphanumeric character   (not "any character")
//   [] any one of the enclosed, with a-z ranges
//   [~] any one NOT enclosed
//   ~  at the START OF THE WHOLE PATTERN, negates the entire match
//   ,  separates alternatives: "LINE,ARC" matches either
//   `  escapes the next character
//
// The two that catch people are `.` meaning non-alphanumeric rather than any
// character, and `~` being an anchor at position zero rather than an operator
// that can appear anywhere.
#pragma once

#include <string_view>

namespace ncad::lisp {

// True when `text` matches `pattern`.
//
// `fold_case` exists because AutoCAD applies the two differently and both
// behaviours are wanted: standalone `wcmatch` compares case-sensitively, while
// an ssget filter matching a layer name does not -- (8 . "walls") finds layer
// WALLS. One implementation with a flag rather than two that drift.
bool wildcard_match(std::string_view text, std::string_view pattern, bool fold_case);

}  // namespace ncad::lisp
