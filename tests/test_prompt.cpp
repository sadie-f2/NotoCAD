// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "ncad/command.hpp"
#include "ncad/lisp/command_input.hpp"
#include "ncad/lisp/eval.hpp"
#include "ncad/lisp/reader.hpp"
#include "prompt.hpp"

#include <string>
#include <vector>

using namespace ncad;
using namespace ncad::app;

namespace {

#define CHECK_VEC3(a, b) CHECK_VEC((a), (b).x, (b).y, (b).z, 1e-9)

lisp::Value read(lisp::Context& ctx, const std::string& source) {
    lisp::ReadError err;
    return lisp::read_one(ctx, source, &err);
}

}  // namespace

TEST_CASE("prompt line: whitespace separates answers") {
    // At a command prompt a space acts as Enter, so one typed line can carry a
    // whole command.
    const auto tokens = split_prompt_line("LINE 0,0 10,0", false);
    CHECK(tokens.size() == 3);
    CHECK(tokens[0] == "LINE");
    CHECK(tokens[1] == "0,0");
    CHECK(tokens[2] == "10,0");

    CHECK(split_prompt_line("   ", false).empty());
    CHECK(split_prompt_line("CIRCLE 0,0 5 ; a comment", false).size() == 3);
}

TEST_CASE("prompt line: a comma joins across spaces") {
    // Reported from use: `1,1,1` was a point and `1, 1, 1` was three answers,
    // the first of which -- "1," -- cannot be a point and reported as one.
    //
    // Whitespace still separates answers, because space-is-Enter is R12's and
    // is what makes `CIRCLE 0,0 5` a centre and a radius. But a token ending in
    // a comma cannot be a complete answer to anything, and neither can one
    // beginning with a comma follow one, so gluing them changes the meaning of
    // no valid line.
    auto tokens = split_prompt_line("1, 1, 1", false);
    CHECK(tokens.size() == 1);
    CHECK(tokens[0] == "1,1,1");

    // A space before the comma as well.
    tokens = split_prompt_line("1 , 1 , 1", false);
    CHECK(tokens.size() == 1);
    CHECK(tokens[0] == "1,1,1");

    // And the whole command on one line, still split where it should be.
    tokens = split_prompt_line("LINE 0, 0 10, 10", false);
    CHECK(tokens.size() == 3);
    CHECK(tokens[0] == "LINE");
    CHECK(tokens[1] == "0,0");
    CHECK(tokens[2] == "10,10");

    // What must NOT change: an ordinary pair is still one token, and two
    // separate answers are still two.
    tokens = split_prompt_line("CIRCLE 0,0 5", false);
    CHECK(tokens.size() == 3);
    CHECK(tokens[2] == "5");
}

TEST_CASE("prompt line: a parenthesised expression stays whole") {
    // Splitting on spaces inside an expression would break every LISP answer
    // that takes more than one argument.
    auto tokens = split_prompt_line("CIRCLE 0,0 (* 2.0 5.0)", false);
    CHECK(tokens.size() == 3);
    CHECK(tokens[2] == "(* 2.0 5.0)");

    // Nested parens.
    tokens = split_prompt_line("(list (+ 1 2) 3)", false);
    CHECK(tokens.size() == 1);
    CHECK(tokens[0] == "(list (+ 1 2) 3)");

    // A close paren inside a string must not end the expression.
    tokens = split_prompt_line("(strcat \"a )\" \"b\")", false);
    CHECK(tokens.size() == 1);
    CHECK(tokens[0] == "(strcat \"a )\" \"b\")");

    // And an escaped quote inside that string.
    tokens = split_prompt_line("(strcat \"a\\\")\" \"b\") tail", false);
    CHECK(tokens.size() == 2);
    CHECK(tokens[1] == "tail");
}

TEST_CASE("prompt line: a string prompt takes the line verbatim") {
    const auto tokens = split_prompt_line("hello world  ", true);
    CHECK(tokens.size() == 1);
    CHECK(tokens[0] == "hello world");

    // A blank line is still one answer -- Enter -- rather than none.
    const auto blank = split_prompt_line("   ", true);
    CHECK(blank.size() == 1);
    CHECK(blank[0].empty());
}

TEST_CASE("prompt: incomplete LISP is detected so the caller can read on") {
    lisp::Context ctx;
    CHECK(needs_more_input(ctx, "(setq a"));
    CHECK(needs_more_input(ctx, "(defun f (x)\n"));
    CHECK(!needs_more_input(ctx, "(setq a 1)"));
    CHECK(!needs_more_input(ctx, "LINE"));
    // A malformed form is not "incomplete"; it is wrong, and must not hang the
    // prompt waiting for a line that would fix it.
    CHECK(!needs_more_input(ctx, "(setq a 1))"));
}

TEST_CASE("value_to_input: conversion is directed by the prompt") {
    lisp::Context ctx;
    InputValue v;
    std::string err;

    Prompt point;
    point.kind = PromptKind::Point;
    CHECK(lisp::value_to_input(point, read(ctx, "(1.0 2.0 3.0)"), v, err));
    CHECK(v.kind == InputKind::Point);
    CHECK_VEC3(v.point, Vec3(1.0, 2.0, 3.0));

    // Two-element lists are points at Z = 0.
    CHECK(lisp::value_to_input(point, read(ctx, "(4 5)"), v, err));
    CHECK_VEC3(v.point, Vec3(4.0, 5.0, 0.0));

    // The same integer means different things at different prompts.
    Prompt entity;
    entity.kind = PromptKind::Entity;
    CHECK(lisp::value_to_input(entity, lisp::make_int(7), v, err));
    CHECK(v.kind == InputKind::Entity);
    CHECK(v.entity == 7);

    Prompt distance;
    distance.kind = PromptKind::Distance;
    CHECK(lisp::value_to_input(distance, lisp::make_int(7), v, err));
    CHECK(v.kind == InputKind::Real);
    CHECK_NEAR(v.real, 7.0, 1e-12);
}

TEST_CASE("value_to_input: nil and the empty string are Enter") {
    lisp::Context ctx;
    InputValue v;
    std::string err;

    Prompt p;
    p.kind = PromptKind::Point;
    p.allow_empty = true;

    CHECK(lisp::value_to_input(p, lisp::make_nil(), v, err));
    CHECK(v.kind == InputKind::None);
    CHECK(lisp::value_to_input(p, read(ctx, "\"\""), v, err));
    CHECK(v.kind == InputKind::None);
}

TEST_CASE("value_to_input: strings go through the same parser typed text does") {
    lisp::Context ctx;
    InputValue v;
    std::string err;

    Prompt p;
    p.kind = PromptKind::Distance;
    p.keywords = {"Diameter"};
    CHECK(lisp::value_to_input(p, read(ctx, "\"D\""), v, err));
    CHECK(v.kind == InputKind::Keyword);
    CHECK(v.text == "DIAMETER");

    Prompt point;
    point.kind = PromptKind::Point;
    CHECK(lisp::value_to_input(point, read(ctx, "\"3,4\""), v, err));
    CHECK(v.kind == InputKind::Point);
    CHECK_VEC3(v.point, Vec3(3.0, 4.0, 0.0));
}

TEST_CASE("value_to_input: an ename answers a selection prompt") {
    InputValue v;
    std::string err;
    Prompt p;
    p.kind = PromptKind::Entity;
    CHECK(lisp::value_to_input(p, lisp::make_ename(42), v, err));
    CHECK(v.kind == InputKind::Entity);
    CHECK(v.entity == 42);
}

TEST_CASE("value_to_input: unusable values are reported") {
    lisp::Context ctx;
    InputValue v;
    std::string err;

    Prompt p;
    p.kind = PromptKind::Point;
    // A symbol is not geometry.
    CHECK(!lisp::value_to_input(p, lisp::make_sym(ctx.intern("FOO")), v, err));
    CHECK(!err.empty());
    // Nor is a list of non-numbers.
    CHECK(!lisp::value_to_input(p, read(ctx, "(\"a\" \"b\")"), v, err));
    CHECK(!err.empty());
    // Nor a four-element list.
    CHECK(!lisp::value_to_input(p, read(ctx, "(1 2 3 4)"), v, err));
}

// --- PromptSession -----------------------------------------------------------
//
// The seam the Qt shell shares with ncad. Both front ends differ only in where
// lines come from and where output goes, so these cases are what keeps the
// command line in the window behaving like the one at the terminal.

namespace {

struct Session {
    Database db;
    lisp::Context ctx;
    lisp::Interp in{ctx};
    CommandEngine engine{db};

    struct Recorder final : PromptOutput {
        std::string out;
        std::string err;
        void write(const std::string& text) override { out += text; }
        void write_error(const std::string& text) override { err += text; }
    } rec;

    PromptSession session{ctx, in, engine, rec, true};

    Session() {
        in.set_database(&db);
        in.set_command_engine(&engine);
    }

    bool feed(const std::string& line) { return session.feed_line(line); }
    std::string prompt() const { return session.current_prompt(); }
};

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("session: a typed command runs to completion a line at a time") {
    Session s;
    CHECK(s.prompt() == "Command: ");

    s.feed("LINE");
    CHECK(s.engine.active());
    CHECK(contains(s.prompt(), "first point"));

    s.feed("0,0");
    CHECK(contains(s.prompt(), "next point"));

    s.feed("10,0");
    s.feed("");  // Enter ends LINE
    CHECK(!s.engine.active());
    CHECK(s.db.size() == 1);
    CHECK(s.prompt() == "Command: ");
}

TEST_CASE("session: a whole command on one line, as at the terminal") {
    Session s;
    s.feed("LINE 0,0 10,0 ");
    CHECK(s.db.size() == 1);
}

TEST_CASE("session: supply() and typed lines drive the same command") {
    // The case the GUI depends on: a command started by typing, answered by a
    // click, and finished by typing again. If these were separate paths this is
    // where they would diverge.
    Session s;
    s.feed("LINE");
    s.feed("0,0");

    s.engine.supply(InputValue::of_point({10.0, 0.0, 0.0}));  // the "click"
    CHECK(s.engine.active());
    CHECK(contains(s.prompt(), "next point"));

    s.feed("");
    CHECK(!s.engine.active());
    CHECK(s.db.size() == 1);
}

TEST_CASE("session: an unterminated LISP form spans lines") {
    Session s;
    s.feed("(setq r");
    CHECK(s.session.continuing());
    CHECK(s.prompt() == ">  ");

    s.feed(" 5.0)");
    CHECK(!s.session.continuing());
    CHECK(s.prompt() == "Command: ");
    CHECK(contains(s.rec.out, "5.0"));
}

TEST_CASE("session: !variable answers a prompt from the interpreter") {
    Session s;
    s.feed("(setq r 7.5)");
    s.feed("CIRCLE");
    s.feed("0,0");
    s.feed("!r");
    CHECK(!s.engine.active());
    CHECK(s.db.size() == 1);
}

TEST_CASE("session: Enter repeats the last command") {
    // CIRCLE rather than LINE: LINE keeps asking for points, so it is still
    // running after its arguments and a blank line would end it instead.
    Session s;
    s.feed("CIRCLE 0,0 5");
    CHECK(!s.engine.active());
    CHECK(s.db.size() == 1);

    s.feed("");  // repeats CIRCLE
    CHECK(s.engine.active());
    CHECK(contains(s.prompt(), "enter") || contains(s.prompt(), "Specify"));
}

TEST_CASE("session: QUIT ends the session") {
    Session s;
    CHECK(s.feed("(setq a 1)"));
    CHECK(!s.feed("QUIT"));
}

TEST_CASE("session: a running command takes the line before the command prompt does") {
    // R12: while a command is asking, what you type answers it. QUIT here is a
    // bad point, not the QUIT command -- which is why feed_line keeps going.
    Session s;
    s.feed("LINE");
    CHECK(s.engine.active());
    CHECK(s.feed("QUIT"));
    CHECK(!s.rec.err.empty());
}

TEST_CASE("session: an unknown command is reported, not fatal") {
    Session s;
    CHECK(s.feed("NOSUCHCOMMAND"));
    CHECK(contains(s.rec.err, "Unknown command"));
    CHECK(s.prompt() == "Command: ");
}

TEST_CASE("session: an unterminated form left open is an error at end of input") {
    Session s;
    s.feed("(setq r");
    CHECK(!s.session.finish());
    CHECK(contains(s.rec.err, "unterminated"));
}
