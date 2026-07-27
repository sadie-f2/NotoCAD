// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The first commands. Three, chosen because they are different shapes of state
// machine rather than because they are the three most useful:
//
//   CIRCLE  a fixed sequence of prompts, then done
//   LINE    an unbounded loop with keywords and an undo history
//   ERASE   repeated selection terminated by Enter
//
// Between them they cover what the engine has to support. A fourth command that
// does not fit one of these shapes is a sign the abstraction is wrong.
#pragma once

#include "noto/command.hpp"

#include <string_view>

namespace noto {

// LINE: first point, then next points until Enter. Close joins back to the
// start; Undo removes the last segment.
class LineCommand final : public Command {
public:
    const char* name() const override { return "LINE"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    Prompt next_prompt() const;

    Vec3 first_{};
    Vec3 previous_{};
    bool have_first_{false};
    std::vector<Vec3> vertices_;     // every point accepted so far
    std::vector<Handle> segments_;   // the entity for each segment, for Undo
};

// CIRCLE: centre, then radius. Diameter switches which the second answer means.
class CircleCommand final : public Command {
public:
    const char* name() const override { return "CIRCLE"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    enum class State : std::uint8_t { Centre, Radius };

    State state_{State::Centre};
    Vec3 centre_{};
    bool diameter_{false};
};

// ERASE: select entities until Enter, then delete them all.
class EraseCommand final : public Command {
public:
    const char* name() const override { return "ERASE"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    Prompt select_prompt() const;

    std::vector<Handle> selected_;
};

// DXFOUT: prompt for a file name and write the drawing. In R12 this is a
// command, and it only lived as a LISP function because the command layer did
// not exist yet. The (dxfout ...) function stays -- scripts want it -- but this
// is the form a person types.
class DxfOutCommand final : public Command {
public:
    const char* name() const override { return "DXFOUT"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;
};

// Looks a command up by name, case-insensitively. Returns nullptr if unknown,
// which callers report rather than treating as a crash.
CommandPtr make_command(std::string_view name);

// The registered command names, for help text and completion.
const std::vector<std::string>& command_names();

}  // namespace noto
