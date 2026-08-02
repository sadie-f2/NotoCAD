// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Internal: drawing and file I/O builtins, declared here so the table in
// builtins.cpp can reference them. Not an installed header.
#pragma once

#include "ncad/lisp/eval.hpp"

#include <string>

namespace ncad::lisp {

bool subr_dxfout(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_quit(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_load(Interp& in, const Value* args, std::size_t argc, Value& out);

// Shared by (load ...), APPLOAD (via InterpScriptLoader) and `ncad`'s own
// startup file loading -- one path reading and evaluating a .lsp file, so the
// three ways of getting a script into the interpreter cannot drift apart from
// each other.
struct LoadResult {
    bool opened{false};  // false: the file itself could not be read
    bool ok{false};      // true: every form evaluated without error (implies opened)
    std::string message;  // set whenever !ok
};
LoadResult load_lisp_file(Interp& in, const std::string& path);

}  // namespace ncad::lisp
