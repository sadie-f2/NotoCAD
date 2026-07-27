// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/lisp/eval.hpp"

#include "noto/lisp/reader.hpp"

#include <iostream>
#include <string>

namespace noto::lisp {
namespace {

struct SpecialDef {
    const char* name;
    Special which;
};

// Dispatched before argument evaluation. Everything else is a function.
constexpr SpecialDef kSpecials[] = {
    {"QUOTE", Special::Quote},     {"IF", Special::If},
    {"COND", Special::Cond},       {"WHILE", Special::While},
    {"REPEAT", Special::Repeat},   {"PROGN", Special::Progn},
    {"SETQ", Special::Setq},       {"DEFUN", Special::Defun},
    {"LAMBDA", Special::Lambda},   {"FOREACH", Special::Foreach},
    {"AND", Special::And},         {"OR", Special::Or},
};

// The marker separating a defun's parameters from its local variables:
// (defun f (a b / tmp) ...)
bool is_local_marker(const Value& v) {
    return v.type == Type::Sym && v.sym->name == "/";
}

}  // namespace

const char* eval_status_message(EvalStatus s) {
    switch (s) {
        case EvalStatus::Ok: return "ok";
        case EvalStatus::UnboundVariable: return "unbound variable";
        case EvalStatus::UndefinedFunction: return "no function definition";
        case EvalStatus::BadArgumentType: return "bad argument type";
        case EvalStatus::WrongArgumentCount: return "wrong number of arguments";
        case EvalStatus::DivideByZero: return "divide by zero";
        case EvalStatus::StackOverflow: return "recursion too deep";
        case EvalStatus::BadSyntax: return "malformed expression";
        case EvalStatus::ReadFailed: return "read failed";
    }
    return "unknown error";
}

std::string EvalError::message() const {
    std::string s = eval_status_message(status);
    if (!detail.empty()) {
        s += ": ";
        s += detail;
    }
    return s;
}

// --- construction -----------------------------------------------------------

Interp::Interp(Context& context) : ctx_(context) { install_builtins(); }

void Interp::install_builtins() {
    std::size_t count = 0;
    const SubrDef* table = subr_table(count);
    for (std::size_t i = 0; i < count; ++i) {
        ctx_.intern(table[i].name)->subr = static_cast<std::int32_t>(i);
    }
    for (const SpecialDef& sp : kSpecials) {
        ctx_.intern(sp.name)->special = static_cast<std::int16_t>(sp.which);
    }
}

std::ostream& Interp::output() { return out_ ? *out_ : std::cout; }

bool Interp::fail(EvalStatus status, std::string detail) {
    // Keep the innermost failure: it is the one that describes what went wrong.
    if (error_.ok()) {
        error_.status = status;
        error_.detail = std::move(detail);
    }
    return false;
}

// --- dynamic binding --------------------------------------------------------

void Interp::bind(Symbol* sym, const Value& v) {
    bindings_.push_back(Binding{sym, sym->value, sym->bound});
    sym->value = v;
    sym->bound = true;
}

void Interp::unwind_to(std::size_t mark) {
    while (bindings_.size() > mark) {
        const Binding& b = bindings_.back();
        b.sym->value = b.saved;
        b.sym->bound = b.was_bound;
        bindings_.pop_back();
    }
}

// --- eval -------------------------------------------------------------------

bool Interp::eval(const Value& form, Value& out) {
    switch (form.type) {
        case Type::Sym: {
            Symbol* sym = form.sym;
            // A builtin's name evaluates to the builtin, so it can be passed to
            // apply and mapcar without quoting.
            if (sym->subr >= 0 && !sym->bound) {
                out = make_subr(sym->subr);
                return true;
            }
            if (!sym->bound) {
                return fail(EvalStatus::UnboundVariable, std::string(sym->name));
            }
            out = sym->value;
            return true;
        }
        case Type::Cons:
            return eval_call(form, out);
        default:
            // Numbers, strings, nil, T, enames and files evaluate to themselves.
            out = form;
            return true;
    }
}

bool Interp::eval_call(const Value& form, Value& out) {
    if (depth_ >= max_depth_) return fail(EvalStatus::StackOverflow);

    const Value head = car(form);
    const Value args = cdr(form);

    if (head.type == Type::Sym && head.sym->special >= 0) {
        ++depth_;
        const bool ok = eval_special(static_cast<Special>(head.sym->special), args, out);
        --depth_;
        return ok;
    }

    // Function position: a symbol naming a function, or a literal lambda.
    Value fn = head;
    if (head.type == Type::Sym) {
        Symbol* sym = head.sym;
        if (sym->subr >= 0) {
            fn = make_subr(sym->subr);
        } else if (sym->has_func) {
            fn = sym->func;
        } else {
            return fail(EvalStatus::UndefinedFunction, std::string(sym->name));
        }
    } else if (!is_cons(head)) {
        return fail(EvalStatus::UndefinedFunction, prin1(head));
    }

    ++depth_;
    std::size_t base = 0;
    std::size_t argc = 0;
    bool ok = eval_args(args, base, argc);
    if (ok) {
        // stack_ may reallocate while nested calls evaluate, so the pointer is
        // taken only once every argument is in place.
        ok = apply(fn, stack_.data() + base, argc, out);
    }
    stack_.resize(base);
    --depth_;
    return ok;
}

bool Interp::eval_args(const Value& args, std::size_t& base, std::size_t& argc) {
    base = stack_.size();
    for (Value rest = args; is_cons(rest); rest = cdr(rest)) {
        Value v;
        if (!eval(car(rest), v)) return false;
        stack_.push_back(v);
    }
    argc = stack_.size() - base;
    return true;
}

bool Interp::apply(const Value& fn, const Value* args, std::size_t argc, Value& out) {
    if (fn.type == Type::Subr) return call_subr(fn.subr, args, argc, out);

    // A symbol here comes from (apply 'foo ...) rather than function position.
    if (fn.type == Type::Sym) {
        Symbol* sym = fn.sym;
        if (sym->subr >= 0) return call_subr(sym->subr, args, argc, out);
        if (sym->has_func) return apply_lambda(sym->func, args, argc, out);
        return fail(EvalStatus::UndefinedFunction, std::string(sym->name));
    }

    if (is_cons(fn) && car(fn).type == Type::Sym &&
        car(fn).sym->special == static_cast<std::int16_t>(Special::Lambda)) {
        return apply_lambda(fn, args, argc, out);
    }

    return fail(EvalStatus::UndefinedFunction, prin1(fn));
}

bool Interp::call_subr(std::int32_t index, const Value* args, std::size_t argc, Value& out) {
    std::size_t count = 0;
    const SubrDef* table = subr_table(count);
    if (index < 0 || static_cast<std::size_t>(index) >= count) {
        return fail(EvalStatus::UndefinedFunction);
    }
    const SubrDef& def = table[static_cast<std::size_t>(index)];
    if (argc < def.min_args || (def.max_args != kNoMax && argc > def.max_args)) {
        return fail(EvalStatus::WrongArgumentCount, def.name);
    }
    return def.fn(*this, args, argc, out);
}

// fn is (LAMBDA params . body).
bool Interp::apply_lambda(const Value& fn, const Value* args, std::size_t argc, Value& out) {
    if (depth_ >= max_depth_) return fail(EvalStatus::StackOverflow);

    const Value params = car(cdr(fn));
    const Value body = cdr(cdr(fn));

    // Count declared parameters before binding anything, so an arity mismatch
    // reports cleanly rather than half-binding the frame.
    std::size_t nparams = 0;
    for (Value p = params; is_cons(p); p = cdr(p)) {
        if (is_local_marker(car(p))) break;
        ++nparams;
    }
    if (argc != nparams) {
        return fail(EvalStatus::WrongArgumentCount,
                    "expected " + std::to_string(nparams) + ", got " + std::to_string(argc));
    }

    const std::size_t mark = bindings_.size();
    std::size_t i = 0;
    bool in_locals = false;
    for (Value p = params; is_cons(p); p = cdr(p)) {
        const Value name = car(p);
        if (is_local_marker(name)) {
            in_locals = true;
            continue;
        }
        if (name.type != Type::Sym) {
            unwind_to(mark);
            return fail(EvalStatus::BadSyntax, "parameter is not a symbol");
        }
        // Locals start nil, exactly as R12 has it.
        bind(name.sym, in_locals ? make_nil() : args[i++]);
    }

    ++depth_;
    const bool ok = eval_body(body, out);
    --depth_;

    // Bindings are restored on the error path too, or a failed call would leave
    // the caller's variables clobbered.
    unwind_to(mark);
    return ok;
}

bool Interp::eval_body(const Value& body, Value& out) {
    out = make_nil();
    for (Value rest = body; is_cons(rest); rest = cdr(rest)) {
        if (!eval(car(rest), out)) return false;
    }
    return true;
}

// --- special forms ----------------------------------------------------------

bool Interp::eval_special(Special which, const Value& args, Value& out) {
    switch (which) {
        case Special::Quote:
            out = car(args);
            return true;

        case Special::If: {
            Value test;
            if (!eval(car(args), test)) return false;
            if (is_truthy(test)) return eval(car(cdr(args)), out);
            // A missing else branch yields nil, not an error.
            const Value else_form = cdr(cdr(args));
            if (!is_cons(else_form)) {
                out = make_nil();
                return true;
            }
            return eval(car(else_form), out);
        }

        case Special::Cond: {
            for (Value clause = args; is_cons(clause); clause = cdr(clause)) {
                const Value c = car(clause);
                if (!is_cons(c)) return fail(EvalStatus::BadSyntax, "cond clause is not a list");
                Value test;
                if (!eval(car(c), test)) return false;
                if (!is_truthy(test)) continue;
                // A clause with no body returns the test value itself.
                if (!is_cons(cdr(c))) {
                    out = test;
                    return true;
                }
                return eval_body(cdr(c), out);
            }
            out = make_nil();
            return true;
        }

        case Special::While: {
            out = make_nil();
            for (;;) {
                Value test;
                if (!eval(car(args), test)) return false;
                if (!is_truthy(test)) return true;
                if (!eval_body(cdr(args), out)) return false;
            }
        }

        case Special::Repeat: {
            Value count;
            if (!eval(car(args), count)) return false;
            if (count.type != Type::Int) {
                return fail(EvalStatus::BadArgumentType, "repeat count must be an integer");
            }
            out = make_nil();
            for (std::int32_t n = 0; n < count.i; ++n) {
                if (!eval_body(cdr(args), out)) return false;
            }
            return true;
        }

        case Special::Progn:
            return eval_body(args, out);

        case Special::Setq: {
            out = make_nil();
            for (Value rest = args; is_cons(rest); rest = cdr(cdr(rest))) {
                const Value name = car(rest);
                if (name.type != Type::Sym) {
                    return fail(EvalStatus::BadSyntax, "setq target is not a symbol");
                }
                if (!is_cons(cdr(rest))) {
                    return fail(EvalStatus::BadSyntax, "setq has no value for " +
                                                           std::string(name.sym->name));
                }
                if (!eval(car(cdr(rest)), out)) return false;
                name.sym->value = out;
                name.sym->bound = true;
            }
            return true;
        }

        case Special::Defun: {
            const Value name = car(args);
            if (name.type != Type::Sym) {
                return fail(EvalStatus::BadSyntax, "defun name is not a symbol");
            }
            const Value params = car(cdr(args));
            if (!is_list(params)) {
                return fail(EvalStatus::BadSyntax, "defun parameter list is not a list");
            }
            // Stored in the same shape a literal lambda has, so one apply path
            // serves both.
            name.sym->func = ctx_.cons(make_sym(ctx_.intern("LAMBDA")), cdr(args));
            name.sym->has_func = true;
            out = make_sym(name.sym);  // R12 returns the function's name
            return true;
        }

        case Special::Lambda:
            // Evaluates to itself: (LAMBDA params . body).
            out = ctx_.cons(make_sym(ctx_.intern("LAMBDA")), args);
            return true;

        case Special::Foreach: {
            const Value name = car(args);
            if (name.type != Type::Sym) {
                return fail(EvalStatus::BadSyntax, "foreach variable is not a symbol");
            }
            Value seq;
            if (!eval(car(cdr(args)), seq)) return false;

            const Value body = cdr(cdr(args));
            const std::size_t mark = bindings_.size();
            bind(name.sym, make_nil());

            out = make_nil();
            bool ok = true;
            for (Value rest = seq; is_cons(rest) && ok; rest = cdr(rest)) {
                name.sym->value = car(rest);
                ok = eval_body(body, out);
            }
            unwind_to(mark);
            return ok;
        }

        case Special::And: {
            out = make_true();
            for (Value rest = args; is_cons(rest); rest = cdr(rest)) {
                if (!eval(car(rest), out)) return false;
                if (!is_truthy(out)) return true;  // short-circuits
            }
            return true;
        }

        case Special::Or: {
            out = make_nil();
            for (Value rest = args; is_cons(rest); rest = cdr(rest)) {
                if (!eval(car(rest), out)) return false;
                if (is_truthy(out)) return true;
            }
            return true;
        }
    }
    return fail(EvalStatus::BadSyntax);
}

// --- source evaluation ------------------------------------------------------

bool Interp::eval_string(std::string_view source, Value& out) {
    out = make_nil();
    Reader reader(ctx_, source);
    Value form;
    while (reader.read(form)) {
        if (!eval(form, out)) return false;
    }
    const ReadError& re = reader.error();
    if (re.status != ReadStatus::Ok && re.status != ReadStatus::EndOfInput) {
        return fail(EvalStatus::ReadFailed,
                    std::string(read_status_message(re.status)) + " at line " +
                        std::to_string(re.line) + ", column " + std::to_string(re.column));
    }
    return true;
}

}  // namespace noto::lisp
