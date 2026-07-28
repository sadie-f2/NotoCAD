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
#include "noto/database.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace noto {

// Parses one token against a prompt. Returns false if it cannot be read as the
// kind the prompt wants, with the reason in `error`.
//
// `ucs_to_world` is the current UCS's frame, and is what makes a typed
// coordinate mean what R12 says it means: coordinates are entered in the
// current user coordinate system, not in world. Null is world, which is what
// every caller wanted before UCS existed and what a caller with no database
// still wants.
//
// Only TYPED coordinates need it. A point that arrived by clicking is already
// world -- the viewport unprojected it -- and does not pass through here, which
// is why the conversion belongs at this boundary rather than inside the engine
// where the two would be indistinguishable.
//
// An absolute coordinate is transformed as a point. A relative one -- R12's
// `@5,0` -- is a displacement from the last point, so its offset is transformed
// as a VECTOR and added to a base that is already world. Transforming it as a
// point would apply the UCS origin twice.
bool parse_input(const Prompt& prompt, std::string_view token, InputValue& out,
                 std::string& error, const Mat4* ucs_to_world = nullptr);

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

    // The drawing whose current UCS typed coordinates are read in. Optional:
    // left null, coordinates are world, which is what every test that predates
    // UCS expects and what a caller with no drawing means anyway.
    //
    // Held rather than resolved once, because a script may CHANGE the UCS part
    // way through and everything after it must be read in the new one.
    void set_database(const Database* db) { db_ = db; }

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
    const Database* db_{nullptr};
};

}  // namespace noto
