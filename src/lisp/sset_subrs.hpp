// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Internal: the selection-set builtins. Not an installed header.
#pragma once

#include "noto/lisp/eval.hpp"

namespace noto::lisp {

bool subr_ssget(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_ssadd(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_ssdel(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_sslength(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_ssname(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_ssmemb(Interp& in, const Value* args, std::size_t argc, Value& out);

}  // namespace noto::lisp
