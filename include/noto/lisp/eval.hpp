// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// AutoLISP evaluator.
//
// Scoping is dynamic, not lexical. This is not an oversight: R12 AutoLISP binds
// dynamically, real-world LISP files rely on it (a defun reads a variable set by
// its caller), and lexical scope would silently break them. Bindings are shallow
// -- a call saves the symbol's current value, overwrites it, and restores on
// return -- so variable access stays a pointer dereference.
//
// Errors are status codes, never exceptions, matching the reader and the dialect
// rules in CLAUDE.md. Every entry point returns bool; on false, error() says why.
#pragma once

#include "noto/lisp/value.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace noto::lisp {

enum class EvalStatus : std::uint8_t {
    Ok,
    UnboundVariable,
    UndefinedFunction,
    BadArgumentType,
    WrongArgumentCount,
    DivideByZero,
    StackOverflow,
    BadSyntax,
    ReadFailed,
};

const char* eval_status_message(EvalStatus s);

struct EvalError {
    EvalStatus status{EvalStatus::Ok};
    std::string detail;

    bool ok() const { return status == EvalStatus::Ok; }

    // "unbound variable: FOO"
    std::string message() const;
};

// Special forms take their arguments unevaluated, so they are dispatched ahead
// of the builtin table.
enum class Special : std::int16_t {
    Quote,
    If,
    Cond,
    While,
    Repeat,
    Progn,
    Setq,
    Defun,
    Lambda,
    Foreach,
    And,
    Or,
};

class Interp;

// A builtin receives arguments already evaluated. Returns false on error, having
// called in.fail().
using SubrFn = bool (*)(Interp& in, const Value* args, std::size_t argc, Value& out);

inline constexpr std::size_t kNoMax = static_cast<std::size_t>(-1);

struct SubrDef {
    const char* name;
    SubrFn fn;
    std::size_t min_args;
    std::size_t max_args;  // kNoMax when variadic
};

// The builtin table, shared by every Interp.
const SubrDef* subr_table(std::size_t& count);

class Interp {
public:
    // Installs the builtin and special-form names into ctx's symbol table.
    explicit Interp(Context& ctx);

    Interp(const Interp&) = delete;
    Interp& operator=(const Interp&) = delete;

    // Evaluates one form.
    bool eval(const Value& form, Value& out);

    // Reads and evaluates every form in source; out receives the last value.
    // A read error is reported as EvalStatus::ReadFailed with the reader's
    // message and position in detail.
    bool eval_string(std::string_view source, Value& out);

    // Calls a function -- a symbol, a subr, or a (LAMBDA ...) list -- with
    // arguments that have already been evaluated.
    bool apply(const Value& fn, const Value* args, std::size_t argc, Value& out);

    Context& ctx() { return ctx_; }

    const EvalError& error() const { return error_; }
    void clear_error() { error_ = EvalError{}; }

    // Always returns false, so builtins can `return in.fail(...)`.
    bool fail(EvalStatus status, std::string detail = {});

    // Where princ, print and terpri write. Defaults to std::cout.
    void set_output(std::ostream* os) { out_ = os; }
    std::ostream& output();

    // Guards against runaway recursion. Blowing the C++ stack is a crash; this
    // turns it into an error the REPL can report and carry on from.
    std::size_t max_depth() const { return max_depth_; }
    void set_max_depth(std::size_t d) { max_depth_ = d; }

private:
    // One saved binding on the dynamic-scope stack.
    struct Binding {
        Symbol* sym;
        Value saved;
        bool was_bound;
    };

    void install_builtins();

    bool eval_call(const Value& form, Value& out);
    bool eval_special(Special which, const Value& args, Value& out);
    bool eval_body(const Value& body, Value& out);
    bool eval_args(const Value& args, std::size_t& base, std::size_t& argc);

    bool apply_lambda(const Value& fn, const Value* args, std::size_t argc, Value& out);
    bool call_subr(std::int32_t index, const Value* args, std::size_t argc, Value& out);

    void bind(Symbol* sym, const Value& v);
    void unwind_to(std::size_t mark);

    Context& ctx_;
    EvalError error_{};
    std::ostream* out_{nullptr};

    // Evaluated arguments for calls in progress, one contiguous region per frame.
    std::vector<Value> stack_;
    std::vector<Binding> bindings_;

    std::size_t depth_{0};
    std::size_t max_depth_{2000};
};

}  // namespace noto::lisp
