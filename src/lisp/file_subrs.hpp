// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Internal: drawing and file I/O builtins, declared here so the table in
// builtins.cpp can reference them. Not an installed header.
#pragma once

#include "ncad/lisp/eval.hpp"

namespace ncad::lisp {

bool subr_dxfout(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_quit(Interp& in, const Value* args, std::size_t argc, Value& out);

}  // namespace ncad::lisp
