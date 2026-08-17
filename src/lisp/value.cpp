// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/lisp/value.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace ncad::lisp {
namespace {

char upcase(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

// Shortest representation that still round-trips exactly, so printed output is
// readable without silently losing precision.
std::string format_real(double x) {
    char buf[64];
    for (int precision = 15; precision <= 17; ++precision) {
        std::snprintf(buf, sizeof(buf), "%.*g", precision, x);
        if (std::strtod(buf, nullptr) == x) break;
    }
    std::string s(buf);
    // AutoLISP always shows reals as reals.
    if (s.find_first_of(".eEnN") == std::string::npos) s += ".0";
    return s;
}

// How deep printing may nest before it stops descending.
//
// print_list and print_value recurse into each other once per level of CAR
// nesting, so a 200k-deep list -- which is built ITERATIVELY, at eval depth 2,
// so Interp::max_depth_ never sees it -- blew the C stack when anything tried
// to print it. That includes the REPL echoing a result and, worse, prin1()
// formatting a value into an ERROR MESSAGE, so a malformed call crashed instead
// of reporting.
//
// Deeper than the reader's limit on purpose: a structure this program built for
// itself is not bounded by what its reader would accept.
constexpr std::size_t kMaxPrintDepth = 8000;

void print_value(const Value& v, bool readable, std::string& out, std::size_t depth);

void print_list(const Value& v, bool readable, std::string& out, std::size_t depth) {
    out += '(';
    Value cur = v;
    bool first = true;
    while (is_cons(cur)) {
        if (!first) out += ' ';
        print_value(cur.cons->car, readable, out, depth + 1);
        first = false;
        cur = cur.cons->cdr;
    }
    // A dotted tail: (a . b), the shape entity data arrives in.
    if (!is_nil(cur)) {
        out += " . ";
        print_value(cur, readable, out, depth + 1);
    }
    out += ')';
}

void print_string(const Str* s, bool readable, std::string& out) {
    if (!readable) {
        out.append(s->data, s->len);
        return;
    }
    out += '"';
    for (std::uint32_t i = 0; i < s->len; ++i) {
        const char c = s->data[i];
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    out += '"';
}

void print_value(const Value& v, bool readable, std::string& out, std::size_t depth) {
    // Too deep to descend. An ellipsis rather than an error: printing is what
    // REPORTS errors, so it must always produce something.
    if (depth >= kMaxPrintDepth) {
        out += "...";
        return;
    }
    char buf[64];
    switch (v.type) {
        case Type::Nil: out += "nil"; break;
        case Type::True: out += "T"; break;
        case Type::Int:
            std::snprintf(buf, sizeof(buf), "%d", v.i);
            out += buf;
            break;
        case Type::Real: out += format_real(v.d); break;
        case Type::Sym: out.append(v.sym->name.data(), v.sym->name.size()); break;
        case Type::Str: print_string(v.str, readable, out); break;
        case Type::Cons: print_list(v, readable, out, depth); break;
        case Type::Subr:
            std::snprintf(buf, sizeof(buf), "<Subr: %d>", v.subr);
            out += buf;
            break;
        case Type::Ename:
            std::snprintf(buf, sizeof(buf), "<Entity name: %llx>",
                          static_cast<unsigned long long>(v.ename));
            out += buf;
            break;
        case Type::File:
            std::snprintf(buf, sizeof(buf), "<File: %d>", v.file);
            out += buf;
            break;
        case Type::Sset:
            std::snprintf(buf, sizeof(buf), "<Selection set: %d>", v.sset);
            out += buf;
            break;
    }
}

}  // namespace

const char* type_name(Type t) {
    switch (t) {
        case Type::Nil: return "NIL";
        case Type::True: return "T";
        case Type::Int: return "INT";
        case Type::Real: return "REAL";
        case Type::Sym: return "SYM";
        case Type::Str: return "STR";
        case Type::Cons: return "LIST";
        case Type::Subr: return "SUBR";
        case Type::Ename: return "ENAME";
        case Type::File: return "FILE";
        case Type::Sset: return "PICKSET";
    }
    return "?";
}

std::size_t list_length(const Value& v) {
    std::size_t n = 0;
    Value cur = v;
    while (is_cons(cur)) {
        ++n;
        cur = cur.cons->cdr;
    }
    return n;
}

bool equal(const Value& a, const Value& b) {
    // Numbers compare by value across Int and Real, matching AutoLISP's `equal`.
    if (is_number(a) && is_number(b)) {
        if (a.type == Type::Int && b.type == Type::Int) return a.i == b.i;
        return as_double(a) == as_double(b);
    }
    if (a.type != b.type) return false;

    switch (a.type) {
        case Type::Nil:
        case Type::True: return true;
        case Type::Sym: return a.sym == b.sym;  // interned, so pointer compare
        case Type::Str: return a.str->view() == b.str->view();
        case Type::Ename: return a.ename == b.ename;
        case Type::Subr: return a.subr == b.subr;
        case Type::File: return a.file == b.file;
        case Type::Sset: return a.sset == b.sset;
        case Type::Cons:
            return equal(a.cons->car, b.cons->car) && equal(a.cons->cdr, b.cons->cdr);
        default: return false;
    }
}

Context::Context() { install_constants(); }

void Context::install_constants() {
    nil_ = intern("NIL");
    nil_->value = make_nil();
    nil_->bound = true;

    t_ = intern("T");
    t_->value = make_true();
    t_->bound = true;

    quote_ = intern("QUOTE");
}

Symbol* Context::intern(std::string_view name) {
    // Upcase into a stack buffer for the lookup so the common case of an
    // already-interned symbol allocates nothing.
    std::string key;
    key.reserve(name.size());
    for (const char c : name) key += upcase(c);

    const auto it = symbols_.find(key);
    if (it != symbols_.end()) return it->second;

    char* stored = static_cast<char*>(arena_.copy(key.data(), key.size() + 1, 1));
    stored[key.size()] = '\0';

    Symbol* sym = arena_.make<Symbol>();
    sym->name = std::string_view(stored, key.size());
    sym->value = make_nil();
    sym->bound = false;
    sym->subr = -1;

    symbols_.emplace(sym->name, sym);
    return sym;
}

Str* Context::new_string(std::string_view text) {
    char* data = static_cast<char*>(arena_.alloc(text.size() + 1, 1));
    std::memcpy(data, text.data(), text.size());
    data[text.size()] = '\0';

    Str* s = arena_.make<Str>();
    s->len = static_cast<std::uint32_t>(text.size());
    s->data = data;
    return s;
}

Value Context::cons(const Value& a, const Value& d) {
    Cons* c = arena_.make<Cons>();
    c->car = a;
    c->cdr = d;
    return make_cons(c);
}

Value Context::list(const Value* items, std::size_t count) {
    Value result = make_nil();
    for (std::size_t i = count; i > 0; --i) result = cons(items[i - 1], result);
    return result;
}

void Context::reset() {
    symbols_.clear();
    arena_.reset();
    install_constants();
}

std::string prin1(const Value& v) {
    std::string out;
    print_value(v, true, out, 0);
    return out;
}

std::string princ(const Value& v) {
    std::string out;
    print_value(v, false, out, 0);
    return out;
}

}  // namespace ncad::lisp
