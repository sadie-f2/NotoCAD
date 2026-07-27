// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/command.hpp"
#include "noto/lisp/command_input.hpp"
#include "noto/lisp/eval.hpp"
#include "noto/lisp/reader.hpp"
#include "prompt.hpp"

#include <string>
#include <vector>

using namespace noto;
using namespace noto::app;

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
