// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// AutoLISP's (command ...) -- the second implementation of InputSource, and the
// one that justifies the whole resumable-command design.
//
// The behaviour that forces it: a command can be started in one (command ...)
// call and finished in a later one.
//
//     (command "LINE" '(0 0 0) '(10 0 0))   ; LINE is still running
//     (command '(10 10 0) "")               ; another vertex, then Enter
//
// There is no way to serve that with a blocking read_point(), because between
// the two calls control has returned to the interpreter and the command's state
// has to be sitting somewhere. It sits in the CommandEngine.
#include "command_subr.hpp"

#include "noto/command.hpp"
#include "noto/commands.hpp"
#include "noto/input_text.hpp"
#include "noto/lisp/command_input.hpp"

#include <string>
#include <vector>

namespace noto::lisp {
namespace {

// Feeds already-evaluated LISP values to a command. Converting from Value to
// InputValue is prompt-directed, exactly as text parsing is.
class LispInputSource final : public InputSource {
public:
    LispInputSource(const Value* args, std::size_t argc, std::size_t start)
        : args_(args), argc_(argc), pos_(start) {}

    bool next_value(const Prompt& prompt, InputValue& out) override;

    std::size_t position() const { return pos_; }
    bool exhausted() const { return pos_ >= argc_; }
    const std::string& error() const { return error_; }
    bool failed() const { return !error_.empty(); }

    // A command name appearing mid-argument-list means the current command is
    // finished and a new one starts.
    bool at_command_name() const;

private:
    const Value* args_;
    std::size_t argc_;
    std::size_t pos_;
    std::string error_;
};

std::string upcase(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

bool LispInputSource::at_command_name() const {
    if (pos_ >= argc_) return false;
    const Value& v = args_[pos_];
    if (v.type != Type::Str) return false;
    const std::string text = upcase(v.str->view());
    if (text.empty()) return false;  // "" is Enter, not a command
    return make_command(text) != nullptr;
}

bool LispInputSource::next_value(const Prompt& prompt, InputValue& out) {
    if (pos_ >= argc_) return false;

    // The prompt gets first refusal. A string is a command name only where the
    // running command cannot use it: "U" at LINE's next-point prompt is Undo
    // even once an UNDO command exists, and "LINE" is a perfectly good file
    // name at DXFOUT's prompt.
    const Value& v = args_[pos_];
    std::string why;
    if (value_to_input(prompt, v, out, why)) {
        ++pos_;
        return true;
    }

    // Unusable here. If it names a command, stop without consuming it so the
    // caller starts that command instead.
    if (at_command_name()) return false;

    error_ = "(command): " + why;
    ++pos_;  // consuming it stops the run from spinning on the same value
    return false;
}

}  // namespace

// (command "LINE" '(0 0 0) '(10 0 0) "") -> nil, always, as AutoLISP has it.
//
// Arguments are consumed left to right. A string naming a command starts that
// command; everything else answers the current prompt. Arguments running out
// with a command still active is not an error -- the command stays suspended
// and a later (command ...) can continue it.
bool subr_command(Interp& in, const Value* args, std::size_t argc, Value& out) {
    CommandEngine* engine = in.command_engine();
    if (!engine) {
        return in.fail(EvalStatus::BadArgumentType, "command: no command engine is attached");
    }

    out = make_nil();

    // (command) with no arguments cancels, matching R12.
    if (argc == 0) {
        engine->cancel();
        return true;
    }

    LispInputSource source(args, argc, 0);

    while (!source.exhausted()) {
        if (source.at_command_name()) {
            const std::size_t at = source.position();
            const std::string name = upcase(args[at].str->view());
            CommandPtr cmd = make_command(name);
            // at_command_name() already established this resolves.
            engine->begin(std::move(cmd));
            // Skip the name itself and carry on feeding the new command.
            LispInputSource rest(args, argc, at + 1);
            engine->run(rest);
            if (rest.failed()) {
                return in.fail(EvalStatus::BadArgumentType, rest.error());
            }
            if (engine->status() == EngineStatus::Failed) {
                return in.fail(EvalStatus::BadArgumentType,
                               name + ": " + engine->message());
            }
            source = LispInputSource(args, argc, rest.position());
            continue;
        }

        // No command name: these values continue whatever is already running.
        if (!engine->active()) {
            return in.fail(EvalStatus::BadArgumentType,
                           "command: no command is running to receive " + prin1(args[source.position()]));
        }
        engine->run(source);
        if (source.failed()) return in.fail(EvalStatus::BadArgumentType, source.error());
        if (engine->status() == EngineStatus::Failed) {
            return in.fail(EvalStatus::BadArgumentType, engine->message());
        }
    }
    return true;
}

}  // namespace noto::lisp
