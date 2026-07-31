// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// getvar / setvar -- AutoLISP's whole view of the system variable table.
//
// The two are deliberately asymmetric about names they do not know, and this
// matches R12 rather than being an oversight:
//
//   (getvar "NOSUCHVAR")   -> nil
//   (setvar "NOSUCHVAR" 1) -> error
//
// getvar returning nil is what lets LISP written for a later release probe for
// a variable and carry on without it, which is a real and common idiom. setvar
// has no such use: writing to a variable that does not exist is a typo, and
// silently discarding the write is how a script comes to have no effect at all
// for reasons nobody can see.
#include "sysvar_subrs.hpp"

#include "ncad/database.hpp"
#include "ncad/sysvar.hpp"

#include <string>

namespace ncad::lisp {
namespace {

Sysvars* require_sysvars(Interp& in, const char* who) {
    Database* db = in.database();
    if (!db) {
        in.fail(EvalStatus::BadArgumentType, std::string(who) + ": no drawing is attached");
        return nullptr;
    }
    return &db->sysvars();
}

bool require_name(Interp& in, const char* who, const Value& v, std::string& out) {
    if (v.type != Type::Str) {
        in.fail(EvalStatus::BadArgumentType,
                std::string(who) + ": variable name must be a string: " + prin1(v));
        return false;
    }
    out.assign(v.str->view());
    return true;
}

// R12 hands a point variable to LISP as a three-element list, the same shape
// getpoint returns and (command ...) accepts.
Value point_to_list(Context& ctx, const Vec3& p) {
    const Value items[3] = {make_real(p.x), make_real(p.y), make_real(p.z)};
    return ctx.list(items, 3);
}

Value sysvar_to_value(Context& ctx, const SysvarValue& v) {
    switch (v.type) {
        case SysvarType::Int: return make_int(v.integer);
        case SysvarType::Real: return make_real(v.real);
        case SysvarType::String: return make_str(ctx.new_string(v.text));
        case SysvarType::Point: return point_to_list(ctx, v.point);
    }
    return make_nil();
}

// The reverse, for setvar. Only the shapes a caller can actually write: a bare
// number or string. A point arrives as a list and is read separately, because
// deciding "is this a point" needs the target variable's type to be known.
bool value_to_sysvar(const Value& v, SysvarValue& out) {
    switch (v.type) {
        case Type::Int: out = SysvarValue::of_int(v.i); return true;
        case Type::Real: out = SysvarValue::of_real(v.d); return true;
        case Type::Str: out = SysvarValue::of_string(std::string(v.str->view())); return true;
        default: return false;
    }
}

bool list_to_point(const Value& v, Vec3& out) {
    double c[3] = {0.0, 0.0, 0.0};
    Value cur = v;
    for (double& x : c) {
        if (!is_cons(cur)) return false;
        const Value& item = cur.cons->car;
        if (item.type == Type::Int) {
            x = static_cast<double>(item.i);
        } else if (item.type == Type::Real) {
            x = item.d;
        } else {
            return false;
        }
        cur = cur.cons->cdr;
    }
    if (!is_nil(cur)) return false;  // a longer list is not a point
    out = Vec3{c[0], c[1], c[2]};
    return true;
}

}  // namespace

bool subr_getvar(Interp& in, const Value* a, std::size_t, Value& out) {
    Sysvars* sv = require_sysvars(in, "getvar");
    if (!sv) return false;

    std::string name;
    if (!require_name(in, "getvar", a[0], name)) return false;

    SysvarValue value;
    if (!sv->get(name, value)) {
        out = make_nil();  // an unknown name is a condition, not an error
        return true;
    }
    out = sysvar_to_value(in.ctx(), value);
    return true;
}

bool subr_setvar(Interp& in, const Value* a, std::size_t, Value& out) {
    Sysvars* sv = require_sysvars(in, "setvar");
    if (!sv) return false;

    std::string name;
    if (!require_name(in, "setvar", a[0], name)) return false;

    const SysvarDef* def = find_sysvar(name);
    if (!def) {
        in.fail(EvalStatus::BadArgumentType, "setvar: no such system variable: " + name);
        return false;
    }

    SysvarValue value;
    if (def->type == SysvarType::Point) {
        Vec3 p{};
        if (!list_to_point(a[1], p)) {
            in.fail(EvalStatus::BadArgumentType,
                    "setvar: " + name + " takes a point: " + prin1(a[1]));
            return false;
        }
        value = SysvarValue::of_point(p);
    } else if (!value_to_sysvar(a[1], value)) {
        in.fail(EvalStatus::BadArgumentType,
                "setvar: bad value for " + name + ": " + prin1(a[1]));
        return false;
    }

    const Sysvars::SetStatus st = sv->set(name, value);
    if (st != Sysvars::SetStatus::Ok) {
        in.fail(EvalStatus::BadArgumentType,
                "setvar: " + name + ": " + sysvar_set_status_message(st));
        return false;
    }

    // AutoLISP returns the value that was stored, which is not always the value
    // handed in: an integer written to a real variable comes back as a real.
    SysvarValue stored;
    sv->get(name, stored);
    out = sysvar_to_value(in.ctx(), stored);
    return true;
}

}  // namespace ncad::lisp
