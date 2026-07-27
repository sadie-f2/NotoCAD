// AutoLISP value representation.
//
// A Value is 16 bytes: an 8-bit tag plus an 8-byte payload. Values are passed by
// copy everywhere; only the things they point at (cons cells, strings, symbols)
// live in the arena.
//
// The R12 dialect's type set, and no more: no vectors, no hash tables, no CLOS,
// no Visual LISP object model.
#pragma once

#include "noto/entity.hpp"
#include "noto/lisp/arena.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace noto::lisp {

enum class Type : std::uint8_t {
    Nil,      // also the empty list
    True,     // T
    Int,      // 32-bit, as R12 AutoLISP
    Real,     // double
    Sym,
    Str,
    Cons,
    Subr,     // built-in function
    Ename,    // entity name: a database handle
    File,     // open file descriptor from (open ...)
};

const char* type_name(Type t);

struct Cons;
struct Symbol;
struct Str;
class Context;

struct Value {
    Type type{Type::Nil};
    union {
        std::int32_t i;
        double d;
        Symbol* sym;
        Str* str;
        Cons* cons;
        Handle ename;
        std::int32_t subr;   // index into the builtin table
        std::int32_t file;   // index into the open-file table
    };

    constexpr Value() : i(0) {}
};

static_assert(sizeof(Value) <= 16, "Value must stay compact");

struct Cons {
    Value car;
    Value cdr;
};

// Arena-allocated, length-prefixed, NUL-terminated so it can be handed to C.
struct Str {
    std::uint32_t len;
    char* data;
    std::string_view view() const { return {data, len}; }
};

struct Symbol {
    std::string_view name;  // points into the arena
    Value value;            // global binding
    bool bound{false};
    std::int32_t subr{-1};  // >= 0 if this symbol names a builtin
};

// --- constructors -----------------------------------------------------------

inline Value make_nil() { return Value{}; }

inline Value make_true() {
    Value v;
    v.type = Type::True;
    return v;
}

inline Value make_int(std::int32_t n) {
    Value v;
    v.type = Type::Int;
    v.i = n;
    return v;
}

inline Value make_real(double x) {
    Value v;
    v.type = Type::Real;
    v.d = x;
    return v;
}

inline Value make_sym(Symbol* s) {
    Value v;
    v.type = Type::Sym;
    v.sym = s;
    return v;
}

inline Value make_str(Str* s) {
    Value v;
    v.type = Type::Str;
    v.str = s;
    return v;
}

inline Value make_cons(Cons* c) {
    Value v;
    v.type = Type::Cons;
    v.cons = c;
    return v;
}

inline Value make_ename(Handle h) {
    Value v;
    v.type = Type::Ename;
    v.ename = h;
    return v;
}

// --- predicates -------------------------------------------------------------

inline bool is_nil(const Value& v) { return v.type == Type::Nil; }

// Everything except nil is true, exactly as AutoLISP has it.
inline bool is_truthy(const Value& v) { return v.type != Type::Nil; }

inline bool is_cons(const Value& v) { return v.type == Type::Cons; }
inline bool is_list(const Value& v) { return v.type == Type::Nil || v.type == Type::Cons; }
inline bool is_number(const Value& v) { return v.type == Type::Int || v.type == Type::Real; }

// Numeric value of an Int or Real; 0 for anything else.
inline double as_double(const Value& v) {
    if (v.type == Type::Real) return v.d;
    if (v.type == Type::Int) return static_cast<double>(v.i);
    return 0.0;
}

// --- list access ------------------------------------------------------------

inline Value car(const Value& v) { return is_cons(v) ? v.cons->car : make_nil(); }
inline Value cdr(const Value& v) { return is_cons(v) ? v.cons->cdr : make_nil(); }

// Length of a proper list; stops at a dotted tail.
std::size_t list_length(const Value& v);

// Structural equality, matching AutoLISP's `equal` rather than `eq`.
bool equal(const Value& a, const Value& b);

// The interpreter's shared state: the arena everything is allocated from, and
// the symbol table that makes symbol comparison a pointer compare.
class Context {
public:
    Context();

    Arena& arena() { return arena_; }

    // Symbols are case-insensitive in R12 AutoLISP; names are upcased on intern
    // so that comparison stays a pointer compare on the hot path.
    Symbol* intern(std::string_view name);

    Str* new_string(std::string_view text);
    Value cons(const Value& car, const Value& cdr);

    // Builds a proper list from a span of values.
    Value list(const Value* items, std::size_t count);

    Symbol* sym_nil() const { return nil_; }
    Symbol* sym_t() const { return t_; }
    Symbol* sym_quote() const { return quote_; }

    std::size_t symbol_count() const { return symbols_.size(); }

    // Drops all allocations. Invalidates every Value previously produced.
    void reset();

private:
    void install_constants();

    Arena arena_;
    std::unordered_map<std::string_view, Symbol*> symbols_;
    Symbol* nil_{nullptr};
    Symbol* t_{nullptr};
    Symbol* quote_{nullptr};
};

// --- printing ---------------------------------------------------------------

// prin1 style: strings quoted and escaped. This is what the reader can read back.
std::string prin1(const Value& v);

// princ style: strings printed raw.
std::string princ(const Value& v);

}  // namespace noto::lisp
