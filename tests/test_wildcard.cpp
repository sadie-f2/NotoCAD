// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// AutoCAD's wildcard dialect, and the ssget filters built on it.
//
// Neither shell globbing nor a regular expression. The two that catch people are
// `.` meaning non-alphanumeric rather than any character, and `~` being an
// anchor at position zero rather than an operator -- both pinned below.

#include "test.hpp"

#include "ncad/command.hpp"
#include "ncad/database.hpp"
#include "ncad/entities.hpp"
#include "ncad/lisp/eval.hpp"

#include <memory>
#include <sstream>
#include <string>

using namespace ncad;
using namespace ncad::lisp;

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

// --- symbol tables ----------------------------------------------------------

TEST_CASE("tblsearch: finds an entry by name, case-insensitively") {
    Fixture f;
    f.eval("(command \"LAYER\" \"MAKE\" \"WALLS\" \"\")");

    CHECK(f.eval("(cdr (assoc 2 (tblsearch \"LAYER\" \"WALLS\")))") == "\"WALLS\"");
    // R12's table names are case-insensitive, and a script that wrote "walls"
    // means the layer WALLS.
    CHECK(f.eval("(cdr (assoc 2 (tblsearch \"LAYER\" \"walls\")))") == "\"WALLS\"");
    CHECK(f.eval("(cdr (assoc 0 (tblsearch \"LAYER\" \"WALLS\")))") == "\"LAYER\"");

    CHECK(f.eval("(tblsearch \"LAYER\" \"NOSUCHLAYER\")") == "nil");
}

TEST_CASE("tblnext: walks a table and then stays at the end") {
    Fixture f;
    f.eval("(command \"LAYER\" \"MAKE\" \"WALLS\" \"\")");
    f.eval("(command \"LAYER\" \"MAKE\" \"GRID\" \"\")");

    // Layer 0 always exists and comes first.
    CHECK(f.eval("(cdr (assoc 2 (tblnext \"LAYER\" T)))") == "\"0\"");
    CHECK(f.eval("(cdr (assoc 2 (tblnext \"LAYER\")))") == "\"WALLS\"");
    CHECK(f.eval("(cdr (assoc 2 (tblnext \"LAYER\")))") == "\"GRID\"");
    CHECK(f.eval("(tblnext \"LAYER\")") == "nil");

    // The cursor stays put rather than wrapping, so a second call keeps saying
    // nil instead of silently starting over -- which would make a while loop
    // run forever.
    CHECK(f.eval("(tblnext \"LAYER\")") == "nil");

    // And rewind starts again.
    CHECK(f.eval("(cdr (assoc 2 (tblnext \"LAYER\" T)))") == "\"0\"");
}

TEST_CASE("tblnext: each table has its own cursor") {
    Fixture f;
    f.eval("(command \"LAYER\" \"MAKE\" \"WALLS\" \"\")");

    f.eval("(tblnext \"LAYER\" T)");
    // Walking the linetypes must not disturb where the layer walk had got to.
    CHECK(f.eval("(cdr (assoc 0 (tblnext \"LTYPE\" T)))") == "\"LTYPE\"");
    CHECK(f.eval("(cdr (assoc 2 (tblnext \"LAYER\")))") == "\"WALLS\"");
}

TEST_CASE("tblsearch: the third argument sets where the walk resumes") {
    Fixture f;
    f.eval("(command \"LAYER\" \"MAKE\" \"WALLS\" \"\")");
    f.eval("(command \"LAYER\" \"MAKE\" \"GRID\" \"\")");

    // R12's way of starting a walk in the middle rather than at the top.
    f.eval("(tblsearch \"LAYER\" \"WALLS\" T)");
    CHECK(f.eval("(cdr (assoc 2 (tblnext \"LAYER\")))") == "\"GRID\"");

    // Without it, the cursor is left alone.
    f.eval("(tblnext \"LAYER\" T)");
    f.eval("(tblsearch \"LAYER\" \"GRID\")");
    CHECK(f.eval("(cdr (assoc 2 (tblnext \"LAYER\")))") == "\"WALLS\"");
}

TEST_CASE("tables: a layer entry carries its colour, linetype and flags") {
    Fixture f;
    f.eval("(command \"LAYER\" \"MAKE\" \"WALLS\" \"COLOR\" \"1\" \"\" \"\")");

    const std::string entry = f.eval("(tblsearch \"LAYER\" \"WALLS\")");
    CHECK(entry.find("(0 . \"LAYER\")") != std::string::npos);
    CHECK(entry.find("(2 . \"WALLS\")") != std::string::npos);
    CHECK(entry.find("(6 . \"CONTINUOUS\")") != std::string::npos);
    // Group 70 is the flags word and must be present even when zero, since a
    // script reads it with assoc and would get nil for an absent group.
    CHECK(entry.find("(70 . 0)") != std::string::npos);
}

TEST_CASE("tables: an unknown table is an error, not an empty walk") {
    Fixture f;
    // "No such table" and "an empty table" are different answers, and a script
    // can act on the difference. Returning nil would make a typo look like an
    // empty drawing.
    CHECK(f.eval("(tblnext \"STYLE\")").find("<error") == 0);
    CHECK(f.eval("(tblsearch \"VPORT\" \"*ACTIVE\")").find("<error") == 0);
    CHECK(f.eval("(tblnext 5)").find("<error") == 0);
}

TEST_CASE("tables: LTYPE and BLOCK walk too") {
    Fixture f;
    CHECK(f.eval("(cdr (assoc 2 (tblnext \"LTYPE\" T)))") == "\"CONTINUOUS\"");

    // A block table starts empty, which is a legitimate answer rather than an
    // error -- unlike naming a table that does not exist.
    CHECK(f.eval("(tblnext \"BLOCK\" T)") == "nil");

    f.eval("(command \"LINE\" '(0 0 0) '(1.0 1.0 0) \"\")");
    f.eval("(command \"BLOCK\" \"WIDGET\" '(0 0 0) \"L\" \"\")");
    CHECK(f.eval("(cdr (assoc 2 (tblnext \"BLOCK\" T)))") == "\"WIDGET\"");
}

// --- interruption, one-step undo, and *error* --------------------------------

TEST_CASE("interrupt: a runaway loop can be stopped") {
    Fixture f;
    // Without this an unbounded (while ...) runs until the process is killed,
    // which presents as a hang rather than as a defect.
    f.in.interrupt();
    const std::string r = f.eval("(while T (setq x 1))");
    CHECK(r.find("<error") == 0);
    CHECK(r.find("Function cancelled") != std::string::npos);

    // The flag is consumed, so the next evaluation runs normally.
    CHECK(f.eval("(+ 1 1)") == "2");
}

TEST_CASE("undo: a whole script is one step") {
    Fixture f;
    f.eval(
        "(command \"LINE\" '(0 0 0) '(1.0 0 0) \"\")"
        "(command \"LINE\" '(0 0 0) '(2.0 0 0) \"\")"
        "(command \"CIRCLE\" '(0 0 0) 1.0)");
    CHECK(f.db.order().size() == 3);

    // One U, not three. A script is one action from the user's side, which is
    // what makes Escape recoverable without counting operations.
    CHECK(f.db.journal().undo_depth() == 1);
    CHECK(f.db.journal().undo(f.db));
    CHECK(f.db.order().empty());
}

TEST_CASE("undo: entmake in bulk is still one step") {
    Fixture f;
    f.eval(
        "(setq i 0)"
        "(while (< i 5)"
        "  (entmake (list '(0 . \"LINE\") (cons 10 (list 0.0 0.0 0.0))"
        "                 (cons 11 (list (float i) 1.0 0.0))))"
        "  (setq i (1+ i)))");
    CHECK(f.db.order().size() == 5);
    CHECK(f.db.journal().undo_depth() == 1);
}

TEST_CASE("undo: a suspended command does not leave the journal unbalanced") {
    // The case the whole resumable-command design exists for: one (command ...)
    // starts a command, arbitrary LISP runs, a later call finishes it. Wrapping
    // each evaluation unconditionally would leave a group nobody closes and the
    // work would never commit at all.
    Fixture f;
    f.eval("(command \"LINE\" '(0 0 0))");
    CHECK(f.engine.active());

    f.eval("(setq unrelated 42)");
    f.eval("(command '(5.0 5.0 0) \"\")");

    CHECK(!f.engine.active());
    CHECK(f.db.order().size() == 1);
    // And it committed as one step rather than being stranded.
    CHECK(f.db.journal().undo_depth() == 1);
    CHECK(f.db.journal().undo(f.db));
    CHECK(f.db.order().empty());
}

TEST_CASE("*error*: the script's own handler is called, with the message") {
    Fixture f;
    f.eval("(defun *error* (msg) (setq caught msg))");

    // A genuine fault.
    f.eval("(+ 1 \"not a number\")");
    CHECK(f.eval("caught").find("bad argument type") != std::string::npos);

    // And an interrupt, which handlers branch on: R12's wording is exactly
    // "Function cancelled".
    f.in.interrupt();
    f.eval("(while T (setq x 1))");
    CHECK(f.eval("caught") == "\"Function cancelled\"");
}

TEST_CASE("*error*: a handler that itself fails does not recurse forever") {
    Fixture f;
    // Losing the session is a worse failure than losing the command.
    f.eval("(defun *error* (msg) (+ 1 \"also broken\"))");
    const std::string r = f.eval("(+ 1 \"broken\")");
    CHECK(r.find("<error") == 0);
    // The original error survives, not the handler's.
    CHECK(r.find("bad argument type") != std::string::npos);
}

TEST_CASE("*error*: no handler defined is not itself an error") {
    Fixture f;
    const std::string r = f.eval("(+ 1 \"broken\")");
    CHECK(r.find("<error") == 0);
}
