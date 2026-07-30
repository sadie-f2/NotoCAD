// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// AutoCAD's wildcard dialect, and the ssget filters built on it.
//
// Neither shell globbing nor a regular expression. The two that catch people are
// `.` meaning non-alphanumeric rather than any character, and `~` being an
// anchor at position zero rather than an operator -- both pinned below.

#include "test.hpp"

#include "noto/command.hpp"
#include "noto/database.hpp"
#include "noto/entities.hpp"
#include "noto/lisp/eval.hpp"

#include <memory>
#include <sstream>
#include <string>

using namespace noto;
using namespace noto::lisp;

namespace {

struct Fixture {
    Database db;
    CommandEngine engine{db};
    Context ctx;
    Interp in{ctx};
    std::ostringstream out;

    Fixture() {
        in.set_output(&out);
        in.set_database(&db);
        in.set_command_engine(&engine);
    }

    std::string eval(const std::string& source) {
        in.clear_error();
        Value v;
        if (!in.eval_string(source, v)) return "<error: " + in.error().message() + ">";
        return prin1(v);
    }

    bool yes(const std::string& source) { return eval(source) == "T"; }
};

}  // namespace

TEST_CASE("wcmatch: the ordinary metacharacters") {
    Fixture f;
    CHECK(f.yes("(wcmatch \"WALLS\" \"WALL*\")"));
    CHECK(f.yes("(wcmatch \"WALLS\" \"*ALL*\")"));
    CHECK(f.yes("(wcmatch \"WALLS\" \"W?LLS\")"));
    CHECK(!f.yes("(wcmatch \"WALLS\" \"W?LL\")"));

    // A pattern with no metacharacters is an exact match, not a substring one.
    CHECK(f.yes("(wcmatch \"WALLS\" \"WALLS\")"));
    CHECK(!f.yes("(wcmatch \"WALLS\" \"WALL\")"));
}

TEST_CASE("wcmatch: the character classes, including the one people misread") {
    Fixture f;
    CHECK(f.yes("(wcmatch \"A1\" \"@#\")"));   // alpha then digit
    CHECK(!f.yes("(wcmatch \"11\" \"@#\")"));  // @ is not a digit
    CHECK(!f.yes("(wcmatch \"AA\" \"@#\")"));  // # is not alphabetic

    // `.` is NON-ALPHANUMERIC, not "any character". This is the one that reads
    // like a regular expression and is not.
    CHECK(f.yes("(wcmatch \"A-\" \"@.\")"));
    CHECK(!f.yes("(wcmatch \"AB\" \"@.\")"));
    CHECK(!f.yes("(wcmatch \"A1\" \"@.\")"));
}

TEST_CASE("wcmatch: bracket sets and ranges") {
    Fixture f;
    CHECK(f.yes("(wcmatch \"B\" \"[ABC]\")"));
    CHECK(!f.yes("(wcmatch \"D\" \"[ABC]\")"));
    CHECK(f.yes("(wcmatch \"D\" \"[~ABC]\")"));
    CHECK(!f.yes("(wcmatch \"B\" \"[~ABC]\")"));

    CHECK(f.yes("(wcmatch \"M\" \"[A-Z]\")"));
    CHECK(!f.yes("(wcmatch \"5\" \"[A-Z]\")"));
    CHECK(f.yes("(wcmatch \"PART7\" \"PART[0-9]\")"));
}

TEST_CASE("wcmatch: a leading tilde negates the WHOLE pattern") {
    Fixture f;
    // An anchor at position zero, not an operator that can appear anywhere.
    CHECK(f.yes("(wcmatch \"PIPE\" \"~WALL*\")"));
    CHECK(!f.yes("(wcmatch \"WALLS\" \"~WALL*\")"));

    // And it negates across the alternatives too, rather than binding only to
    // the first of them.
    CHECK(!f.yes("(wcmatch \"ARC\" \"~LINE,ARC\")"));
    CHECK(f.yes("(wcmatch \"TEXT\" \"~LINE,ARC\")"));
}

TEST_CASE("wcmatch: comma alternatives and the backquote escape") {
    Fixture f;
    CHECK(f.yes("(wcmatch \"ARC\" \"LINE,ARC,CIRCLE\")"));
    CHECK(!f.yes("(wcmatch \"TEXT\" \"LINE,ARC,CIRCLE\")"));

    // The escape makes a metacharacter literal.
    CHECK(f.yes("(wcmatch \"A*B\" \"A`*B\")"));
    CHECK(!f.yes("(wcmatch \"AXB\" \"A`*B\")"));
}

TEST_CASE("wcmatch: case-sensitive, unlike the same matcher inside a filter") {
    Fixture f;
    CHECK(f.yes("(wcmatch \"WALLS\" \"WALL*\")"));
    CHECK(!f.yes("(wcmatch \"walls\" \"WALL*\")"));
    // Both behaviours are AutoCAD's; the filter case is tested below.
}

// --- ssget filters ----------------------------------------------------------

namespace {

void build_drawing(Fixture& f) {
    f.eval(
        "(command \"LINE\" '(0 0 0) '(10.0 0 0) \"\")"
        "(command \"CIRCLE\" '(5.0 5.0 0) 3.0)"
        "(command \"CIRCLE\" '(20.0 5.0 0) 2.0)"
        "(command \"LINE\" '(0 10.0 0) '(10.0 10.0 0) \"\")");
}

}  // namespace

TEST_CASE("ssget filter: by entity type, which is the common case") {
    Fixture f;
    build_drawing(f);

    CHECK(f.eval("(sslength (ssget \"X\"))") == "4");
    CHECK(f.eval("(sslength (ssget \"X\" '((0 . \"CIRCLE\"))))") == "2");
    CHECK(f.eval("(sslength (ssget \"X\" '((0 . \"LINE\"))))") == "2");

    // An empty result is nil, which is what a script tests for.
    CHECK(f.eval("(ssget \"X\" '((0 . \"TEXT\")))") == "nil");
}

TEST_CASE("ssget filter: wildcards and comma alternatives in the value") {
    Fixture f;
    build_drawing(f);

    CHECK(f.eval("(sslength (ssget \"X\" '((0 . \"C*\"))))") == "2");
    CHECK(f.eval("(sslength (ssget \"X\" '((0 . \"LINE,CIRCLE\"))))") == "4");
    CHECK(f.eval("(sslength (ssget \"X\" '((0 . \"~CIRCLE\"))))") == "2");
}

TEST_CASE("ssget filter: string comparison folds case, unlike wcmatch") {
    Fixture f;
    build_drawing(f);
    // (8 . "walls") finds layer WALLS in AutoCAD, so a filter folds where the
    // standalone matcher does not.
    CHECK(f.eval("(sslength (ssget \"X\" '((0 . \"circle\"))))") == "2");
    CHECK(f.eval("(sslength (ssget \"X\" '((8 . \"0\"))))") == "4");
}

TEST_CASE("ssget filter: several pairs must ALL match") {
    Fixture f;
    build_drawing(f);
    // R12's rule, and the reason there is no explicit AND.
    CHECK(f.eval("(sslength (ssget \"X\" '((0 . \"CIRCLE\") (8 . \"0\"))))") == "2");
    CHECK(f.eval("(ssget \"X\" '((0 . \"CIRCLE\") (8 . \"NOSUCHLAYER\")))") == "nil");
}

TEST_CASE("ssget filter: numbers compare numerically") {
    Fixture f;
    build_drawing(f);
    // Radius is group 40 on a circle. One of each here.
    CHECK(f.eval("(sslength (ssget \"X\" '((40 . 3.0))))") == "1");
    CHECK(f.eval("(sslength (ssget \"X\" '((40 . 2.0))))") == "1");
    CHECK(f.eval("(ssget \"X\" '((40 . 99.0)))") == "nil");
}

TEST_CASE("ssget filter: applies to the region modes too, not only X") {
    Fixture f;
    build_drawing(f);
    // A window round everything, then narrowed by type -- which is the point of
    // the filter being the last argument whatever came before it.
    CHECK(f.eval("(sslength (ssget \"C\" '(-50.0 -50.0) '(50.0 50.0)))") == "4");
    CHECK(f.eval(
              "(sslength (ssget \"C\" '(-50.0 -50.0) '(50.0 50.0) '((0 . \"CIRCLE\"))))") == "2");
}

TEST_CASE("ssget filter: a point argument is not mistaken for a filter") {
    Fixture f;
    build_drawing(f);
    // A filter is a list whose first element is a dotted PAIR; a point is a list
    // of numbers. That is how the two are told apart, and getting it wrong would
    // make every windowed ssget fail.
    CHECK(f.eval("(sslength (ssget \"W\" '(-50.0 -50.0) '(50.0 50.0)))") == "4");
}
