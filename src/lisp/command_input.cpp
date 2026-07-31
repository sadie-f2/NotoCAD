// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/lisp/command_input.hpp"

#include "ncad/input_text.hpp"

#include <string>

namespace ncad::lisp {
namespace {

// A LISP point is a list of two or three numbers.
bool value_to_point(const Value& v, Vec3& out) {
    if (!is_cons(v)) return false;
    double c[3] = {0.0, 0.0, 0.0};
    std::size_t i = 0;
    for (Value cur = v; is_cons(cur); cur = cdr(cur)) {
        if (i >= 3) return false;
        const Value n = car(cur);
        if (!is_number(n)) return false;
        c[i++] = as_double(n);
    }
    if (i < 2) return false;
    out = Vec3{c[0], c[1], c[2]};
    return true;
}

}  // namespace

bool value_to_input(const Prompt& prompt, const Value& v, InputValue& out, std::string& error) {
    error.clear();

    switch (v.type) {
        case Type::Nil:
            // nil is Enter.
            out = InputValue::none();
            return true;

        case Type::Sset:
            // A selection set is not an answer to a prompt. R12 has no way to
            // hand one to (command ...) either -- you name the entities, or you
            // use Previous.
            error = "a selection set is not valid input to a command";
            return false;

        case Type::Str: {
            const std::string text(v.str->view());
            if (text.empty()) {
                out = InputValue::none();
                return true;
            }
            // Routed through the same parser typed text uses, so keywords and
            // coordinate strings behave identically however they arrived.
            return parse_input(prompt, text, out, error);
        }

        case Type::Int:
            if (prompt.kind == PromptKind::Entity) {
                out = InputValue::of_entity(static_cast<Handle>(v.i));
            } else if (prompt.kind == PromptKind::Integer) {
                out = InputValue::of_integer(v.i);
            } else {
                out = InputValue::of_real(static_cast<double>(v.i));
            }
            return true;

        case Type::Real:
            out = InputValue::of_real(v.d);
            return true;

        case Type::Ename:
            out = InputValue::of_entity(v.ename);
            return true;

        case Type::Cons: {
            Vec3 p;
            if (!value_to_point(v, p)) {
                error = "not a point: " + prin1(v);
                return false;
            }
            out = InputValue::of_point(p);
            return true;
        }

        case Type::True:
        case Type::Sym:
        case Type::Subr:
        case Type::File:
            break;
    }

    error = "cannot use " + std::string(type_name(v.type)) + " as command input";
    return false;
}

}  // namespace ncad::lisp
