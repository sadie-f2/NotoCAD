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

// A LISP point is a list of two or three numbers.
bool value_to_point(const Value& v, Vec3& out) {
    if (!is_cons(v)) return false;
    double c[3] = {0.0, 0.0, 0.0};
    std::size_t i = 0;
    for (Value cur = v; is_cons(cur); cur = cdr(cur)) {
        if (i >= 3) return false;
        const Value n = car(cur);
        if (!is_number(n)) return false;
        c[i++] = as_double(n);
    }
    if (i < 2) return false;
    out = Vec3{c[0], c[1], c[2]};
    return true;
}

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
    // Stop rather than feeding a command name in as data.
    if (at_command_name()) return false;

    const Value& v = args_[pos_];

    switch (v.type) {
        case Type::Nil:
            // nil is Enter, which is how a (command ...) call ends a loop.
            out = InputValue::none();
            ++pos_;
            return true;

        case Type::Str: {
            const std::string text(v.str->view());
            // "" is Enter.
            if (text.empty()) {
                out = InputValue::none();
                ++pos_;
                return true;
            }
            // A string at a non-string prompt is a keyword, matched the same way
            // typed text is.
            std::string parsed_error;
            InputValue parsed;
            if (parse_input(prompt, text, parsed, parsed_error)) {
                out = parsed;
                ++pos_;
                return true;
            }
            error_ = "(command): " + parsed_error + ": \"" + text + "\"";
            ++pos_;
            return false;
        }

        case Type::Int:
            out = (prompt.kind == PromptKind::Entity)
                      ? InputValue::of_entity(static_cast<Handle>(v.i))
                      : ((prompt.kind == PromptKind::Integer)
                             ? InputValue::of_integer(v.i)
                             : InputValue::of_real(static_cast<double>(v.i)));
            ++pos_;
            return true;

        case Type::Real:
            out = InputValue::of_real(v.d);
            ++pos_;
            return true;

        case Type::Ename:
            out = InputValue::of_entity(v.ename);
            ++pos_;
            return true;

        case Type::Cons: {
            Vec3 p;
            if (!value_to_point(v, p)) {
                error_ = "(command): not a point: " + prin1(v);
                ++pos_;
                return false;
            }
            out = InputValue::of_point(p);
            ++pos_;
            return true;
        }

        default:
            error_ = "(command): cannot use " + std::string(type_name(v.type)) + " as input";
            ++pos_;
            return false;
    }
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
