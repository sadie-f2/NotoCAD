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
#include "noto/render.hpp"

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

// "Select objects:" -- the whole of it, in one place.
//
// A small state machine rather than a helper function, because Window and
// Crossing are not single answers: each asks for two corners, so the selection
// prompt has sub-prompts of its own. Every editing command delegates to this,
// which is what stops MOVE and ERASE quietly disagreeing about what All means
// or which corner order a crossing box wants.
//
// It needs a DrawContext to flatten entities against for the region tests. The
// tolerance only affects how finely curves are diced before being tested, so a
// default is fine for text-driven use; the viewport passes its own.
class SelectionPrompter {
public:
    // What to ask right now, given what has been collected so far.
    Prompt prompt(const CommandContext& ctx) const;

    enum class Result {
        Selecting,  // still collecting; ask prompt() again
        Finished,   // Enter: the selection is complete
        Rejected,   // not something a selection prompt accepts
    };

    Result feed(CommandContext& ctx, const InputValue& value);

    // Set by the viewport, which knows its own zoom and orientation. A window
    // is a screen-aligned box, so the axes it is built on are the view's, not
    // the world's -- these default to world XY, which is right for text-driven
    // selection and for plan view.
    void set_draw_context(const DrawContext& ctx) { draw_ = ctx; }
    void set_view_axes(const Vec3& ax, const Vec3& ay) {
        view_ax_ = ax;
        view_ay_ = ay;
    }

    // Non-empty when the last answer deserves an echo, R12-style "4 found".
    const std::string& note() const { return note_; }

private:
    enum class State : std::uint8_t { Selecting, FirstCorner, SecondCorner };

    void apply_region(CommandContext& ctx, const Vec3& a, const Vec3& b);

    State state_{State::Selecting};
    bool removing_{false};
    bool crossing_{false};
    Vec3 first_{};
    Vec3 view_ax_{1, 0, 0};
    Vec3 view_ay_{0, 1, 0};
    DrawContext draw_{};
    std::string note_;
};

// ERASE: select entities until Enter, then delete them all.
class EraseCommand final : public Command {
public:
    const char* name() const override { return "ERASE"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;

private:
    SelectionPrompter select_;
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

// Looks a command up by its FULL name, case-insensitively. Returns nullptr if
// unknown, which callers report rather than treating as a crash.
//
// Deliberately exact. Abbreviation resolution must not happen here: a string
// argument to (command ...) is a command name only if it matches exactly, and
// resolving prefixes would make (command "LINE" p1 p2 "C") start CIRCLE instead
// of closing the polyline.
// UNDO and REDO. Both complete in one step with no prompt: R12's UNDO takes a
// count and options, but plain "undo one thing" is what it does by default and
// is the whole of what exists here.
//
// Neither is itself undoable, and neither opens a group -- an undo step that
// undoes an undo is how a history turns into a maze. REDO is the inverse.
class UndoCommand final : public Command {
public:
    const char* name() const override { return "UNDO"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;
};

class RedoCommand final : public Command {
public:
    const char* name() const override { return "REDO"; }
    Step start(CommandContext& ctx) override;
    Step next(CommandContext& ctx, const InputValue& value) override;
};

CommandPtr make_command(std::string_view name);

// The registered command names, for help text and completion.
const std::vector<std::string>& command_names();

// An acad.pgp-style abbreviation. R12 shipped a table of these rather than
// deriving them, because the useful short forms are not always prefixes --
// COPY is CP once COPY and CIRCLE both want to be C.
struct CommandAlias {
    std::string alias;
    std::string name;
};

const std::vector<CommandAlias>& command_aliases();

// What a typed command name resolved to.
struct CommandMatch {
    std::string name;                     // canonical name, empty if unresolved
    bool ambiguous{false};                // the prefix matched more than one
    std::vector<std::string> candidates;  // all matches, when ambiguous

    bool ok() const { return !name.empty(); }
};

// Resolves what the user typed, in order: exact name, exact alias, then prefix.
//
// An ambiguous prefix still resolves rather than refusing -- pressing Enter is
// expected to commit to something. The winner is the shortest candidate, ties
// broken alphabetically. Shorter names are the more fundamental commands, so LI
// is LINE rather than LINETYPE and AR is ARC rather than ARRAY, which is the
// right answer for the common case.
//
// Deliberately not AutoCAD's modern behaviour, which ranks by how often you have
// used each command and so shifts underneath you. A command line worth building
// muscle memory against has to resolve the same way next month. Where the rule
// picks wrong, the alias table is the override.
//
// For interactive input only -- see make_command above.
CommandMatch resolve_command_name(std::string_view typed);

// The same resolution against an explicit table, so the rules can be tested
// without waiting for two real commands to share a prefix.
CommandMatch resolve_in(std::string_view typed, const std::vector<std::string>& names,
                        const std::vector<CommandAlias>& aliases);

}  // namespace noto
