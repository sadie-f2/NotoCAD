// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Converting evaluated AutoLISP values into command input.
//
// Shared deliberately. Two things feed LISP values to a command -- the
// (command ...) function and the `!variable` escape at a command prompt -- and
// if they disagreed about what a value means, the same expression would build
// different geometry depending on how it was typed.
#pragma once

#include "ncad/command.hpp"
#include "ncad/lisp/value.hpp"

#include <string>

namespace ncad::lisp {

// Interprets `v` as the answer to `prompt`. Conversion is prompt-directed: an
// integer is an entity handle at a selection prompt and a number elsewhere, and
// a string is matched against the prompt's keywords before anything else.
//
// nil and "" both mean Enter, which is how a (command ...) call ends a loop.
bool value_to_input(const Prompt& prompt, const Value& v, InputValue& out, std::string& error);

}  // namespace ncad::lisp
