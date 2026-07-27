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

    // Where a rubber-band line or relative coordinate starts from. The engine
    // does not use it; a viewport does.
    Vec3 base{};
    bool has_base{false};

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
};

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

// What a command is allowed to touch. Deliberately narrow: no view, no
// selection set, no UI. Those arrive as explicit members when they exist, so
// the coupling stays visible.
struct CommandContext {
    Database& db;
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
    explicit CommandEngine(Database& db) : ctx_{db} {}

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

private:
    EngineStatus apply(const Step& step);

    CommandContext ctx_;
    CommandPtr command_;
    Prompt prompt_{};
    std::string message_;
    EngineStatus status_{EngineStatus::Idle};
};

}  // namespace noto
