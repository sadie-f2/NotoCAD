// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/dxf.hpp"

#include "noto/database.hpp"
#include "noto/ecs.hpp"

#include <cstdio>
#include <fstream>
#include <ostream>

namespace noto {
namespace {

// DXF is CRLF-terminated. Streams are opened in binary mode so this is exact
// rather than at the mercy of the platform's newline translation.
constexpr const char* kEol = "\r\n";

std::string to_hex(Handle h) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(h));
    return buf;
}

bool is_default_normal(const Vec3& n) {
    return near_equal(n, kWorldZ, 1e-12);
}

}  // namespace

std::string dxf_real(double v) {
    char buf[64];
    // 17 significant digits round-trips an IEEE double exactly.
    std::snprintf(buf, sizeof(buf), "%.17g", v);

    std::string s(buf);
    // Guarantee it reads as a real, not an integer.
    if (s.find_first_of(".eEnN") == std::string::npos) s += ".0";
    return s;
}

void DxfWriter::code(int c, const std::string& value) {
    out_ << c << kEol << value << kEol;
}

void DxfWriter::code(int c, const char* value) {
    out_ << c << kEol << value << kEol;
}

void DxfWriter::code(int c, int value) {
    out_ << c << kEol << value << kEol;
}

void DxfWriter::code(int c, double value) {
    out_ << c << kEol << dxf_real(value) << kEol;
}

void DxfWriter::point(int base, const Vec3& p) {
    code(base, p.x);
    code(base + 10, p.y);
    code(base + 20, p.z);
}

void DxfWriter::write_common(const Entity& e) { write_common_as(e, e.type_name()); }

void DxfWriter::write_common_as(const Entity& e, const char* type_name) {
    const EntityProps& props = e.props();

    code(0, type_name);

    const LayerId lid = props.layer;
    code(8, lid < db_.layers().size() ? db_.layer(lid).name : std::string("0"));

    if (props.linetype != kLinetypeContinuous && props.linetype < db_.linetypes().size()) {
        code(6, db_.linetype(props.linetype).name);
    }
    if (props.color != kColorByLayer) {
        code(62, static_cast<int>(props.color));
    }
    if (props.thickness != 0.0) {
        code(39, props.thickness);
    }
}

std::string DxfWriter::handle_text(Handle h) const { return to_hex(h); }

std::string DxfWriter::layer_name(const Entity& e) const {
    const LayerId lid = e.props().layer;
    return lid < db_.layers().size() ? db_.layer(lid).name : std::string("0");
}

void DxfWriter::write_extrusion(const Vec3& normal) {
    if (is_default_normal(normal)) return;  // 0,0,1 is the implied default
    code(210, normal.x);
    code(220, normal.y);
    code(230, normal.z);
}

void DxfWriter::begin_section(const char* name) {
    code(0, "SECTION");
    code(2, name);
}

void DxfWriter::end_section() { code(0, "ENDSEC"); }

void DxfWriter::write_header() {
    begin_section("HEADER");

    code(9, "$ACADVER");
    code(1, "AC1009");

    // NO HANDLES. R12 made them optional -- $HANDLING defaults to 0 and most
    // R12 files carry none -- and they became mandatory only in R13.
    //
    // We used to write them, and it made AutoCAD call the file corrupt. A
    // POLYLINE's VERTEX and SEQEND records are not database entities and have no
    // handles of their own, so they were emitted carrying the PARENT'S handle:
    // a degraded ellipse wrote eighteen records all claiming to be handle 6.
    // Handles must be unique, and $HANDLING = 1 told the reader to check.
    // $HANDSEED was wrong as well -- it is the next handle to allocate and must
    // exceed every one present, and ours equalled the maximum.
    //
    // Writing them correctly would mean allocating handles for subordinate
    // records at write time and knowing the total before the header is emitted,
    // which is a two-pass write for something NOTHING READS: dxf_read ignores
    // group 5 entirely and assigns fresh handles on load. So they go. Two lines
    // saved on every record, which on a drawing of a million polyline vertices
    // is not a rounding error either.
    //
    // R13 and later require them, so a version-aware writer will have to solve
    // the allocation properly. See SF_todo.md.
    code(9, "$HANDLING");
    code(70, 0);

    code(9, "$INSBASE");
    point(10, db_.sysvars().get_point(Sysvar::InsBase));

    // The current UCS, exactly as DXF carries it: three points in world terms
    // plus a name and a flag. This is the header half of the split -- named
    // systems are a table, the current one is header state, the same division
    // $CLAYER and the LAYER table use.
    const Ucs ucs = db_.current_ucs();
    code(9, "$UCSNAME");
    code(2, db_.sysvars().get_string(Sysvar::UcsName));
    code(9, "$UCSORG");
    point(10, ucs.origin);
    code(9, "$UCSXDIR");
    point(10, ucs.xdir);
    code(9, "$UCSYDIR");
    point(10, ucs.ydir);
    code(9, "$WORLDUCS");
    code(70, ucs.is_world() ? 1 : 0);
    code(9, "$UCSFOLLOW");
    code(70, static_cast<int>(db_.sysvars().get_int(Sysvar::UcsFollow)));
    code(9, "$UCSICON");
    code(70, static_cast<int>(db_.sysvars().get_int(Sysvar::UcsIcon)));

    BBox ext = db_.extents();
    if (!ext.valid()) ext = BBox{Vec3{}, Vec3{}};
    code(9, "$EXTMIN");
    point(10, ext.min);
    code(9, "$EXTMAX");
    point(10, ext.max);

    // From the sysvars, not hardcoded: LIMITS exists, and a header that always
    // wrote the default meant setting the limits and saving lost them.
    const Vec3 limmin = db_.sysvars().get_point(Sysvar::LimMin);
    const Vec3 limmax = db_.sysvars().get_point(Sysvar::LimMax);
    code(9, "$LIMMIN");
    code(10, limmin.x);
    code(20, limmin.y);
    code(9, "$LIMMAX");
    code(10, limmax.x);
    code(20, limmax.y);

    end_section();
}

void DxfWriter::write_tables() {
    begin_section("TABLES");

    // LTYPE must precede LAYER, since layers reference linetypes by name.
    code(0, "TABLE");
    code(2, "LTYPE");
    code(70, static_cast<int>(db_.linetypes().size()));
    for (const Linetype& lt : db_.linetypes()) {
        code(0, "LTYPE");
        code(2, lt.name);
        code(70, 0);
        code(3, lt.description);
        code(72, 65);  // 'A', the only alignment R12 defines
        code(73, static_cast<int>(lt.pattern.size()));
        double total = 0.0;
        for (const double d : lt.pattern) total += (d < 0.0) ? -d : d;
        code(40, total);
        for (const double d : lt.pattern) code(49, d);
    }
    code(0, "ENDTAB");

    code(0, "TABLE");
    code(2, "LAYER");
    code(70, static_cast<int>(db_.layers().size()));
    for (const Layer& ly : db_.layers()) {
        code(0, "LAYER");
        code(2, ly.name);
        code(70, ly.frozen ? 1 : 0);
        code(62, static_cast<int>(ly.color));
        code(6, ly.linetype < db_.linetypes().size() ? db_.linetype(ly.linetype).name
                                                     : std::string("CONTINUOUS"));
    }
    code(0, "ENDTAB");

    // Named coordinate systems. Written even when empty, because a reader that
    // expects the table and does not find it is a worse failure than an empty
    // one -- and because an empty table is the honest report for a drawing
    // that has saved none.
    code(0, "TABLE");
    code(2, "UCS");
    code(70, static_cast<int>(db_.ucs_table().size()));
    for (const UcsDef& def : db_.ucs_table()) {
        if (def.name.empty()) continue;  // deleted by UCS Del
        code(0, "UCS");
        code(2, def.name);
        code(70, 0);
        point(10, def.ucs.origin);
        point(11, def.ucs.xdir);
        point(12, def.ucs.ydir);
    }
    code(0, "ENDTAB");

    // A STANDARD text style and the ACAD application id: both are expected to
    // exist by most readers even when nothing references them yet.
    code(0, "TABLE");
    code(2, "STYLE");
    code(70, 1);
    code(0, "STYLE");
    code(2, "STANDARD");
    code(70, 0);
    code(40, 0.0);
    code(41, 1.0);
    code(50, 0.0);
    code(71, 0);
    code(42, 0.2);
    code(3, "txt");
    code(4, "");
    code(0, "ENDTAB");

    code(0, "TABLE");
    code(2, "APPID");
    code(70, 1);
    code(0, "APPID");
    code(2, "ACAD");
    code(70, 0);
    code(0, "ENDTAB");

    end_section();
}

void DxfWriter::write_blocks() {
    begin_section("BLOCKS");

    // Each definition is a BLOCK record, its entities, and an ENDBLK. The
    // entities are written by the same dxf_write() the drawing uses, because a
    // block's contents are ordinary entities that merely live somewhere else --
    // which is also why a block can contain an INSERT with no extra work.
    for (const std::unique_ptr<BlockDef>& def : db_.blocks()) {
        if (!def) continue;

        code(0, "BLOCK");
        code(8, "0");
        code(2, def->name);
        code(70, static_cast<int>(def->flags));
        point(10, def->base);
        // R12 repeats the name in group 3. Readers differ on which they trust,
        // so both are written.
        code(3, def->name);

        for (const EntityPtr& e : def->entities) {
            if (e) e->dxf_write(*this);
        }

        code(0, "ENDBLK");
        code(8, "0");
    }

    end_section();
}

void DxfWriter::write_entities() {
    begin_section("ENTITIES");
    for (const Handle h : db_.order()) {
        if (const Entity* e = db_.get(h)) e->dxf_write(*this);
    }
    end_section();
}

void DxfWriter::write_document() {
    write_header();
    write_tables();
    write_blocks();
    write_entities();
    code(0, "EOF");
}

bool write_dxf_file(const Database& db, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    DxfWriter w(out, db);
    w.write_document();
    return out.good();
}

}  // namespace noto
