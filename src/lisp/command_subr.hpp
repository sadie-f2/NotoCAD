// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Internal: the (command ...) builtin. Not an installed header.
#pragma once

#include "noto/lisp/eval.hpp"

namespace noto::lisp {

bool subr_command(Interp& in, const Value* args, std::size_t argc, Value& out);

}  // namespace noto::lisp
