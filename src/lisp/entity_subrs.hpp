// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Internal: the entity-access builtins, declared here so the table in
// builtins.cpp can reference them while their implementation stays in its own
// translation unit. Not an installed header.
#pragma once

#include "noto/lisp/eval.hpp"

namespace noto::lisp {

bool subr_entmake(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_entget(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_entmod(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_entdel(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_entlast(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_entnext(Interp& in, const Value* args, std::size_t argc, Value& out);

}  // namespace noto::lisp
