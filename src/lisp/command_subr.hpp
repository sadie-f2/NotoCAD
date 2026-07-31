// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Internal: the (command ...) builtin. Not an installed header.
#pragma once

#include "ncad/lisp/eval.hpp"

namespace ncad::lisp {

bool subr_command(Interp& in, const Value* args, std::size_t argc, Value& out);

}  // namespace ncad::lisp
