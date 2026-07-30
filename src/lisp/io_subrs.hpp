// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// AutoLISP file I/O.
//
// Named in `CLAUDE.md`'s own scope statement -- "plus solid file I/O (`open`,
// `read-line`)" -- and it is the path the whole project exists for: pulling
// external analysis results back in and generating geometry from them. Without
// it a script can build a drawing but cannot be told what to build.
//
// A descriptor is `Type::File`, an index into the interpreter's open-file
// table. The type, the printed form and the equality rule were all in
// `value.hpp` already, waiting.
//
// PATHS GET THE SAME TREATMENT AS THE FILE COMMANDS: `~` expanded, so a script
// and the SAVE prompt agree about what `~/drawings/x.dxf` means. Nothing else
// is done to them -- no extension is invented, because a script naming a file
// means the file it named.
#pragma once

#include "noto/lisp/eval.hpp"

namespace noto::lisp {

// (open "path" "r"|"w"|"a") -> file descriptor, or nil if it could not be
// opened. A missing file is a condition a script tests for, not an error.
bool subr_open(Interp& in, const Value* args, std::size_t n, Value& out);

// (close f) -> nil. Closing twice is not an error; the second is a no-op.
bool subr_close(Interp& in, const Value* args, std::size_t n, Value& out);

// (read-line f) -> string, or nil at end of file. The line separator is not
// part of the result, and a CRLF file reads the same as a LF one -- a script
// should not have to know which machine wrote its input.
bool subr_read_line(Interp& in, const Value* args, std::size_t n, Value& out);

// (write-line "text" f) -> the string, with a newline added to the file but not
// to the returned value. R12 returns what it was given.
bool subr_write_line(Interp& in, const Value* args, std::size_t n, Value& out);

// (read-char f) -> the next byte as an integer, or nil at end of file.
bool subr_read_char(Interp& in, const Value* args, std::size_t n, Value& out);

// (write-char n f) -> n.
bool subr_write_char(Interp& in, const Value* args, std::size_t n, Value& out);

// (findfile "name") -> the full path if it exists, otherwise nil. The companion
// to open: a script asks this before deciding whether to read.
bool subr_findfile(Interp& in, const Value* args, std::size_t n, Value& out);

}  // namespace noto::lisp
