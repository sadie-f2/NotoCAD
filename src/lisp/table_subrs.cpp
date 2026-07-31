// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "table_subrs.hpp"

#include "ncad/blocks.hpp"
#include "ncad/database.hpp"
#include "ncad/tables.hpp"

#include <string>
#include <vector>

namespace ncad::lisp {
namespace {

Value pair_int(Context& ctx, std::int32_t code, std::int32_t v) {
    return ctx.cons(make_int(code), make_int(v));
}

Value pair_real(Context& ctx, std::int32_t code, double v) {
    return ctx.cons(make_int(code), make_real(v));
}

Value pair_str(Context& ctx, std::int32_t code, std::string_view v) {
    return ctx.cons(make_int(code), make_str(ctx.new_string(v)));
}

Value pair_point(Context& ctx, std::int32_t code, const Vec3& p) {
    const Value coords[3] = {make_real(p.x), make_real(p.y), make_real(p.z)};
    return ctx.cons(make_int(code), ctx.list(coords, 3));
}

std::string upcase(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

// R12's group codes for each table record. Group 0 names the table, group 2 the
// entry, and group 70 carries the flags -- the same shape as the DXF TABLES
// section, because that is where a script's expectations come from.

Value layer_alist(Context& ctx, const Database& db, const Layer& l) {
    std::vector<Value> items;
    items.push_back(pair_str(ctx, 0, "LAYER"));
    items.push_back(pair_str(ctx, 2, l.name));

    // Bit 1 is frozen and bit 4 is locked, as R12 has them.
    std::int32_t flags = 0;
    if (l.frozen) flags |= 1;
    if (l.locked) flags |= 4;
    items.push_back(pair_int(ctx, 70, flags));

    // Negative colour means the layer is off, and the magnitude is still the
    // colour -- so this is reported as stored rather than as displayed.
    items.push_back(pair_int(ctx, 62, l.color));
    items.push_back(pair_str(ctx, 6, db.linetype(l.linetype).name));
    return ctx.list(items.data(), items.size());
}

Value linetype_alist(Context& ctx, const Linetype& lt) {
    std::vector<Value> items;
    items.push_back(pair_str(ctx, 0, "LTYPE"));
    items.push_back(pair_str(ctx, 2, lt.name));
    items.push_back(pair_int(ctx, 70, 0));
    items.push_back(pair_str(ctx, 3, lt.description));
    // 65 is 'A', R12's only alignment code.
    items.push_back(pair_int(ctx, 72, 65));
    items.push_back(pair_int(ctx, 73, static_cast<std::int32_t>(lt.pattern.size())));
    items.push_back(pair_real(ctx, 40, lt.pattern_length()));
    for (const double d : lt.pattern) items.push_back(pair_real(ctx, 49, d));
    return ctx.list(items.data(), items.size());
}

Value block_alist(Context& ctx, const BlockDef& b) {
    std::vector<Value> items;
    items.push_back(pair_str(ctx, 0, "BLOCK"));
    items.push_back(pair_str(ctx, 2, b.name));
    items.push_back(pair_int(ctx, 70, static_cast<std::int32_t>(b.flags)));
    items.push_back(pair_point(ctx, 10, b.base));
    return ctx.list(items.data(), items.size());
}

Value ucs_alist(Context& ctx, const UcsDef& u) {
    std::vector<Value> items;
    items.push_back(pair_str(ctx, 0, "UCS"));
    items.push_back(pair_str(ctx, 2, u.name));
    items.push_back(pair_int(ctx, 70, 0));
    items.push_back(pair_point(ctx, 10, u.ucs.origin));
    items.push_back(pair_point(ctx, 11, u.ucs.xdir));
    items.push_back(pair_point(ctx, 12, u.ucs.ydir));
    return ctx.list(items.data(), items.size());
}

// How many entries a table has, and the alist of one of them. Together these
// are the whole of what tblnext and tblsearch need, so the two functions differ
// only in how they choose an index rather than in how they read one.
std::size_t table_size(const Database& db, const std::string& table) {
    if (table == "LAYER") return db.layers().size();
    if (table == "LTYPE") return db.linetypes().size();
    if (table == "BLOCK") return db.blocks().size();
    if (table == "UCS") return db.ucs_table().size();
    return 0;
}

bool known_table(const std::string& table) {
    return table == "LAYER" || table == "LTYPE" || table == "BLOCK" || table == "UCS";
}

Value entry_alist(Context& ctx, const Database& db, const std::string& table, std::size_t i) {
    if (table == "LAYER") return layer_alist(ctx, db, db.layers()[i]);
    if (table == "LTYPE") return linetype_alist(ctx, db.linetypes()[i]);
    if (table == "BLOCK") {
        const BlockDef* b = db.blocks()[i].get();
        return b == nullptr ? make_nil() : block_alist(ctx, *b);
    }
    if (table == "UCS") return ucs_alist(ctx, db.ucs_table()[i]);
    return make_nil();
}

std::string entry_name(const Database& db, const std::string& table, std::size_t i) {
    if (table == "LAYER") return db.layers()[i].name;
    if (table == "LTYPE") return db.linetypes()[i].name;
    if (table == "BLOCK") {
        const BlockDef* b = db.blocks()[i].get();
        return b == nullptr ? std::string{} : b->name;
    }
    if (table == "UCS") return db.ucs_table()[i].name;
    return {};
}

bool table_args(Interp& in, const char* who, const Value& v, Database*& db, std::string& table) {
    db = in.database();
    if (db == nullptr) {
        return in.fail(EvalStatus::BadArgumentType, std::string(who) + ": no drawing is attached");
    }
    if (v.type != Type::Str) {
        return in.fail(EvalStatus::BadArgumentType,
                       std::string(who) + ": table name is not a string: " + prin1(v));
    }
    table = upcase(v.str->view());
    if (!known_table(table)) {
        // An error, not an empty walk. "No such table" and "an empty table" are
        // different answers and a script can act on the difference -- silently
        // returning nil would make a typo look like an empty drawing.
        return in.fail(EvalStatus::BadArgumentType,
                       std::string(who) + ": no such table: " + prin1(v) +
                           " (LAYER, LTYPE, BLOCK, UCS)");
    }
    return true;
}

}  // namespace

bool subr_tblsearch(Interp& in, const Value* args, std::size_t n, Value& out) {
    Database* db = nullptr;
    std::string table;
    if (!table_args(in, "tblsearch", args[0], db, table)) return false;

    if (args[1].type != Type::Str) {
        return in.fail(EvalStatus::BadArgumentType,
                       "tblsearch: entry name is not a string: " + prin1(args[1]));
    }
    const std::string wanted = upcase(args[1].str->view());

    const std::size_t count = table_size(*db, table);
    for (std::size_t i = 0; i < count; ++i) {
        // Case-insensitive, because R12's table names are, and a script that
        // wrote "walls" means the layer WALLS.
        if (upcase(entry_name(*db, table, i)) != wanted) continue;

        // A non-nil third argument sets the walk to continue from here, which is
        // R12's way of starting a tblnext in the middle rather than at the top.
        if (n >= 3 && !is_nil(args[2])) in.table_cursor(table) = i + 1;

        out = entry_alist(in.ctx(), *db, table, i);
        return true;
    }

    out = make_nil();
    return true;
}

bool subr_tblnext(Interp& in, const Value* args, std::size_t n, Value& out) {
    Database* db = nullptr;
    std::string table;
    if (!table_args(in, "tblnext", args[0], db, table)) return false;

    std::size_t& cursor = in.table_cursor(table);
    if (n >= 2 && !is_nil(args[1])) cursor = 0;  // rewind

    const std::size_t count = table_size(*db, table);
    if (cursor >= count) {
        // The end of the table. The cursor stays put rather than wrapping, so a
        // second call keeps saying nil instead of starting silently over.
        out = make_nil();
        return true;
    }

    out = entry_alist(in.ctx(), *db, table, cursor);
    ++cursor;
    return true;
}

}  // namespace ncad::lisp
