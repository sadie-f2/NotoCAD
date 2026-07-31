// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Internal: the entity-access builtins, declared here so the table in
// builtins.cpp can reference them while their implementation stays in its own
// translation unit. Not an installed header.
#pragma once

#include "ncad/lisp/eval.hpp"

namespace ncad {
class Database;
class Entity;
}  // namespace ncad

namespace ncad::lisp {

bool subr_entmake(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_entget(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_entmod(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_entdel(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_entlast(Interp& in, const Value* args, std::size_t argc, Value& out);
bool subr_entnext(Interp& in, const Value* args, std::size_t argc, Value& out);

// The alist entget hands back. Exposed so that ssget's filters test against
// EXACTLY what entget would show -- a filter and an entget disagreeing about an
// entity would be the worst kind of wrong, since a script would see one and act
// on the other.
Value entity_to_alist(Context& ctx, const Database& db, const Entity& ent);

}  // namespace ncad::lisp
