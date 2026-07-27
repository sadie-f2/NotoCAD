// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/lisp/arena.hpp"
#include "noto/lisp/reader.hpp"
#include "noto/lisp/value.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace noto;
using namespace noto::lisp;

namespace {

// Reads one form and returns its prin1 form, so tests read as source -> output.
std::string roundtrip(Context& ctx, const std::string& source) {
    ReadError err;
    const Value v = read_one(ctx, source, &err);
    if (!err.ok() && err.status != ReadStatus::EndOfInput) {
        return std::string("<error: ") + read_status_message(err.status) + ">";
    }
    return prin1(v);
}

}  // namespace

TEST_CASE("arena: allocations are aligned and distinct") {
    Arena a(1024);
    CHECK(a.bytes_used() == 0);

    void* p1 = a.alloc(16, 8);
    void* p2 = a.alloc(16, 8);
    CHECK(p1 != p2);
    CHECK(reinterpret_cast<std::uintptr_t>(p1) % 8 == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(p2) % 8 == 0);
    CHECK(a.bytes_used() >= 32);

    // An allocation larger than the block size still succeeds, in its own block.
    void* big = a.alloc(8192, 16);
    CHECK(big != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(big) % 16 == 0);
    CHECK(a.block_count() >= 2);

    a.reset();
    CHECK(a.bytes_used() == 0);
    CHECK(a.block_count() == 0);
}

TEST_CASE("arena: many small cells stay in few blocks") {
    // The mesh-generation path allocates cons cells in bulk; this is the shape
    // that has to stay cheap.
    Arena a(64 * 1024);
    for (int i = 0; i < 20000; ++i) {
        void* p = a.alloc(32, 8);
        CHECK(p != nullptr);
    }
    CHECK(a.bytes_used() >= 20000 * 32);
    // 20000 * 32 = 640KB across 64KB blocks: about 10, certainly not thousands.
    CHECK(a.block_count() < 20);
}

TEST_CASE("lisp: symbols are interned and case-insensitive") {
    Context ctx;
    Symbol* a = ctx.intern("setq");
    Symbol* b = ctx.intern("SETQ");
    Symbol* c = ctx.intern("SetQ");
    CHECK(a == b);
    CHECK(b == c);
    CHECK(a->name == "SETQ");  // stored upcased

    CHECK(ctx.intern("other") != a);

    // Interning an existing symbol must not grow the table.
    const std::size_t before = ctx.symbol_count();
    ctx.intern("SETQ");
    CHECK(ctx.symbol_count() == before);
}

TEST_CASE("lisp: reader distinguishes integers, reals and symbols") {
    Context ctx;
    CHECK(roundtrip(ctx, "42") == "42");
    CHECK(roundtrip(ctx, "-7") == "-7");
    CHECK(roundtrip(ctx, "1.5") == "1.5");
    CHECK(roundtrip(ctx, ".5") == "0.5");
    CHECK(roundtrip(ctx, "-0.25") == "-0.25");
    CHECK(roundtrip(ctx, "1e3") == "1000.0");

    // An integer literal must stay an integer: R12 arithmetic is type-sensitive.
    ReadError err;
    CHECK(read_one(ctx, "42", &err).type == Type::Int);
    CHECK(read_one(ctx, "42.0", &err).type == Type::Real);

    // Tokens that merely start like numbers are symbols.
    CHECK(read_one(ctx, "1+", &err).type == Type::Sym);
    CHECK(read_one(ctx, "-", &err).type == Type::Sym);
    CHECK(read_one(ctx, "+", &err).type == Type::Sym);
}

TEST_CASE("lisp: nil and T read as themselves") {
    Context ctx;
    ReadError err;
    CHECK(read_one(ctx, "nil", &err).type == Type::Nil);
    CHECK(read_one(ctx, "NIL", &err).type == Type::Nil);
    CHECK(read_one(ctx, "t", &err).type == Type::True);
    CHECK(roundtrip(ctx, "nil") == "nil");
    CHECK(roundtrip(ctx, "T") == "T");

    // Only nil is false.
    CHECK(!is_truthy(make_nil()));
    CHECK(is_truthy(make_int(0)));
    CHECK(is_truthy(make_true()));
}

TEST_CASE("lisp: lists and nesting") {
    Context ctx;
    CHECK(roundtrip(ctx, "()") == "nil");
    CHECK(roundtrip(ctx, "(1 2 3)") == "(1 2 3)");
    CHECK(roundtrip(ctx, "(1 (2 3) 4)") == "(1 (2 3) 4)");
    CHECK(roundtrip(ctx, "  ( 1   2 )  ") == "(1 2)");

    ReadError err;
    CHECK(list_length(read_one(ctx, "(1 2 3 4 5)", &err)) == 5);
    CHECK(list_length(read_one(ctx, "nil", &err)) == 0);
}

TEST_CASE("lisp: dotted pairs, the shape entity data arrives in") {
    Context ctx;
    CHECK(roundtrip(ctx, "(1 . 2)") == "(1 . 2)");
    CHECK(roundtrip(ctx, "(0 . \"LINE\")") == "(0 . \"LINE\")");

    // A DXF-style association list, which is exactly what entmake consumes.
    const std::string alist = "((0 . \"LINE\") (8 . \"WALLS\") (10 1.0 2.0 0.0))";
    CHECK(roundtrip(ctx, alist) == alist);

    ReadError err;
    const Value v = read_one(ctx, "(0 . \"LINE\")", &err);
    CHECK(err.ok());
    CHECK(is_cons(v));
    CHECK(car(v).type == Type::Int);
    CHECK(car(v).i == 0);
    CHECK(cdr(v).type == Type::Str);  // dotted, so cdr is the value not a list
    CHECK(cdr(v).str->view() == "LINE");
}

TEST_CASE("lisp: a dot only starts a dotted pair when it stands alone") {
    Context ctx;
    // .5 is a number and .foo is a symbol, neither is a dotted-pair marker.
    CHECK(roundtrip(ctx, "(1 .5)") == "(1 0.5)");
    ReadError err;
    CHECK(read_one(ctx, "(a .b)", &err).type == Type::Cons);
    CHECK(err.ok());
}

TEST_CASE("lisp: strings and escapes") {
    Context ctx;
    CHECK(roundtrip(ctx, "\"hello\"") == "\"hello\"");
    CHECK(roundtrip(ctx, "\"a\\nb\"") == "\"a\\nb\"");
    CHECK(roundtrip(ctx, "\"say \\\"hi\\\"\"") == "\"say \\\"hi\\\"\"");

    ReadError err;
    const Value v = read_one(ctx, "\"a\\tb\"", &err);
    CHECK(v.str->view() == "a\tb");
    // princ prints raw, prin1 prints readable.
    CHECK(princ(v) == "a\tb");
    CHECK(prin1(v) == "\"a\\tb\"");
}

TEST_CASE("lisp: quote reads as (QUOTE x)") {
    Context ctx;
    CHECK(roundtrip(ctx, "'foo") == "(QUOTE FOO)");
    CHECK(roundtrip(ctx, "'(1 2)") == "(QUOTE (1 2))");
}

TEST_CASE("lisp: comments run to end of line") {
    Context ctx;
    CHECK(roundtrip(ctx, "; a comment\n42") == "42");
    CHECK(roundtrip(ctx, "(1 ; inner\n 2)") == "(1 2)");
}

TEST_CASE("lisp: reader reports errors rather than throwing") {
    Context ctx;
    ReadError err;

    read_one(ctx, "(1 2", &err);
    CHECK(err.status == ReadStatus::UnexpectedEof);

    read_one(ctx, ")", &err);
    CHECK(err.status == ReadStatus::UnexpectedRParen);

    read_one(ctx, "\"unterminated", &err);
    CHECK(err.status == ReadStatus::BadString);

    read_one(ctx, "(. 1)", &err);
    CHECK(err.status == ReadStatus::BadDottedPair);

    read_one(ctx, "   ", &err);
    CHECK(err.status == ReadStatus::EndOfInput);

    // Errors carry a position.
    Reader r(ctx, "(1\n 2\n  (");
    Value v;
    CHECK(!r.read(v));
    CHECK(r.error().line == 3);
}

TEST_CASE("lisp: read_all consumes a whole source file") {
    Context ctx;
    Reader r(ctx, "(setq a 1)\n(setq b 2)\n; trailing comment\n");
    std::vector<Value> forms;
    CHECK(r.read_all(forms));
    CHECK(forms.size() == 2);
    CHECK(prin1(forms[0]) == "(SETQ A 1)");
    CHECK(prin1(forms[1]) == "(SETQ B 2)");
}

TEST_CASE("lisp: equal is structural and numeric across int/real") {
    Context ctx;
    ReadError err;
    CHECK(equal(read_one(ctx, "(1 2 (3))", &err), read_one(ctx, "(1 2 (3))", &err)));
    CHECK(!equal(read_one(ctx, "(1 2)", &err), read_one(ctx, "(1 3)", &err)));
    CHECK(equal(make_int(1), make_real(1.0)));
    CHECK(equal(read_one(ctx, "\"ab\"", &err), read_one(ctx, "\"ab\"", &err)));
    // Interned symbols compare as pointers.
    CHECK(equal(make_sym(ctx.intern("x")), make_sym(ctx.intern("X"))));
}

TEST_CASE("lisp: real printing round-trips without noise") {
    Context ctx;
    ReadError err;
    // Shortest round-tripping form: 0.1 prints as 0.1, not 0.100000000000000006.
    CHECK(prin1(make_real(0.1)) == "0.1");
    CHECK(prin1(make_real(1.0)) == "1.0");
    CHECK(prin1(make_real(-2.5)) == "-2.5");
    CHECK(read_one(ctx, prin1(make_real(1.0 / 3.0)), &err).d == 1.0 / 3.0);
}

TEST_CASE("lisp: context reset reclaims the arena and keeps constants") {
    Context ctx;
    ReadError err;
    read_one(ctx, "(1 2 3 4 5)", &err);
    ctx.intern("SOMETHING");
    CHECK(ctx.arena().bytes_used() > 0);

    ctx.reset();
    // nil, T and QUOTE are reinstalled, so the table is not empty.
    CHECK(ctx.symbol_count() == 3);
    CHECK(ctx.sym_nil() != nullptr);
    CHECK(ctx.sym_t() != nullptr);
    CHECK(roundtrip(ctx, "(1 . 2)") == "(1 . 2)");
}
