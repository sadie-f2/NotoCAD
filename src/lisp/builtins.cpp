// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The AutoLISP builtin function table.
//
// R12 arithmetic is type-sensitive: an operation on integers yields an integer,
// and one real argument makes the whole result real. (/ 7 2) is 3, not 3.5.
// That is not a rounding artifact to be smoothed over -- LISP files depend on it.
#include "noto/lisp/eval.hpp"

#include "entity_subrs.hpp"
#include "command_subr.hpp"
#include "file_subrs.hpp"
#include "sset_subrs.hpp"
#include "sysvar_subrs.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace noto::lisp {
namespace {

// --- argument helpers -------------------------------------------------------

bool bad_type(Interp& in, const char* who, const Value& v) {
    return in.fail(EvalStatus::BadArgumentType,
                   std::string(who) + ": " + type_name(v.type) + " " + prin1(v));
}

// True when every argument is a number; reports the first that is not.
bool check_numbers(Interp& in, const char* who, const Value* a, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (!is_number(a[i])) return bad_type(in, who, a[i]);
    }
    return true;
}

bool all_int(const Value* a, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (a[i].type != Type::Int) return false;
    }
    return true;
}

// Accumulation happens in 64 bits and is narrowed on the way out, so overflow
// wraps predictably instead of being undefined.
Value make_int_wrapped(std::int64_t v) {
    return make_int(static_cast<std::int32_t>(v));
}

bool want_string(Interp& in, const char* who, const Value& v) {
    if (v.type == Type::Str) return true;
    return bad_type(in, who, v);
}

// --- arithmetic -------------------------------------------------------------

bool subr_add(Interp& in, const Value* a, std::size_t n, Value& out) {
    if (!check_numbers(in, "+", a, n)) return false;
    if (all_int(a, n)) {
        std::int64_t sum = 0;
        for (std::size_t i = 0; i < n; ++i) sum += a[i].i;
        out = make_int_wrapped(sum);
    } else {
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i) sum += as_double(a[i]);
        out = make_real(sum);
    }
    return true;
}

bool subr_sub(Interp& in, const Value* a, std::size_t n, Value& out) {
    if (!check_numbers(in, "-", a, n)) return false;
    const bool ints = all_int(a, n);
    // (- x) negates; (- a b c) subtracts the rest from the first.
    if (n == 1) {
        out = ints ? make_int_wrapped(-static_cast<std::int64_t>(a[0].i))
                   : make_real(-as_double(a[0]));
        return true;
    }
    if (ints) {
        std::int64_t acc = a[0].i;
        for (std::size_t i = 1; i < n; ++i) acc -= a[i].i;
        out = make_int_wrapped(acc);
    } else {
        double acc = as_double(a[0]);
        for (std::size_t i = 1; i < n; ++i) acc -= as_double(a[i]);
        out = make_real(acc);
    }
    return true;
}

bool subr_mul(Interp& in, const Value* a, std::size_t n, Value& out) {
    if (!check_numbers(in, "*", a, n)) return false;
    if (all_int(a, n)) {
        std::int64_t acc = 1;
        for (std::size_t i = 0; i < n; ++i) acc *= a[i].i;
        out = make_int_wrapped(acc);
    } else {
        double acc = 1.0;
        for (std::size_t i = 0; i < n; ++i) acc *= as_double(a[i]);
        out = make_real(acc);
    }
    return true;
}

bool subr_div(Interp& in, const Value* a, std::size_t n, Value& out) {
    if (!check_numbers(in, "/", a, n)) return false;
    if (n == 1) {
        out = a[0];
        return true;
    }
    if (all_int(a, n)) {
        std::int64_t acc = a[0].i;
        for (std::size_t i = 1; i < n; ++i) {
            if (a[i].i == 0) return in.fail(EvalStatus::DivideByZero);
            acc /= a[i].i;
        }
        out = make_int_wrapped(acc);
    } else {
        double acc = as_double(a[0]);
        for (std::size_t i = 1; i < n; ++i) {
            const double d = as_double(a[i]);
            if (d == 0.0) return in.fail(EvalStatus::DivideByZero);
            acc /= d;
        }
        out = make_real(acc);
    }
    return true;
}

bool subr_rem(Interp& in, const Value* a, std::size_t n, Value& out) {
    if (!check_numbers(in, "rem", a, n)) return false;
    if (all_int(a, n)) {
        std::int64_t acc = a[0].i;
        for (std::size_t i = 1; i < n; ++i) {
            if (a[i].i == 0) return in.fail(EvalStatus::DivideByZero);
            acc %= a[i].i;
        }
        out = make_int_wrapped(acc);
    } else {
        double acc = as_double(a[0]);
        for (std::size_t i = 1; i < n; ++i) {
            const double d = as_double(a[i]);
            if (d == 0.0) return in.fail(EvalStatus::DivideByZero);
            acc = std::fmod(acc, d);
        }
        out = make_real(acc);
    }
    return true;
}

bool subr_add1(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!check_numbers(in, "1+", a, 1)) return false;
    out = a[0].type == Type::Int ? make_int_wrapped(static_cast<std::int64_t>(a[0].i) + 1)
                                 : make_real(a[0].d + 1.0);
    return true;
}

bool subr_sub1(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!check_numbers(in, "1-", a, 1)) return false;
    out = a[0].type == Type::Int ? make_int_wrapped(static_cast<std::int64_t>(a[0].i) - 1)
                                 : make_real(a[0].d - 1.0);
    return true;
}

bool subr_abs(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!check_numbers(in, "abs", a, 1)) return false;
    out = a[0].type == Type::Int ? make_int_wrapped(std::llabs(a[0].i))
                                 : make_real(std::fabs(a[0].d));
    return true;
}

bool subr_min(Interp& in, const Value* a, std::size_t n, Value& out) {
    if (!check_numbers(in, "min", a, n)) return false;
    out = a[0];
    for (std::size_t i = 1; i < n; ++i) {
        if (as_double(a[i]) < as_double(out)) out = a[i];
    }
    return true;
}

bool subr_max(Interp& in, const Value* a, std::size_t n, Value& out) {
    if (!check_numbers(in, "max", a, n)) return false;
    out = a[0];
    for (std::size_t i = 1; i < n; ++i) {
        if (as_double(a[i]) > as_double(out)) out = a[i];
    }
    return true;
}

bool subr_sqrt(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!check_numbers(in, "sqrt", a, 1)) return false;
    out = make_real(std::sqrt(as_double(a[0])));
    return true;
}

bool subr_sin(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!check_numbers(in, "sin", a, 1)) return false;
    out = make_real(std::sin(as_double(a[0])));
    return true;
}

bool subr_cos(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!check_numbers(in, "cos", a, 1)) return false;
    out = make_real(std::cos(as_double(a[0])));
    return true;
}

// (atan x) or (atan y x), the second form being the one that matters for angles.
bool subr_atan(Interp& in, const Value* a, std::size_t n, Value& out) {
    if (!check_numbers(in, "atan", a, n)) return false;
    out = n == 1 ? make_real(std::atan(as_double(a[0])))
                 : make_real(std::atan2(as_double(a[0]), as_double(a[1])));
    return true;
}

bool subr_expt(Interp& in, const Value* a, std::size_t n, Value& out) {
    if (!check_numbers(in, "expt", a, n)) return false;
    if (all_int(a, n) && a[1].i >= 0) {
        std::int64_t acc = 1;
        for (std::int32_t i = 0; i < a[1].i; ++i) acc *= a[0].i;
        out = make_int_wrapped(acc);
    } else {
        out = make_real(std::pow(as_double(a[0]), as_double(a[1])));
    }
    return true;
}

bool subr_exp(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!check_numbers(in, "exp", a, 1)) return false;
    out = make_real(std::exp(as_double(a[0])));
    return true;
}

bool subr_log(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!check_numbers(in, "log", a, 1)) return false;
    out = make_real(std::log(as_double(a[0])));
    return true;
}

bool subr_fix(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!check_numbers(in, "fix", a, 1)) return false;
    out = make_int_wrapped(static_cast<std::int64_t>(as_double(a[0])));
    return true;
}

bool subr_float(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!check_numbers(in, "float", a, 1)) return false;
    out = make_real(as_double(a[0]));
    return true;
}

bool subr_gcd(Interp& in, const Value* a, std::size_t n, Value& out) {
    if (!check_numbers(in, "gcd", a, n)) return false;
    if (!all_int(a, n)) return bad_type(in, "gcd", a[0]);
    std::int64_t x = std::llabs(a[0].i);
    std::int64_t y = std::llabs(a[1].i);
    while (y != 0) {
        const std::int64_t t = x % y;
        x = y;
        y = t;
    }
    out = make_int_wrapped(x);
    return true;
}

// --- comparison -------------------------------------------------------------

// AutoLISP's comparisons chain: (< 1 2 3) is true.
enum class Cmp { Eq, Ne, Lt, Gt, Le, Ge };

bool compare_chain(Interp& in, const char* who, Cmp op, const Value* a, std::size_t n,
                   Value& out) {
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const Value& x = a[i];
        const Value& y = a[i + 1];
        bool ok;
        if (x.type == Type::Str && y.type == Type::Str) {
            const int c = x.str->view().compare(y.str->view());
            switch (op) {
                case Cmp::Eq: ok = c == 0; break;
                case Cmp::Ne: ok = c != 0; break;
                case Cmp::Lt: ok = c < 0; break;
                case Cmp::Gt: ok = c > 0; break;
                case Cmp::Le: ok = c <= 0; break;
                case Cmp::Ge: ok = c >= 0; break;
            }
        } else if (is_number(x) && is_number(y)) {
            const double dx = as_double(x);
            const double dy = as_double(y);
            switch (op) {
                case Cmp::Eq: ok = dx == dy; break;
                case Cmp::Ne: ok = dx != dy; break;
                case Cmp::Lt: ok = dx < dy; break;
                case Cmp::Gt: ok = dx > dy; break;
                case Cmp::Le: ok = dx <= dy; break;
                case Cmp::Ge: ok = dx >= dy; break;
            }
        } else if (op == Cmp::Eq || op == Cmp::Ne) {
            // = falls back to structural equality for non-numbers.
            const bool same = equal(x, y);
            ok = (op == Cmp::Eq) ? same : !same;
        } else {
            return bad_type(in, who, is_number(x) ? y : x);
        }
        if (!ok) {
            out = make_nil();
            return true;
        }
    }
    out = make_true();
    return true;
}

bool subr_num_eq(Interp& in, const Value* a, std::size_t n, Value& out) {
    return compare_chain(in, "=", Cmp::Eq, a, n, out);
}
bool subr_num_ne(Interp& in, const Value* a, std::size_t n, Value& out) {
    return compare_chain(in, "/=", Cmp::Ne, a, n, out);
}
bool subr_lt(Interp& in, const Value* a, std::size_t n, Value& out) {
    return compare_chain(in, "<", Cmp::Lt, a, n, out);
}
bool subr_gt(Interp& in, const Value* a, std::size_t n, Value& out) {
    return compare_chain(in, ">", Cmp::Gt, a, n, out);
}
bool subr_le(Interp& in, const Value* a, std::size_t n, Value& out) {
    return compare_chain(in, "<=", Cmp::Le, a, n, out);
}
bool subr_ge(Interp& in, const Value* a, std::size_t n, Value& out) {
    return compare_chain(in, ">=", Cmp::Ge, a, n, out);
}

bool subr_eq(Interp&, const Value* a, std::size_t, Value& out) {
    // eq is identity: the same cons cell, not merely equal contents.
    const Value& x = a[0];
    const Value& y = a[1];
    bool same;
    if (x.type != y.type) {
        same = false;
    } else if (x.type == Type::Cons) {
        same = x.cons == y.cons;
    } else if (x.type == Type::Str) {
        same = x.str == y.str;
    } else {
        same = equal(x, y);
    }
    out = same ? make_true() : make_nil();
    return true;
}

bool subr_equal(Interp&, const Value* a, std::size_t, Value& out) {
    out = equal(a[0], a[1]) ? make_true() : make_nil();
    return true;
}

// --- predicates -------------------------------------------------------------

bool subr_null(Interp&, const Value* a, std::size_t, Value& out) {
    out = is_nil(a[0]) ? make_true() : make_nil();
    return true;
}

bool subr_not(Interp&, const Value* a, std::size_t, Value& out) {
    out = is_truthy(a[0]) ? make_nil() : make_true();
    return true;
}

bool subr_atom(Interp&, const Value* a, std::size_t, Value& out) {
    out = is_cons(a[0]) ? make_nil() : make_true();
    return true;
}

bool subr_listp(Interp&, const Value* a, std::size_t, Value& out) {
    out = is_list(a[0]) ? make_true() : make_nil();
    return true;
}

bool subr_numberp(Interp&, const Value* a, std::size_t, Value& out) {
    out = is_number(a[0]) ? make_true() : make_nil();
    return true;
}

bool subr_zerop(Interp&, const Value* a, std::size_t, Value& out) {
    out = (is_number(a[0]) && as_double(a[0]) == 0.0) ? make_true() : make_nil();
    return true;
}

bool subr_minusp(Interp&, const Value* a, std::size_t, Value& out) {
    out = (is_number(a[0]) && as_double(a[0]) < 0.0) ? make_true() : make_nil();
    return true;
}

bool subr_boundp(Interp& in, const Value* a, std::size_t, Value& out) {
    if (a[0].type != Type::Sym) return bad_type(in, "boundp", a[0]);
    out = a[0].sym->bound ? make_true() : make_nil();
    return true;
}

bool subr_type(Interp& in, const Value* a, std::size_t, Value& out) {
    if (is_nil(a[0])) {
        out = make_nil();
        return true;
    }
    out = make_sym(in.ctx().intern(type_name(a[0].type)));
    return true;
}

// --- list operations --------------------------------------------------------

bool subr_car(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!is_list(a[0])) return bad_type(in, "car", a[0]);
    out = car(a[0]);
    return true;
}

bool subr_cdr(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!is_list(a[0])) return bad_type(in, "cdr", a[0]);
    out = cdr(a[0]);
    return true;
}

// Walks a c[ad]+r path right to left, so "adr" means (car (cdr ...)).
bool walk_path(const Value& v, const char* path, Value& out) {
    out = v;
    for (const char* p = path + std::strlen(path); p != path;) {
        --p;
        if (!is_list(out)) return false;
        out = (*p == 'a') ? car(out) : cdr(out);
    }
    return true;
}

template <char... Path>
bool subr_cxr(Interp& in, const Value* a, std::size_t, Value& out) {
    static constexpr char path[] = {Path..., '\0'};
    if (!walk_path(a[0], path, out)) return bad_type(in, path, a[0]);
    return true;
}

bool subr_cons(Interp& in, const Value* a, std::size_t, Value& out) {
    out = in.ctx().cons(a[0], a[1]);
    return true;
}

bool subr_list(Interp& in, const Value* a, std::size_t n, Value& out) {
    out = in.ctx().list(a, n);
    return true;
}

bool subr_length(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!is_list(a[0])) return bad_type(in, "length", a[0]);
    out = make_int_wrapped(static_cast<std::int64_t>(list_length(a[0])));
    return true;
}

bool subr_nth(Interp& in, const Value* a, std::size_t, Value& out) {
    if (a[0].type != Type::Int) return bad_type(in, "nth", a[0]);
    if (!is_list(a[1])) return bad_type(in, "nth", a[1]);
    Value cur = a[1];
    for (std::int32_t i = 0; i < a[0].i && is_cons(cur); ++i) cur = cdr(cur);
    out = car(cur);
    return true;
}

bool subr_last(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!is_list(a[0])) return bad_type(in, "last", a[0]);
    out = make_nil();
    for (Value cur = a[0]; is_cons(cur); cur = cdr(cur)) out = car(cur);
    return true;
}

bool subr_reverse(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!is_list(a[0])) return bad_type(in, "reverse", a[0]);
    out = make_nil();
    for (Value cur = a[0]; is_cons(cur); cur = cdr(cur)) out = in.ctx().cons(car(cur), out);
    return true;
}

bool subr_append(Interp& in, const Value* a, std::size_t n, Value& out) {
    // Copies every list but the last, which is shared -- standard behaviour and
    // the reason append is not free.
    out = n == 0 ? make_nil() : a[n - 1];
    for (std::size_t i = n - 1; i > 0; --i) {
        const Value& lst = a[i - 1];
        if (!is_list(lst)) return bad_type(in, "append", lst);
        // Collect then prepend in reverse, to avoid recursion on long lists.
        Value rev = make_nil();
        for (Value cur = lst; is_cons(cur); cur = cdr(cur)) rev = in.ctx().cons(car(cur), rev);
        for (Value cur = rev; is_cons(cur); cur = cdr(cur)) out = in.ctx().cons(car(cur), out);
    }
    return true;
}

bool subr_assoc(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!is_list(a[1])) return bad_type(in, "assoc", a[1]);
    for (Value cur = a[1]; is_cons(cur); cur = cdr(cur)) {
        const Value pair = car(cur);
        if (is_cons(pair) && equal(car(pair), a[0])) {
            out = pair;
            return true;
        }
    }
    out = make_nil();
    return true;
}

bool subr_member(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!is_list(a[1])) return bad_type(in, "member", a[1]);
    for (Value cur = a[1]; is_cons(cur); cur = cdr(cur)) {
        if (equal(car(cur), a[0])) {
            out = cur;  // the tail starting at the match, as AutoLISP has it
            return true;
        }
    }
    out = make_nil();
    return true;
}

bool subr_subst(Interp& in, const Value* a, std::size_t, Value& out) {
    // (subst new old lst)
    if (!is_list(a[2])) return bad_type(in, "subst", a[2]);
    Value rev = make_nil();
    for (Value cur = a[2]; is_cons(cur); cur = cdr(cur)) {
        const Value item = car(cur);
        rev = in.ctx().cons(equal(item, a[1]) ? a[0] : item, rev);
    }
    out = make_nil();
    for (Value cur = rev; is_cons(cur); cur = cdr(cur)) out = in.ctx().cons(car(cur), out);
    return true;
}

// --- functional -------------------------------------------------------------

bool subr_apply(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!is_list(a[1])) return bad_type(in, "apply", a[1]);
    std::vector<Value> args;
    args.reserve(list_length(a[1]));
    for (Value cur = a[1]; is_cons(cur); cur = cdr(cur)) args.push_back(car(cur));
    return in.apply(a[0], args.data(), args.size(), out);
}

bool subr_mapcar(Interp& in, const Value* a, std::size_t n, Value& out) {
    const std::size_t nlists = n - 1;
    std::vector<Value> cursors(a + 1, a + n);
    for (const Value& c : cursors) {
        if (!is_list(c)) return bad_type(in, "mapcar", c);
    }

    std::vector<Value> results;
    std::vector<Value> args(nlists);
    for (;;) {
        for (std::size_t i = 0; i < nlists; ++i) {
            if (!is_cons(cursors[i])) {
                // Stops at the shortest list.
                out = in.ctx().list(results.data(), results.size());
                return true;
            }
            args[i] = car(cursors[i]);
        }
        Value r;
        if (!in.apply(a[0], args.data(), nlists, r)) return false;
        results.push_back(r);
        for (std::size_t i = 0; i < nlists; ++i) cursors[i] = cdr(cursors[i]);
    }
}

bool subr_set(Interp& in, const Value* a, std::size_t, Value& out) {
    if (a[0].type != Type::Sym) return bad_type(in, "set", a[0]);
    a[0].sym->value = a[1];
    a[0].sym->bound = true;
    out = a[1];
    return true;
}

// --- strings ----------------------------------------------------------------

bool subr_strcat(Interp& in, const Value* a, std::size_t n, Value& out) {
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        if (!want_string(in, "strcat", a[i])) return false;
        s += a[i].str->view();
    }
    out = make_str(in.ctx().new_string(s));
    return true;
}

bool subr_strlen(Interp& in, const Value* a, std::size_t n, Value& out) {
    std::int64_t total = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (!want_string(in, "strlen", a[i])) return false;
        total += a[i].str->len;
    }
    out = make_int_wrapped(total);
    return true;
}

// (substr s start [len]) -- start is 1-based, as R12 has it.
bool subr_substr(Interp& in, const Value* a, std::size_t n, Value& out) {
    if (!want_string(in, "substr", a[0])) return false;
    if (a[1].type != Type::Int) return bad_type(in, "substr", a[1]);
    if (n > 2 && a[2].type != Type::Int) return bad_type(in, "substr", a[2]);

    const std::string_view s = a[0].str->view();
    if (a[1].i < 1) return in.fail(EvalStatus::BadArgumentType, "substr: start must be >= 1");

    const std::size_t start = static_cast<std::size_t>(a[1].i) - 1;
    if (start >= s.size()) {
        out = make_str(in.ctx().new_string(""));
        return true;
    }
    std::size_t len = s.size() - start;
    if (n > 2) {
        const std::size_t want = a[2].i < 0 ? 0 : static_cast<std::size_t>(a[2].i);
        if (want < len) len = want;
    }
    out = make_str(in.ctx().new_string(s.substr(start, len)));
    return true;
}

bool subr_strcase(Interp& in, const Value* a, std::size_t n, Value& out) {
    if (!want_string(in, "strcase", a[0])) return false;
    const bool lower = n > 1 && is_truthy(a[1]);
    std::string s(a[0].str->view());
    for (char& c : s) {
        if (lower && c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (!lower && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    out = make_str(in.ctx().new_string(s));
    return true;
}

bool subr_itoa(Interp& in, const Value* a, std::size_t, Value& out) {
    if (a[0].type != Type::Int) return bad_type(in, "itoa", a[0]);
    out = make_str(in.ctx().new_string(std::to_string(a[0].i)));
    return true;
}

bool subr_atoi(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!want_string(in, "atoi", a[0])) return false;
    out = make_int_wrapped(std::strtol(a[0].str->data, nullptr, 10));
    return true;
}

bool subr_atof(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!want_string(in, "atof", a[0])) return false;
    out = make_real(std::strtod(a[0].str->data, nullptr));
    return true;
}

// (rtos x [mode [precision]]). Only decimal mode is implemented; the other R12
// modes are tied to system variables that do not exist yet.
bool subr_rtos(Interp& in, const Value* a, std::size_t n, Value& out) {
    if (!check_numbers(in, "rtos", a, 1)) return false;
    if (n > 1 && a[1].type != Type::Int) return bad_type(in, "rtos", a[1]);
    if (n > 2 && a[2].type != Type::Int) return bad_type(in, "rtos", a[2]);

    int precision = n > 2 ? a[2].i : 4;
    if (precision < 0) precision = 0;
    if (precision > 16) precision = 16;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, as_double(a[0]));
    out = make_str(in.ctx().new_string(buf));
    return true;
}

bool subr_chr(Interp& in, const Value* a, std::size_t, Value& out) {
    if (a[0].type != Type::Int) return bad_type(in, "chr", a[0]);
    const char c = static_cast<char>(a[0].i);
    out = make_str(in.ctx().new_string(std::string_view(&c, 1)));
    return true;
}

bool subr_ascii(Interp& in, const Value* a, std::size_t, Value& out) {
    if (!want_string(in, "ascii", a[0])) return false;
    out = make_int_wrapped(a[0].str->len == 0 ? 0
                                              : static_cast<unsigned char>(a[0].str->data[0]));
    return true;
}

// --- output -----------------------------------------------------------------

bool subr_princ(Interp& in, const Value* a, std::size_t n, Value& out) {
    out = n == 0 ? make_nil() : a[0];
    if (n > 0) in.output() << princ(a[0]);
    return true;
}

bool subr_prin1(Interp& in, const Value* a, std::size_t n, Value& out) {
    out = n == 0 ? make_nil() : a[0];
    if (n > 0) in.output() << prin1(a[0]);
    return true;
}

bool subr_print(Interp& in, const Value* a, std::size_t n, Value& out) {
    out = n == 0 ? make_nil() : a[0];
    in.output() << "\n";
    if (n > 0) in.output() << prin1(a[0]) << " ";
    return true;
}

bool subr_terpri(Interp& in, const Value*, std::size_t, Value& out) {
    in.output() << "\n";
    out = make_nil();
    return true;
}

// --- the table --------------------------------------------------------------

constexpr SubrDef kSubrs[] = {
    // arithmetic
    {"+", subr_add, 0, kNoMax},
    {"-", subr_sub, 1, kNoMax},
    {"*", subr_mul, 0, kNoMax},
    {"/", subr_div, 1, kNoMax},
    {"REM", subr_rem, 2, kNoMax},
    {"1+", subr_add1, 1, 1},
    {"1-", subr_sub1, 1, 1},
    {"ABS", subr_abs, 1, 1},
    {"MIN", subr_min, 1, kNoMax},
    {"MAX", subr_max, 1, kNoMax},
    {"SQRT", subr_sqrt, 1, 1},
    {"SIN", subr_sin, 1, 1},
    {"COS", subr_cos, 1, 1},
    {"ATAN", subr_atan, 1, 2},
    {"EXPT", subr_expt, 2, 2},
    {"EXP", subr_exp, 1, 1},
    {"LOG", subr_log, 1, 1},
    {"FIX", subr_fix, 1, 1},
    {"FLOAT", subr_float, 1, 1},
    {"GCD", subr_gcd, 2, 2},

    // comparison
    {"=", subr_num_eq, 1, kNoMax},
    {"/=", subr_num_ne, 1, kNoMax},
    {"<", subr_lt, 1, kNoMax},
    {">", subr_gt, 1, kNoMax},
    {"<=", subr_le, 1, kNoMax},
    {">=", subr_ge, 1, kNoMax},
    {"EQ", subr_eq, 2, 2},
    {"EQUAL", subr_equal, 2, 3},

    // predicates
    {"NULL", subr_null, 1, 1},
    {"NOT", subr_not, 1, 1},
    {"ATOM", subr_atom, 1, 1},
    {"LISTP", subr_listp, 1, 1},
    {"NUMBERP", subr_numberp, 1, 1},
    {"ZEROP", subr_zerop, 1, 1},
    {"MINUSP", subr_minusp, 1, 1},
    {"BOUNDP", subr_boundp, 1, 1},
    {"TYPE", subr_type, 1, 1},

    // lists
    {"CAR", subr_car, 1, 1},
    {"CDR", subr_cdr, 1, 1},
    {"CAAR", subr_cxr<'a', 'a'>, 1, 1},
    {"CADR", subr_cxr<'a', 'd'>, 1, 1},
    {"CDAR", subr_cxr<'d', 'a'>, 1, 1},
    {"CDDR", subr_cxr<'d', 'd'>, 1, 1},
    {"CADDR", subr_cxr<'a', 'd', 'd'>, 1, 1},
    {"CADAR", subr_cxr<'a', 'd', 'a'>, 1, 1},
    {"CDDDR", subr_cxr<'d', 'd', 'd'>, 1, 1},
    {"CONS", subr_cons, 2, 2},
    {"LIST", subr_list, 0, kNoMax},
    {"LENGTH", subr_length, 1, 1},
    {"NTH", subr_nth, 2, 2},
    {"LAST", subr_last, 1, 1},
    {"REVERSE", subr_reverse, 1, 1},
    {"APPEND", subr_append, 0, kNoMax},
    {"ASSOC", subr_assoc, 2, 2},
    {"MEMBER", subr_member, 2, 2},
    {"SUBST", subr_subst, 3, 3},

    // functional
    {"APPLY", subr_apply, 2, 2},
    {"MAPCAR", subr_mapcar, 2, kNoMax},
    {"SET", subr_set, 2, 2},

    // strings
    {"STRCAT", subr_strcat, 0, kNoMax},
    {"STRLEN", subr_strlen, 0, kNoMax},
    {"SUBSTR", subr_substr, 2, 3},
    {"STRCASE", subr_strcase, 1, 2},
    {"ITOA", subr_itoa, 1, 1},
    {"ATOI", subr_atoi, 1, 1},
    {"ATOF", subr_atof, 1, 1},
    {"RTOS", subr_rtos, 1, 3},
    {"CHR", subr_chr, 1, 1},
    {"ASCII", subr_ascii, 1, 1},

    // entity access -- implemented in entity_subrs.cpp
    {"ENTMAKE", subr_entmake, 1, 1},
    {"ENTGET", subr_entget, 1, 2},
    {"ENTMOD", subr_entmod, 1, 1},
    {"ENTDEL", subr_entdel, 1, 1},
    {"ENTLAST", subr_entlast, 0, 0},
    {"ENTNEXT", subr_entnext, 0, 1},

    // selection sets -- implemented in sset_subrs.cpp
    {"SSGET", subr_ssget, 0, kNoMax},
    {"SSADD", subr_ssadd, 0, 2},
    {"SSDEL", subr_ssdel, 2, 2},
    {"SSLENGTH", subr_sslength, 1, 1},
    {"SSNAME", subr_ssname, 2, 2},
    {"SSMEMB", subr_ssmemb, 2, 2},

    // system variables -- implemented in sysvar_subrs.cpp
    {"GETVAR", subr_getvar, 1, 1},
    {"SETVAR", subr_setvar, 2, 2},

    // drawing file I/O -- implemented in file_subrs.cpp
    {"COMMAND", subr_command, 0, kNoMax},
    {"DXFOUT", subr_dxfout, 1, 1},
    {"QUIT", subr_quit, 0, 0},
    {"EXIT", subr_quit, 0, 0},

    // output
    {"PRINC", subr_princ, 0, 2},
    {"PRIN1", subr_prin1, 0, 2},
    {"PRINT", subr_print, 0, 2},
    {"TERPRI", subr_terpri, 0, 0},
};

}  // namespace

const SubrDef* subr_table(std::size_t& count) {
    count = sizeof(kSubrs) / sizeof(kSubrs[0]);
    return kSubrs;
}

}  // namespace noto::lisp
