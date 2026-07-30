// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// ssget and the selection-set accessors.
//
// The non-interactive half of R12's selection vocabulary: every mode here
// builds a set from arguments rather than by asking the user to point at
// things. That is deliberate scope, not an omission -- see the note at
// subr_ssget -- and it is the half this project's own workflow needs, since
// generating geometry from analysis data never involves a cursor.
//
// Modes: "X" everything, "P" previous, "L" last, "W" window, "C" crossing,
// each optionally filtered. R12 also has "F" fence, which is not here yet.
#include "sset_subrs.hpp"

#include "entity_subrs.hpp"
#include "wildcard.hpp"

#include "noto/command.hpp"
#include "noto/database.hpp"
#include "noto/pick.hpp"
#include "noto/render.hpp"
#include "noto/scene.hpp"
#include "noto/selection.hpp"

#include <string>

namespace noto::lisp {
namespace {

Database* require_db(Interp& in, const char* who) {
    Database* db = in.database();
    if (!db) in.fail(EvalStatus::BadArgumentType, std::string(who) + ": no drawing is attached");
    return db;
}

// The set a value names, or null with the error already raised.
SelectionSet* require_sset(Interp& in, const char* who, const Value& v) {
    if (v.type != Type::Sset) {
        in.fail(EvalStatus::BadArgumentType,
                std::string(who) + ": not a selection set: " + prin1(v));
        return nullptr;
    }
    SelectionSet* s = in.selection_set(v.sset);
    if (!s) {
        in.fail(EvalStatus::BadArgumentType, std::string(who) + ": stale selection set");
    }
    return s;
}

bool require_ename_arg(Interp& in, const char* who, const Value& v, Handle& out) {
    if (v.type != Type::Ename) {
        in.fail(EvalStatus::BadArgumentType,
                std::string(who) + ": not an entity name: " + prin1(v));
        return false;
    }
    out = v.ename;
    return true;
}

bool point_from(const Value& v, Vec3& out) {
    double c[3] = {0, 0, 0};
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
    // A two-element point is legal in AutoLISP; z defaults to zero. The loop
    // above needs three, so this is the shorter form's second chance.
    return is_nil(cur) ? (out = Vec3{c[0], c[1], c[2]}, true) : false;
}

bool point2_from(const Value& v, Vec3& out) {
    if (point_from(v, out)) return true;
    if (!is_cons(v)) return false;
    double c[2] = {0, 0};
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
    if (!is_nil(cur)) return false;
    out = Vec3{c[0], c[1], 0.0};
    return true;
}

std::string upcase(std::string_view s) {
    std::string r(s);
    for (char& c : r) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return r;
}

// A screen-aligned box from two corners, on world XY. Selection windows are a
// view-space idea and AutoLISP has no view, so the world axes are the only
// sensible frame -- and they are the right one in plan view, which is where
// scripted selection is meaningful anyway.
SelectionRegion region_from(const Vec3& a, const Vec3& b) {
    SelectionRegion r;
    r.origin = a;
    const Vec3 d = b - a;

    Vec3 ax{1, 0, 0};
    Vec3 ay{0, 1, 0};
    double u = d.x;
    double v = d.y;
    if (u < 0.0) {
        ax = ax * -1.0;
        u = -u;
    }
    if (v < 0.0) {
        ay = ay * -1.0;
        v = -v;
    }
    r.ax = ax;
    r.ay = ay;
    r.width = u;
    r.height = v;
    return r;
}

}  // namespace

// (ssget) with no arguments would prompt the user, which needs the interpreter
// to ask a question mid-evaluation -- see SF_todo.md. Every mode here builds a

// --- filter lists -----------------------------------------------------------
//
// ((0 . "LINE") (8 . "WALLS")) -- every pair must match, which is R12's rule
// and the reason there is no explicit AND. String values carry wildcards and
// comma alternatives, so (0 . "LINE,ARC") and (8 . "WALL*") both work.
//
// Matching is done against the alist ENTGET WOULD RETURN, not against the
// entity directly. It costs a little per candidate and it buys the one property
// that matters: a filter cannot select something entget then describes
// differently, which would have a script acting on an entity it never saw.
//
// R13's -4 operators -- "<OR", ">=", "<AND" and the rest -- are not here. They
// are a small language of their own and R12 has none of them.
namespace {

bool filter_value_matches(const Value& want, const Value& got) {
    if (want.type == Type::Str) {
        if (got.type != Type::Str) return false;
        // Case-folded, because an ssget filter is: (8 . "walls") finds layer
        // WALLS. Standalone wcmatch is not, and both behaviours are AutoCAD's.
        return wildcard_match(got.str->view(), want.str->view(), true);
    }
    if (is_number(want)) {
        if (!is_number(got)) return false;
        return as_double(want) == as_double(got);
    }
    return equal(want, got);
}

// True when the entity satisfies every pair. A code appearing more than once in
// the alist -- a polyline's vertices, a spline's knots -- matches if ANY of them
// does, which is what "has a 10 group equal to this" has to mean.
bool entity_matches_filter(Context& ctx, const Database& db, const Entity& e,
                           const Value& filter) {
    const Value alist = entity_to_alist(ctx, db, e);

    for (Value f = filter; is_cons(f); f = cdr(f)) {
        const Value pair = car(f);
        if (!is_cons(pair)) return false;

        const Value code = car(pair);
        const Value want = cdr(pair);
        if (!is_number(code)) return false;
        const double wanted_code = as_double(code);

        bool ok = false;
        for (Value g = alist; is_cons(g); g = cdr(g)) {
            const Value entry = car(g);
            if (!is_cons(entry) || !is_number(car(entry))) continue;
            if (as_double(car(entry)) != wanted_code) continue;
            if (filter_value_matches(want, cdr(entry))) {
                ok = true;
                break;
            }
        }
        if (!ok) return false;
    }
    return true;
}

// A filter is a list whose first element is a dotted pair. That is how it is
// told apart from a point, which is a list of numbers -- and points are the
// only other list ssget takes.
bool looks_like_filter(const Value& v) { return is_cons(v) && is_cons(car(v)); }

}  // namespace

// set from what it was given instead.
bool subr_ssget(Interp& in, const Value* a, std::size_t argc, Value& out) {
    Database* db = require_db(in, "ssget");
    if (!db) return false;

    if (argc == 0) {
        in.fail(EvalStatus::BadArgumentType,
                "ssget: interactive selection is not implemented; use \"X\", \"P\", \"L\", "
                "\"W\" or \"C\"");
        return false;
    }
    if (a[0].type != Type::Str) {
        in.fail(EvalStatus::BadArgumentType, "ssget: mode must be a string: " + prin1(a[0]));
        return false;
    }

    const std::string mode = upcase(a[0].str->view());
    SelectionSet set;
    const DrawContext ctx{};

    // The filter is always last, whatever the mode took before it.
    Value filter = make_nil();
    if (argc >= 2 && looks_like_filter(a[argc - 1])) filter = a[argc - 1];

    if (mode == "X") {
        for (const Handle h : db->order()) {
            const Entity* e = db->get(h);
            // Unlike the ALL keyword at a prompt, "X" takes everything: it is a
            // database query, not a selection a user could have made by
            // pointing, and filtering it would hide entities from a script that
            // asked for all of them.
            if (e) set.add(h);
        }
    } else if (mode == "P") {
        CommandEngine* engine = in.command_engine();
        if (engine) {
            for (const Handle h : engine->previous_selection().handles()) {
                if (db->get(h)) set.add(h);
            }
        }
    } else if (mode == "L") {
        const Handle h = db->last();
        if (h != kNullHandle) set.add(h);
    } else if (mode == "W" || mode == "C") {
        if (argc < 3) {
            in.fail(EvalStatus::BadArgumentType, "ssget: " + mode + " needs two corner points");
            return false;
        }
        Vec3 p0{};
        Vec3 p1{};
        if (!point2_from(a[1], p0) || !point2_from(a[2], p1)) {
            in.fail(EvalStatus::BadArgumentType, "ssget: corners must be points");
            return false;
        }
        select_by_region(*db, ctx, region_from(p0, p1), mode == "C", set);
    } else {
        in.fail(EvalStatus::BadArgumentType, "ssget: unknown mode \"" + mode + "\"");
        return false;
    }

    if (!is_nil(filter)) {
        SelectionSet kept;
        for (const Handle h : set.handles()) {
            const Entity* e = db->get(h);
            if (e != nullptr && entity_matches_filter(in.ctx(), *db, *e, filter)) kept.add(h);
        }
        set = std::move(kept);
    }

    // R12 returns nil for an empty selection, and LISP tests for it.
    if (set.empty()) {
        out = make_nil();
        return true;
    }
    out = make_sset(in.new_selection_set(std::move(set)));
    return true;
}

bool subr_ssadd(Interp& in, const Value* a, std::size_t argc, Value& out) {
    // (ssadd) with no arguments makes an empty set, which is how a script builds
    // one up entity by entity.
    if (argc == 0) {
        out = make_sset(in.new_selection_set(SelectionSet{}));
        return true;
    }

    Handle h = kNullHandle;
    if (!require_ename_arg(in, "ssadd", a[0], h)) return false;

    if (argc == 1) {
        SelectionSet set;
        set.add(h);
        out = make_sset(in.new_selection_set(std::move(set)));
        return true;
    }

    SelectionSet* set = require_sset(in, "ssadd", a[1]);
    if (!set) return false;
    set->add(h);
    out = a[1];  // the same set, modified in place, as AutoLISP does
    return true;
}

bool subr_ssdel(Interp& in, const Value* a, std::size_t, Value& out) {
    Handle h = kNullHandle;
    if (!require_ename_arg(in, "ssdel", a[0], h)) return false;

    SelectionSet* set = require_sset(in, "ssdel", a[1]);
    if (!set) return false;

    // R12 returns nil when the entity was not in the set, which is how you test
    // whether the removal did anything.
    out = set->remove(h) ? a[1] : make_nil();
    return true;
}

bool subr_sslength(Interp& in, const Value* a, std::size_t, Value& out) {
    const SelectionSet* set = require_sset(in, "sslength", a[0]);
    if (!set) return false;
    out = make_int(static_cast<std::int32_t>(set->size()));
    return true;
}

bool subr_ssname(Interp& in, const Value* a, std::size_t, Value& out) {
    const SelectionSet* set = require_sset(in, "ssname", a[0]);
    if (!set) return false;
    if (a[1].type != Type::Int) {
        in.fail(EvalStatus::BadArgumentType, "ssname: index must be an integer");
        return false;
    }
    const std::int32_t i = a[1].i;
    // Out of range is nil, not an error: walking a set until ssname returns nil
    // is the idiomatic loop.
    if (i < 0 || static_cast<std::size_t>(i) >= set->size()) {
        out = make_nil();
        return true;
    }
    out = make_ename(set->handles()[static_cast<std::size_t>(i)]);
    return true;
}

bool subr_ssmemb(Interp& in, const Value* a, std::size_t, Value& out) {
    Handle h = kNullHandle;
    if (!require_ename_arg(in, "ssmemb", a[0], h)) return false;

    const SelectionSet* set = require_sset(in, "ssmemb", a[1]);
    if (!set) return false;

    out = set->contains(h) ? a[0] : make_nil();
    return true;
}

}  // namespace noto::lisp
