// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Internal: getvar / setvar, declared here so the table in builtins.cpp can
// reference them while their implementation stays in its own translation unit.
// Not an installed header.
#pragma once

#include "noto/lisp/eval.hpp"

namespace noto::lisp {

bool subr_getvar(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_setvar(Interp& in, const Value* args, std::size_t argc, Value& out);

}  // namespace noto::lisp
