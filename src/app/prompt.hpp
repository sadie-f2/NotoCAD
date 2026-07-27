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

// Where a session's output goes: std::cout for ncad, a text widget for the GUI.
// The mirror of InputSource -- the prompt semantics must not differ between
// front ends, so neither front end is allowed to own them.
class PromptOutput {
public:
    virtual ~PromptOutput() = default;

    PromptOutput() = default;
    PromptOutput(const PromptOutput&) = delete;
    PromptOutput& operator=(const PromptOutput&) = delete;

    virtual void write(const std::string& text) = 0;
    virtual void write_error(const std::string& text) = 0;
};

// One R12 command prompt, driven a line at a time.
//
// This is where "what does a typed line mean" lives, and it is deliberately not
// a loop: a GUI has no loop to give it. `ncad` wraps it in a std::cin loop; the
// Qt shell calls feed_line() from a returnPressed handler. Both get the same
// answer to the same line, which is the only way the two stay honest.
class PromptSession {
public:
    // `interactive` enables the R12 behaviours that only make sense at a live
    // terminal: Enter repeating the last command, and the trailing newline.
    PromptSession(lisp::Context& ctx, lisp::Interp& in, CommandEngine& engine, PromptOutput& out,
                  bool interactive);

    PromptSession(const PromptSession&) = delete;
    PromptSession& operator=(const PromptSession&) = delete;

    // Feeds one typed line. Returns false when the session should end, which
    // means QUIT, EXIT, or (quit) from LISP.
    bool feed_line(const std::string& line);

    // What to show before the next line: the continuation marker, the running
    // command's prompt, or "Command: ".
    std::string current_prompt() const;

    // True while an unterminated LISP form is still open across lines.
    bool continuing() const { return !pending_.empty(); }

    // Call when input ends. False if it ended inside an unterminated form.
    bool finish();

private:
    void report_finished();
    void list_commands();

    lisp::Context& ctx_;
    lisp::Interp& in_;
    CommandEngine& engine_;
    PromptOutput& out_;
    bool interactive_;
    std::string pending_;       // an incomplete LISP form spanning lines
    std::string last_command_;  // what Enter repeats
};

// One command prompt session over std::cin. Returns the process exit code.
int run_command_prompt(lisp::Context& ctx, lisp::Interp& in, CommandEngine& engine,
                       bool interactive);

}  // namespace noto::app
