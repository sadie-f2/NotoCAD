// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The text input source: typed lines and script files.
//
// One of several InputSource implementations, and deliberately not a privileged
// one -- it knows nothing the others do not. Parsing is driven by the prompt, so
// "5" means a distance at a distance prompt and a string at a string prompt,
// exactly as R12 behaves.
#pragma once

#include "noto/command.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace noto {

// Parses one token against a prompt. Returns false if it cannot be read as the
// kind the prompt wants, with the reason in `error`.
bool parse_input(const Prompt& prompt, std::string_view token, InputValue& out,
                 std::string& error);

// Splits a script into tokens. Whitespace and newlines separate them, a blank
// line is a token in its own right (it means Enter), and quoted runs stay
// together so a string answer can contain spaces.
std::vector<std::string> tokenize_script(std::string_view text);

// Feeds a fixed list of tokens. This is script playback, and it is also how the
// tests drive commands without a terminal.
class TextInputSource final : public InputSource {
public:
    explicit TextInputSource(std::vector<std::string> tokens)
        : tokens_(std::move(tokens)) {}

    bool next_value(const Prompt& prompt, InputValue& out) override;

    bool exhausted() const { return pos_ >= tokens_.size(); }
    std::size_t position() const { return pos_; }

    // Set when a token could not be parsed; the command itself is untouched.
    const std::string& error() const { return error_; }
    bool failed() const { return !error_.empty(); }

private:
    std::vector<std::string> tokens_;
    std::size_t pos_{0};
    std::string error_;
};

}  // namespace noto
