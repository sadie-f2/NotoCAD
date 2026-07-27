// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The R12-style command prompt.
//
// AutoCAD's command line is a command prompt that can evaluate LISP, not a LISP
// prompt that can call commands. The difference is what you type first: `LINE`
// starts a command, `(setq r 25.0)` evaluates, and at a prompt `!r` substitutes
// a LISP value as the answer.
//
// Everything here is a front end. The state machine, the prompts and the input
// conversion all already exist -- this decides which of them a typed line goes
// to, and nothing more.
#pragma once

#include "noto/command.hpp"
#include "noto/lisp/eval.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace noto::app {

// Splits a typed line into responses. Whitespace separates them -- at a command
// prompt, space acts as Enter, as R12 has it -- except that a parenthesised
// expression is kept whole however many spaces it contains, and a string prompt
// takes the rest of the line verbatim.
std::vector<std::string> split_prompt_line(const std::string& line, bool whole_line);

// True when `text` is an incomplete LISP form and the caller should read another
// line before doing anything with it.
bool needs_more_input(lisp::Context& ctx, const std::string& text);

// One command prompt session over std::cin. Returns the process exit code.
int run_command_prompt(lisp::Context& ctx, lisp::Interp& in, CommandEngine& engine,
                       bool interactive);

}  // namespace noto::app
