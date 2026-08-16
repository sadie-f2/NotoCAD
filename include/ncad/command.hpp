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

#include "ncad/database.hpp"
#include "ncad/drawing_lock.hpp"
#include "ncad/inflight.hpp"
#include "ncad/osnap.hpp"
#include "ncad/script_loader.hpp"
#include "ncad/selection.hpp"
#include "ncad/tables.hpp"
#include "ncad/vec3.hpp"
#include "ncad/view_control.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ncad {

class Clipboard;

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

// What should trail the cursor from `base` while a point prompt stands.
//
// Advice for whatever is drawing, in the same way `base` itself is: the engine
// never reads it, and text-driven input ignores it entirely. It exists because
// `PromptKind` cannot answer the question -- the second corner of a selection
// window and the next point of a LINE are both a Point prompt with a base, and
// they want completely different glyphs. Putting it here rather than adding a
// PromptKind keeps `prompt_takes_point()` and the input parser, which both
// switch on the kind, out of a decision that is purely about display.
enum class RubberBand : std::uint8_t {
    None,  // nothing follows the cursor
    Line,  // a band from `base` to the cursor -- LINE, MOVE, the ordinary case
    Box,   // a screen-aligned rectangle with `base` at the opposite corner
};

// What a String prompt is really asking for, when the answer is a file name.
//
// Advice for whoever is asking, in exactly the way RubberBand is: the engine
// never reads it and the terminal ignores it entirely. It exists because "the
// answer to this is a file that must already exist" is a fact about the ANSWER,
// not an instruction to show a dialog -- which is the line that keeps
// QFileDialog out of the core while still letting a window put one up.
enum class FileIntent : std::uint8_t {
    None,
    Open,  // must already exist
    Save,  // may be created, and replacing wants confirming
};

struct Prompt {
    PromptKind kind{PromptKind::Point};
    std::string message;                 // "Specify first point"
    std::vector<std::string> keywords;   // e.g. {"Close", "Undo"}
    bool allow_empty{false};             // Enter is a valid answer

    // Only meaningful on a String prompt. `file_extension` carries no dot --
    // "dxf" -- and a window builds whatever filter it likes from it.
    FileIntent file{FileIntent::None};
    std::string file_extension;

    // The formats this file could be written in, when the command is going to
    // ask about it in a moment. Declared on the NAME prompt because that is
    // when a save dialog is built and the choice belongs in it -- "Save as
    // type" is where people look for a format, not in a question afterwards.
    //
    // Stating it here does not commit anyone to anything: the terminal ignores
    // the list and answers the later prompt by typing, exactly as before.
    std::vector<std::string> file_formats;

    // And this is that later prompt. A window that folded the choice into its
    // save dialog answers this from what was chosen there, rather than asking
    // the same question twice.
    bool file_format{false};

    // "That file exists -- replace it?". Marked for the same reason: every
    // platform's save dialog asks this itself, so a window that used one has
    // already had the answer and must not ask again. The terminal, which has
    // no dialog to have asked, still does.
    bool file_overwrite{false};

    // Where the rubber band starts from. Set by the command; the engine does
    // not use it, a viewport does.
    Vec3 base{};
    bool has_base{false};

    // And what shape it is. Only meaningful when `has_base`; Line is the
    // default so that every prompt written before this existed keeps drawing
    // what it always did.
    RubberBand rubber_band{RubberBand::Line};

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

    // Set only for an entity answer that came from pointing at something. See
    // of_picked_entity.
    bool has_point{false};

    // Provenance for a point that came from an object snap, and which the snap
    // could NOT resolve on its own.
    //
    // TANGENT to the FIRST point of a line is the case: there is no tangent
    // until the other end exists, so the snap can only record which entity was
    // pointed at and where. The command holds that constraint and solves it
    // once it has the far end -- which is what AutoCAD's deferred tangent does.
    // `point` still carries a usable location, so a command that ignores all of
    // this still behaves sensibly.
    OsnapType snap_type{OsnapType::Endpoint};
    Handle snap_entity{kNullHandle};
    bool snap_deferred{false};

    static InputValue none();
    static InputValue cancel();
    static InputValue of_point(const Vec3& p);
    static InputValue of_real(double v);
    static InputValue of_integer(std::int32_t v);
    static InputValue of_string(std::string s);
    static InputValue of_keyword(std::string s);
    // An entity answer may carry WHERE it was picked as well as which one.
    //
    // BREAK is why: R12 takes the point you pointed at as the first break
    // point, because pointing at an object means pointing somewhere on it. A
    // handle alone cannot say that, and asking a second time would change the
    // command's prompt sequence.
    //
    // `has_point` distinguishes a pick from a typed handle or a LISP ename,
    // neither of which has a location. Commands that do not care ignore both.
    static InputValue of_entity(Handle h);
    static InputValue of_picked_entity(Handle h, const Vec3& at);

    // A point answer that also says which snap produced it and on what, for the
    // deferred case above. `at` is where the marker was drawn, which is also
    // how the command chooses between two tangents.
    static InputValue of_deferred_snap(const Vec3& at, OsnapType type, Handle from);

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

// State that outlives the command that set it, without belonging to the drawing.
//
// The third answer to a question this codebase has now met three times. R12 has
// several options whose whole point is "the one from last time" -- UCS Prev,
// ROTATE3D's Last axis, LASTPOINT -- and none of them can live on the command,
// because the command is gone by the time the next one asks.
//
// It is SESSION state, not drawing state, and the difference decides where it
// goes. A drawing saved and reopened has no previous UCS and no last axis: those
// describe what you were doing, not what you drew. So they are not journalled
// and not written to DXF, and undoing a UCS change does not restore what Prev
// would have given -- which is R12's behaviour too.
//
// One struct rather than a member each, so that the fourth of these costs a
// field instead of another CommandContext member.
struct CommandMemory {
    // ROTATE3D Last: the axis the previous 3D rotation turned about.
    Vec3 last_axis_origin{};
    Vec3 last_axis_direction{};
    bool has_last_axis{false};

    // UCS Prev: the coordinate system in force before the current one.
    Ucs previous_ucs{};
    bool has_previous_ucs{false};

    // OFFSET's distance, and FILLET's and CHAMFER's sizes. R12 offers each
    // back as the default because using one twice is the normal case --
    // offsetting a whole outline, or rounding every corner the same way.
    double offset_distance{0.0};
    bool offset_through{false};
    bool has_offset{false};

    double fillet_radius{0.0};
    double chamfer_a{0.0};
    double chamfer_b{0.0};

    // What the last dimension measured, offered back as LEADER's default note.
    //
    // This is why R12 kept LEader inside DIM rather than giving it a command:
    // you dimension something, then draw a leader, and it offers that number.
    // The leader measures nothing itself -- the number belongs to the session,
    // which is what makes this the right place for it.
    std::string last_measurement;
    bool has_last_measurement{false};
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

    // Session-scoped scratch: the "same as last time" options. Owned by the
    // engine, beside the selection and LASTPOINT, for the same stated reason --
    // state that spans commands belongs to whatever spans commands.
    CommandMemory& memory;

    // The view, when there is one. Null in `ncad`, which has no display, and a
    // command that needs it must say so rather than pretend -- see
    // view_control.hpp for why this is an interface and not a Viewport.
    ViewControl* view{nullptr};

    // APPLOAD's way into the interpreter. Null only if nobody wired one up --
    // see script_loader.hpp for why this is an interface and not an Interp*.
    ScriptLoader* scripts{nullptr};

    // COPYCLIP and PASTECLIP's transport. An interface for the reason
    // ViewControl is -- the Qt shell hands over the system clipboard, `ncad`
    // an in-process one -- and null only if nobody wired one up.
    Clipboard* clipboard{nullptr};

    // The session's advisory lock on the drawing currently open. Owned by the
    // engine, like the selection and LASTPOINT, because it outlives every
    // command that touches it -- OPEN takes it and NEW releases it, with any
    // number of other commands in between.
    //
    // Null where there is no session to speak for, which is why every use is
    // guarded rather than assumed.
    DrawingLockHolder* locks{nullptr};
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

    // What the drawing WOULD look like if `tentative` answered the standing
    // prompt. False when this command has nothing to show for that value, which
    // is the default and is why this is not pure -- most commands never grow
    // one, and a command that has not reached its last prompt has nothing to
    // preview either.
    //
    // `tentative` has NOT arrived. It is where the cursor happens to be, and the
    // command has not been told about it -- so an implementation must not touch
    // its own state, the database or the undo journal. See inflight.hpp; a
    // mouse-move that reached the journal would make every pixel of cursor
    // travel an undo step.
    //
    // The rule that keeps it honest: preview and commit must derive the change
    // with the SAME code, differing only in whether the result is written back.
    // Anything else drifts, and drifts silently, because nothing compares them.
    virtual bool preview(CommandContext& ctx, const InputValue& tentative, InFlight& out) {
        (void)ctx;
        (void)tentative;
        (void)out;
        return false;
    }
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
    explicit CommandEngine(Database& db)
        : ctx_{db, selection_, previous_, memory_, nullptr} {
        // Wired here rather than in the initialiser so the aggregate keeps the
        // shape every other optional member has: named, not positional.
        ctx_.locks = &locks_;
    }

    // Not owned. Set by whatever has a display; left null by `ncad`.
    void set_view_control(ViewControl* view) { ctx_.view = view; }

    // Wired by whoever owns the Interp -- `ncad` and the Qt shell both do, at
    // startup, the same way each wires set_view_control.
    void set_script_loader(ScriptLoader* scripts) { ctx_.scripts = scripts; }
    ViewControl* view_control() { return ctx_.view; }

    // Wired at startup like the two above: the Qt shell hands over the system
    // clipboard, `ncad` an in-process one.
    void set_clipboard(Clipboard* clipboard) { ctx_.clipboard = clipboard; }

    CommandEngine(const CommandEngine&) = delete;
    CommandEngine& operator=(const CommandEngine&) = delete;

    // Starts a command. Any command already running is cancelled first, which is
    // what R12 does when a new command is typed at a prompt.
    EngineStatus begin(CommandPtr cmd);

    // Advances the suspended command by exactly one value. This is the entry
    // point a GUI event handler calls.
    EngineStatus supply(const InputValue& value);

    // Runs a command *inside* another one, R12's apostrophe form: 'ZOOM at a
    // point prompt changes the view and hands the original question back
    // untouched. This is what the resumable design was for -- the outer
    // command's state is already sitting in the engine rather than on a stack
    // somewhere, so suspending it costs a saved prompt and nothing else.
    //
    // Only safe for commands that change no drawing state, which is why the
    // registry marks which ones may be used this way rather than leaving it to
    // whoever types the apostrophe. A transparent command opens no undo group:
    // it is inside the outer command's, and it has nothing to record anyway.
    EngineStatus begin_transparent(CommandPtr cmd);
    bool in_transparent() const { return transparent_ != nullptr; }

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

    // Asks the running command what it would do with `tentative`. False when
    // nothing would be shown -- no command, not waiting, or a command with no
    // preview of its own, which is most of them.
    //
    // Deliberately not routed through supply(): that consumes a value and
    // advances the state machine, and this must do neither. It is the one place
    // the engine's model of "a value arrived" is set aside.
    bool preview(const InputValue& tentative, InFlight& out);

    // The current selection, which survives between commands so that Previous
    // means something.
    SelectionSet& selection() { return selection_; }
    const SelectionSet& selection() const { return selection_; }
    const SelectionSet& previous_selection() const { return previous_; }

    // The "same as last time" state. Readable so a viewport or a test can see
    // what UCS Prev would restore without running the command.
    CommandMemory& memory() { return memory_; }
    const CommandMemory& memory() const { return memory_; }

    // The session's advisory lock on the open drawing. Readable so a front end
    // can say what is held, and so a test can ask without going through a
    // command. Released by ~CommandEngine, which is what covers every exit path
    // in both `ncad` and the Qt shell without either of them knowing.
    DrawingLockHolder& locks() { return locks_; }
    const DrawingLockHolder& locks() const { return locks_; }

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
    EngineStatus apply_transparent(const Step& step);
    void open_group(const char* name);
    void close_group();

    // Declared before ctx_, which holds references to them.
    SelectionSet selection_;
    SelectionSet previous_;
    CommandMemory memory_;
    DrawingLockHolder locks_;
    CommandContext ctx_;
    CommandPtr command_;

    // The command running inside the outer one, if any, and the question the
    // outer one was asking when it was interrupted.
    CommandPtr transparent_;
    Prompt outer_prompt_{};

    Prompt prompt_{};
    std::string message_;
    EngineStatus status_{EngineStatus::Idle};
    Vec3 last_point_{};
    bool has_last_point_{false};
    OsnapMask osnap_override_{kOsnapNone};
    bool has_osnap_override_{false};
    bool group_open_{false};
};

}  // namespace ncad
