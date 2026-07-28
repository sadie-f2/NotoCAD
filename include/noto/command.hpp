// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Commands as resumable state machines.
//
// This is a structural decision, not a style preference. A command written as
//
//     Vec3 a = read_point("First point: ");   // blocks
//     Vec3 b = read_point("Next point: ");    // blocks
//
// can be driven by a keyboard and by nothing else. It cannot be driven by
// AutoLISP's (command ...), because there is no caller to block on; it cannot be
// driven by a GUI, because blocking inside an event handler freezes the window.
// Retrofitting that later is a rewrite of every command, which is why it is here
// before there are any.
//
// So: every prompt is a state transition. A command is asked for its next step,
// hands back a Prompt, and is later handed the value that answered it. It never
// waits. The engine holds the suspended state between steps.
//
// That makes the input side a plain abstraction with several implementations:
// typed text, a script file, AutoLISP's (command ...), and -- when the Qt shell
// lands -- mouse and keyboard events. None of them is privileged, and a command
// cannot tell which one it is talking to.
#pragma once

#include "noto/database.hpp"
#include "noto/osnap.hpp"
#include "noto/selection.hpp"
#include "noto/view_control.hpp"
#include "noto/vec3.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace noto {

// What kind of answer a prompt wants. The GUI will use this to decide whether to
// rubber-band a line, show an aperture, or just take text.
enum class PromptKind : std::uint8_t {
    Point,
    Distance,
    Angle,
    Real,
    Integer,
    String,
    Entity,
};

struct Prompt {
    PromptKind kind{PromptKind::Point};
    std::string message;                 // "Specify first point"
    std::vector<std::string> keywords;   // e.g. {"Close", "Undo"}
    bool allow_empty{false};             // Enter is a valid answer

    // Where a rubber-band line starts from. Set by the command; the engine does
    // not use it, a viewport does.
    Vec3 base{};
    bool has_base{false};

    // R12's LASTPOINT: the last point entered, which outlives the command that
    // took it, so `LINE / @5,0` works right after a CIRCLE. Stamped on by the
    // engine rather than by commands -- it is not theirs to know about. Usually
    // equal to `base` during a command, and different everywhere else.
    Vec3 last_point{};
    bool has_last_point{false};

    // "Specify next point or [Close/Undo]: "
    std::string text() const;
};

enum class InputKind : std::uint8_t {
    None,     // Enter pressed with no input
    Point,
    Real,
    Integer,
    String,
    Keyword,  // one of the prompt's keywords, stored upcased in `text`
    Entity,
    Cancel,   // Escape

    // A one-shot object snap override, typed at a point prompt: "cen", "mid",
    // "non". Not an answer to the prompt -- the engine absorbs it, records it,
    // and asks the same question again. Commands never see one, which is the
    // point: an override is about how the *next* point is found, not about what
    // the command wanted.
    OsnapOverride,
};

// Whether a prompt of this kind can be answered by pointing at somewhere.
//
// One rule, asked by everything: the text parser uses it to decide whether a
// coordinate is a valid answer, and the viewport uses it to decide whether a
// click means anything, whether to track object snaps, and whether to rubber
// band. Those three had drifted apart -- the parser accepted a coordinate for a
// radius while the viewport refused the click that would produce one -- and a
// second copy of this judgement is how they drifted.
//
// Distance and Angle qualify because a magnitude and a direction are both things
// you show rather than type: a radius is the distance from Prompt::base to where
// you pointed, an angle is the direction to it. Real, Integer and String do not:
// there is no geometry that answers "how many rows" or "what file name".
bool prompt_takes_point(PromptKind kind);

struct InputValue {
    InputKind kind{InputKind::None};
    Vec3 point{};
    double real{0.0};
    std::int32_t integer{0};
    std::string text;
    Handle entity{kNullHandle};

    static InputValue none();
    static InputValue cancel();
    static InputValue of_point(const Vec3& p);
    static InputValue of_real(double v);
    static InputValue of_integer(std::int32_t v);
    static InputValue of_string(std::string s);
    static InputValue of_keyword(std::string s);
    static InputValue of_entity(Handle h);

    // The mask rides in `integer`. kOsnapNone is meaningful here -- it is NON,
    // "no snap for this pick" -- so the kind, not the value, says an override
    // is present.
    static InputValue of_osnap_override(OsnapMask mask);
};

enum class StepKind : std::uint8_t {
    Prompt,     // needs another value
    Done,
    Cancelled,
    Failed,
};

// What a command wants to happen next.
struct Step {
    StepKind kind{StepKind::Done};
    Prompt prompt;
    std::string message;

    static Step ask(Prompt p);
    static Step done(std::string msg = {});
    static Step cancelled();
    static Step failed(std::string msg);
};

// What a command is allowed to touch. Still deliberately narrow: no view, no
// UI. New members arrive explicitly, so the coupling stays visible.
//
// The selection set is the second member, and it is here rather than inside
// each command because R12's Previous outlives the command that built it: ERASE
// then MOVE Previous is one selection used twice. The engine owns it for the
// same reason it owns LASTPOINT -- state that spans commands belongs to
// whatever spans commands.
struct CommandContext {
    Database& db;
    SelectionSet& selection;

    // R12's Previous: the last selection an editing command actually used. It
    // survives intervening commands that select nothing, so ERASE, then LINE,
    // then MOVE Previous still means the entities ERASE was given.
    const SelectionSet& previous;

    // The view, when there is one. Null in `ncad`, which has no display, and a
    // command that needs it must say so rather than pretend -- see
    // view_control.hpp for why this is an interface and not a Viewport.
    ViewControl* view{nullptr};
};

class Command {
public:
    virtual ~Command() = default;

    virtual const char* name() const = 0;

    // Begins the command. Returns the first prompt, or Done for a command that
    // needs no input.
    virtual Step start(CommandContext& ctx) = 0;

    // Hands over the value answering the previous prompt.
    virtual Step next(CommandContext& ctx, const InputValue& value) = 0;
};

using CommandPtr = std::unique_ptr<Command>;

// Where values come from. The single abstraction the whole design rests on.
class InputSource {
public:
    virtual ~InputSource() = default;

    // Fills `out` and returns true when a value is available. Returning false
    // means "nothing right now" -- the engine suspends and the caller regains
    // control. A GUI always returns false and calls CommandEngine::supply() from
    // its event handler instead.
    virtual bool next_value(const Prompt& prompt, InputValue& out) = 0;
};

enum class EngineStatus : std::uint8_t {
    Idle,       // no command running
    Waiting,    // a command is suspended, waiting for a value
    Finished,
    Cancelled,
    Failed,
};

class CommandEngine {
public:
    explicit CommandEngine(Database& db) : ctx_{db, selection_, previous_, nullptr} {}

    // Not owned. Set by whatever has a display; left null by `ncad`.
    void set_view_control(ViewControl* view) { ctx_.view = view; }
    ViewControl* view_control() { return ctx_.view; }

    CommandEngine(const CommandEngine&) = delete;
    CommandEngine& operator=(const CommandEngine&) = delete;

    // Starts a command. Any command already running is cancelled first, which is
    // what R12 does when a new command is typed at a prompt.
    EngineStatus begin(CommandPtr cmd);

    // Advances the suspended command by exactly one value. This is the entry
    // point a GUI event handler calls.
    EngineStatus supply(const InputValue& value);

    // Pulls values from `src` until the command finishes or the source runs dry.
    // Script playback and (command ...) both drive it this way.
    EngineStatus run(InputSource& src);

    void cancel();

    EngineStatus status() const { return status_; }
    bool active() const { return status_ == EngineStatus::Waiting; }

    // Valid while active().
    const Prompt& prompt() const { return prompt_; }

    // Why the last command finished, failed or was cancelled.
    const std::string& message() const { return message_; }

    // Empty when idle.
    const char* command_name() const { return command_ ? command_->name() : ""; }

    Database& db() { return ctx_.db; }

    // The current selection, which survives between commands so that Previous
    // means something.
    SelectionSet& selection() { return selection_; }
    const SelectionSet& selection() const { return selection_; }
    const SelectionSet& previous_selection() const { return previous_; }

    // The pending one-shot osnap override, if any. Set by supplying an
    // InputValue of kind OsnapOverride, and cleared as soon as any value
    // actually answers a prompt -- an override lasts for exactly one pick,
    // which is what makes it safe to reach for mid-command.
    //
    // A viewport reads this in place of OSMODE while it is set. Note that an
    // override of kOsnapNone is a real state: it means snap to nothing.
    bool has_osnap_override() const { return has_osnap_override_; }
    OsnapMask osnap_override() const { return osnap_override_; }
    void clear_osnap_override() {
        has_osnap_override_ = false;
        osnap_override_ = kOsnapNone;
    }

    // LASTPOINT. Updated whenever a point is supplied, and readable so a
    // viewport can draw from it.
    const Vec3& last_point() const { return last_point_; }
    bool has_last_point() const { return has_last_point_; }
    void set_last_point(const Vec3& p) {
        last_point_ = p;
        has_last_point_ = true;
    }

private:
    EngineStatus apply(const Step& step);
    void open_group(const char* name);
    void close_group();

    // Declared before ctx_, which holds references to them.
    SelectionSet selection_;
    SelectionSet previous_;
    CommandContext ctx_;
    CommandPtr command_;
    Prompt prompt_{};
    std::string message_;
    EngineStatus status_{EngineStatus::Idle};
    Vec3 last_point_{};
    bool has_last_point_{false};
    OsnapMask osnap_override_{kOsnapNone};
    bool has_osnap_override_{false};
    bool group_open_{false};
};

}  // namespace noto
