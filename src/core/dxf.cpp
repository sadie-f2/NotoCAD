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

void DxfWriter::write_common(const Entity& e) {
    const EntityProps& props = e.props();

    code(0, e.type_name());
    code(5, to_hex(e.handle()));

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

    code(9, "$HANDLING");
    code(70, 1);
    code(9, "$HANDSEED");
    code(5, to_hex(db_.peek_next_handle()));

    code(9, "$INSBASE");
    point(10, Vec3{});

    BBox ext = db_.extents();
    if (!ext.valid()) ext = BBox{Vec3{}, Vec3{}};
    code(9, "$EXTMIN");
    point(10, ext.min);
    code(9, "$EXTMAX");
    point(10, ext.max);

    code(9, "$LIMMIN");
    code(10, 0.0);
    code(20, 0.0);
    code(9, "$LIMMAX");
    code(10, 12.0);
    code(20, 9.0);

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
    // Empty until INSERT and block definitions land.
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
