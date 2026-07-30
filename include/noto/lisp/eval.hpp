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
#include "noto/selection.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iosfwd>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

namespace noto {
class Database;
class CommandEngine;
}

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
    Cancelled,   // the user interrupted; not a fault in the script
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

    // Closes anything a script left open. A script that ends without closing
    // its output has still written it, and losing the tail to an unflushed
    // buffer would be the worst kind of quiet.
    ~Interp();

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

    // The drawing entmake, entget and friends operate on. Null until attached,
    // in which case those functions report an error rather than crashing --
    // the interpreter is usable for pure computation without a drawing.
    void set_database(Database* db) { db_ = db; }
    Database* database() { return db_; }

    // Drives (command ...). Held rather than created per call because a command
    // may be started by one (command ...) and finished by a later one -- which
    // is the whole reason commands are resumable state machines.
    void set_command_engine(CommandEngine* engine) { engine_ = engine; }
    CommandEngine* command_engine() { return engine_; }

    // Selection sets live here rather than in the arena: they own a vector of
    // handles, and the arena is for values that can be dropped wholesale. A
    // Value holds an index into this table, so a set passed between variables
    // is shared rather than copied, which is how AutoLISP behaves.
    std::int32_t new_selection_set(SelectionSet set);
    SelectionSet* selection_set(std::int32_t index);
    const SelectionSet* selection_set(std::int32_t index) const;
    void clear_selection_sets() { ssets_.clear(); }

    // Open files, on the same terms and for the same reason: a FILE value is an
    // index into this table, so passing a descriptor between variables shares
    // one open file rather than duplicating it.
    //
    // INDICES ARE NEVER REUSED. A closed slot keeps its place with a null
    // handle, so a descriptor held past its (close ...) reports "not open"
    // rather than silently reading whatever was opened next.
    struct OpenFile {
        std::FILE* fp{nullptr};
        bool writable{false};
    };

    std::int32_t new_file(std::FILE* fp, bool writable);
    OpenFile* open_file(std::int32_t index);
    bool close_file(std::int32_t index);

    // Every file still open. Called when the interpreter is torn down, because
    // a script that ends without closing its output has still written it, and
    // losing the tail to an unflushed buffer would be the worst kind of quiet.
    void close_all_files();

    // Where (tblnext ...) has got to in each symbol table.
    //
    // Per interpreter rather than global, and keyed by name rather than by an
    // enum, because the caller names the table as a string and this layer has no
    // reason to hold a second list of which tables exist. `rewind` sets it back
    // to zero; tblsearch's optional third argument sets it to a found entry.
    std::size_t& table_cursor(const std::string& table);

    // --- interruption --------------------------------------------------------
    //
    // Set from OUTSIDE the evaluator -- an Escape at the command line, a signal
    // handler -- and checked on every form. Without it a (while t ...) is
    // unstoppable short of killing the process, which is a hang rather than a
    // defect and looks the same to whoever is waiting.
    //
    // A plain bool because there is one interpreter per session and the flag is
    // set by the same thread that is not currently inside eval().
    void interrupt() { interrupted_ = true; }
    bool interrupted() const { return interrupted_; }
    void clear_interrupt() { interrupted_ = false; }

    // Calls the script's *error* function, if it defined one. Called on the way
    // out of a failed top-level evaluation; see the definition for why it is a
    // hook rather than a condition system.
    void run_error_handler();

    const EvalError& error() const { return error_; }
    void clear_error() { error_ = EvalError{}; }

    // Set by (quit) / (exit). Evaluation of remaining forms stops, but this is
    // not an error -- the host decides what to do about it.
    bool quit_requested() const { return quit_; }
    void request_quit() { quit_ = true; }
    void clear_quit() { quit_ = false; }

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
    Database* db_{nullptr};
    CommandEngine* engine_{nullptr};
    std::vector<SelectionSet> ssets_;
    std::vector<OpenFile> files_;
    std::vector<std::pair<std::string, std::size_t>> table_cursors_;
    bool interrupted_{false};
    bool in_error_handler_{false};
    EvalError error_{};
    std::ostream* out_{nullptr};

    // Evaluated arguments for calls in progress, one contiguous region per frame.
    std::vector<Value> stack_;
    std::vector<Binding> bindings_;

    bool quit_{false};
    std::size_t depth_{0};
    std::size_t max_depth_{2000};
};

}  // namespace noto::lisp
