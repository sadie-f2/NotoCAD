// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/dxf.hpp"

#include "ncad/database.hpp"
#include "ncad/ecs.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <streambuf>
#include <ostream>

namespace ncad {
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

const char* dxf_version_name(DxfVersion v) {
    switch (v) {
        case DxfVersion::R12: return "AC1009";
        case DxfVersion::R2000: return "AC1015";
    }
    return "AC1009";
}

const char* dxf_version_label(DxfVersion v) {
    switch (v) {
        case DxfVersion::R12: return "R12";
        case DxfVersion::R2000: return "R2000";
    }
    return "R12";
}

DxfVersion dxf_version_from_name(const std::string& name) {
    std::string up;
    for (const char c : name) up.push_back((c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c);
    if (up == "R2000" || up == "AC1015" || up == "2000") return DxfVersion::R2000;
    return DxfVersion::R12;
}

bool dxf_has_modern_entities(DxfVersion v) { return v != DxfVersion::R12; }
bool dxf_requires_handles(DxfVersion v) { return v != DxfVersion::R12; }

std::string DxfWriter::next_handle() { return to_hex(next_handle_++); }

void DxfWriter::subclass(const char* name) {
    // R12 has no subclass markers at all, and emitting one there would be a
    // group code the reader does not expect.
    if (!dxf_requires_handles(version_)) return;
    code(100, name);
}

namespace {

const char* primary_subclass(const char* type_name) {
    // The AcDb class a record declares itself to be, after the AcDbEntity every
    // entity shares. AutoCAD refuses a file whose entity lacks it: "Class
    // separator for class AcDbLine expected".
    //
    // Keyed on the DXF TYPE NAME rather than on our entity enum, and that is
    // the load-bearing part: an ELLIPSE degrading to R12 goes out under
    // POLYLINE's name, and it is an AcDb2dPolyline while it is doing so.
    // Keying on the enum would have labelled that record AcDbEllipse and
    // produced a file describing an entity that is not there.
    //
    // Two of these are only the FIRST marker of a chain -- an ARC is an
    // AcDbCircle that then declares AcDbArc, and a VERTEX an AcDbVertex that
    // then declares its concrete kind. The rest of each chain is emitted by the
    // writer that knows where in the record it belongs.
    struct Map {
        const char* type;
        const char* subclass;
    };
    static const Map kMap[] = {
        {"LINE", "AcDbLine"},         {"CIRCLE", "AcDbCircle"},
        {"ARC", "AcDbCircle"},        {"POINT", "AcDbPoint"},
        {"TEXT", "AcDbText"},         {"SOLID", "AcDbTrace"},
        {"TRACE", "AcDbTrace"},       {"3DFACE", "AcDbFace"},
        {"POLYLINE", "AcDb2dPolyline"}, {"VERTEX", "AcDbVertex"},
        {"INSERT", "AcDbBlockReference"}, {"ELLIPSE", "AcDbEllipse"},
        {"SPLINE", "AcDbSpline"},     {"MTEXT", "AcDbMText"},
    };
    for (const Map& m : kMap) {
        if (std::strcmp(type_name, m.type) == 0) return m.subclass;
    }
    // SEQEND declares no class of its own, and a Proxy writes back the groups
    // it arrived with rather than being rebuilt from one. Both are correct as
    // nothing.
    return nullptr;
}

}  // namespace

void DxfWriter::write_common(const Entity& e) { write_common_as(e, e.type_name()); }

void DxfWriter::write_common_as(const Entity& e, const char* type_name) {
    const EntityProps& props = e.props();

    code(0, type_name);

    // R13 and later: a unique handle, an owner, and the subclass chain. The
    // owner is the model-space block record, whose handle is fixed before any
    // entity is written -- an entity owned by nothing is rejected.
    if (dxf_requires_handles(version_)) {
        last_handle_ = next_handle();
        code(5, last_handle_);
        if (!model_space_owner_.empty()) code(330, model_space_owner_);
        subclass("AcDbEntity");
        if (const char* sc = primary_subclass(type_name)) subclass(sc);
    }

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

void DxfWriter::write_subrecord(const char* type_name, const Entity& parent,
                                const std::string& owner) {
    code(0, type_name);

    // A VERTEX or a SEQEND is not a database entity and owns no handle of its
    // own, but R13 and later require one on EVERY record and require it to be
    // unique -- giving them the parent's is exactly what made AutoCAD call our
    // R12 files corrupt. They take a fresh handle here and name the parent as
    // their owner, which is the relationship the file is meant to express.
    if (dxf_requires_handles(version_)) {
        code(5, next_handle());
        if (!owner.empty()) code(330, owner);
        subclass("AcDbEntity");
        if (const char* sc = primary_subclass(type_name)) subclass(sc);
        // The concrete vertex kind, after the abstract one. These polylines are
        // all 2D -- ECS coordinates with bulges -- so the header goes out as
        // AcDb2dPolyline and its vertices must agree with it.
        if (std::strcmp(type_name, "VERTEX") == 0) subclass("AcDb2dVertex");
    }

    // Layer only: a subordinate record inherits everything else from the
    // parent, and R12 wrote no more than this.
    code(8, layer_name(parent));
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
    code(1, dxf_version_name(version_));

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
    if (dxf_requires_handles(version_)) {
        // R13 and later require handles, and $HANDSEED must clear every one
        // issued -- which is only known after the document has been written, so
        // write_dxf_* runs a counting pass first and hands the answer back here.
        code(9, "$HANDSEED");
        code(5, to_hex(seed_hint_ != 0 ? seed_hint_ : 0xFFFF));
    } else {
        code(9, "$HANDLING");
        code(70, 0);
    }

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
    // R2000 requires the two special entries to be present by name -- "Missing
    // Default entry ByLayer in SymbolTable:LTYPE" and the whole file is
    // refused. They are not linetypes a drawing owns; they are the two values
    // an entity's linetype can take INSTEAD of naming one, so the database has
    // no reason to hold them and they are synthesised here.
    const int extra_linetypes = dxf_requires_handles(version_) ? 2 : 0;
    const std::string lt_owner =
        begin_table("LTYPE", static_cast<int>(db_.linetypes().size()) + extra_linetypes);

    if (dxf_requires_handles(version_)) {
        for (const char* name : {"ByBlock", "ByLayer"}) {
            table_record("LTYPE", lt_owner, "AcDbLinetypeTableRecord");
            code(2, name);
            code(70, 0);
            code(3, "");
            code(72, 65);
            code(73, 0);
            code(40, 0.0);
        }
    }

    for (const Linetype& lt : db_.linetypes()) {
        table_record("LTYPE", lt_owner, "AcDbLinetypeTableRecord");
        code(2, lt.name);
        code(70, 0);
        code(3, lt.description);
        code(72, 65);  // 'A', the only alignment R12 defines
        code(73, static_cast<int>(lt.pattern.size()));
        double total = 0.0;
        for (const double d : lt.pattern) total += (d < 0.0) ? -d : d;
        code(40, total);
        for (const double d : lt.pattern) {
            code(49, d);
            // R13 and later want the element TYPE after every dash length, and
            // AutoCAD rejects the whole drawing without it -- "Missing group
            // code 49 in complex linetype", which names the group that IS
            // present rather than the one that is not, because the reader is
            // still looking for the end of the previous element when it meets
            // the next 49.
            //
            // Zero means a plain dash: no embedded shape and no embedded text,
            // which is every linetype here. A non-zero 74 would bring 75 and a
            // style pointer with it, and nothing generates one -- R12's acad.lin
            // complex linetypes are not loaded and there is no SHX path.
            //
            // AC1009 has no 74 at all, so this is version-gated rather than
            // written always. The R12 output is confirmed good in AutoCAD and
            // must not acquire a group the revision does not define.
            if (dxf_requires_handles(version_)) code(74, 0);
        }
    }
    code(0, "ENDTAB");

    const std::string la_owner = begin_table("LAYER", static_cast<int>(db_.layers().size()));
    for (const Layer& ly : db_.layers()) {
        table_record("LAYER", la_owner, "AcDbLayerTableRecord");
        code(2, ly.name);
        code(70, ly.frozen ? 1 : 0);
        code(62, static_cast<int>(ly.color));
        code(6, ly.linetype < db_.linetypes().size() ? db_.linetype(ly.linetype).name
                                                     : std::string("CONTINUOUS"));
        if (dxf_requires_handles(version_)) {
            // Lineweight, then the plot style. AutoCAD refuses the whole file
            // without 390 -- "Did not receive PlotStyleName" -- and the handle
            // must resolve to a real object, which is why the placeholder in
            // the OBJECTS section exists.
            code(370, -3);  // -3 is "default", R2000's BYLAYER equivalent
            code(390, plotstyle_normal_);
        }
    }
    code(0, "ENDTAB");

    // Named coordinate systems. Written even when empty, because a reader that
    // expects the table and does not find it is a worse failure than an empty
    // one -- and because an empty table is the honest report for a drawing
    // that has saved none.
    const std::string ucs_owner = begin_table("UCS", static_cast<int>(db_.ucs_table().size()));
    for (const UcsDef& def : db_.ucs_table()) {
        if (def.name.empty()) continue;  // deleted by UCS Del
        table_record("UCS", ucs_owner, "AcDbUCSTableRecord");
        code(2, def.name);
        code(70, 0);
        point(10, def.ucs.origin);
        point(11, def.ucs.xdir);
        point(12, def.ucs.ydir);
    }
    code(0, "ENDTAB");

    // A STANDARD text style and the ACAD application id: both are expected to
    // exist by most readers even when nothing references them yet.
    const std::string st_owner = begin_table("STYLE", 1);
    table_record("STYLE", st_owner, "AcDbTextStyleTableRecord");
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

    const std::string app_owner = begin_table("APPID", 1);
    table_record("APPID", app_owner, "AcDbRegAppTableRecord");
    code(2, "ACAD");
    code(70, 0);
    code(0, "ENDTAB");

    // The rest exist only for R13 and later, which expect them present even when
    // a drawing has nothing to put in them.
    if (dxf_requires_handles(version_)) {
        const std::string vp_owner = begin_table("VPORT", 1);
        table_record("VPORT", vp_owner, "AcDbViewportTableRecord");
        code(2, "*ACTIVE");
        code(70, 0);
        point(10, Vec3{0.0, 0.0, 0.0});
        point(11, Vec3{1.0, 1.0, 0.0});
        point(12, Vec3{0.0, 0.0, 0.0});
        code(40, 1.0);
        code(41, 1.0);
        code(0, "ENDTAB");

        const std::string vw_owner = begin_table("VIEW", 0);
        (void)vw_owner;
        code(0, "ENDTAB");

        // DIMSTYLE is the one table whose HEADER carries a second subclass
        // marker of its own, plus a count in group 71. Without it AutoCAD
        // refuses the file: "Class separator for class AcDbDimStyleTable
        // expected". Every other table stops at AcDbSymbolTable.
        const std::string dim_owner = begin_table("DIMSTYLE", 1);
        subclass("AcDbDimStyleTable");
        code(71, 1);
        table_record("DIMSTYLE", dim_owner, "AcDbDimStyleTableRecord", 105);
        code(2, "STANDARD");
        code(70, 0);
        code(0, "ENDTAB");

        write_block_records();
    }

    end_section();
}

void DxfWriter::write_blocks() {
    begin_section("BLOCKS");

    // Each definition is a BLOCK record, its entities, and an ENDBLK. The
    // entities are written by the same dxf_write() the drawing uses, because a
    // block's contents are ordinary entities that merely live somewhere else --
    // which is also why a block can contain an INSERT with no extra work.
    // R13 and later require the two layout blocks to exist even when empty --
    // model space is where every ordinary entity lives, and a reader looks for
    // it by name.
    if (dxf_requires_handles(version_)) {
        for (const char* name : {"*Model_Space", "*Paper_Space"}) {
            code(0, "BLOCK");
            code(5, next_handle());
            code(330, model_space_owner_);
            subclass("AcDbEntity");
            code(8, "0");
            subclass("AcDbBlockBegin");
            code(2, name);
            code(70, 0);
            point(10, Vec3{0.0, 0.0, 0.0});
            code(3, name);
            code(1, "");
            code(0, "ENDBLK");
            code(5, next_handle());
            code(330, model_space_owner_);
            subclass("AcDbEntity");
            code(8, "0");
            subclass("AcDbBlockEnd");
        }
    }

    for (const std::unique_ptr<BlockDef>& def : db_.blocks()) {
        if (!def) continue;

        code(0, "BLOCK");
        if (dxf_requires_handles(version_)) {
            code(5, next_handle());
            code(330, model_space_owner_);
            subclass("AcDbEntity");
        }
        code(8, "0");
        subclass("AcDbBlockBegin");
        code(2, def->name);
        code(70, static_cast<int>(def->flags));
        point(10, def->base);
        // R12 repeats the name in group 3. Readers differ on which they trust,
        // so both are written.
        code(3, def->name);
        if (dxf_requires_handles(version_)) code(1, "");

        for (const EntityPtr& e : def->entities) {
            if (e) e->dxf_write(*this);
        }

        code(0, "ENDBLK");
        if (dxf_requires_handles(version_)) {
            code(5, next_handle());
            code(330, model_space_owner_);
            subclass("AcDbEntity");
        }
        code(8, "0");
        subclass("AcDbBlockEnd");
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
    // Reserved before anything references them; see the members for why.
    if (dxf_requires_handles(version_)) {
        root_dict_ = next_handle();
        plotstyle_dict_ = next_handle();
        plotstyle_normal_ = next_handle();
    }

    write_header();
    write_classes();
    write_tables();
    write_blocks();
    write_entities();
    write_objects();
    code(0, "EOF");
}

std::string DxfWriter::begin_table(const char* name, int count) {
    code(0, "TABLE");
    code(2, name);

    std::string owner;
    if (dxf_requires_handles(version_)) {
        owner = next_handle();
        code(5, owner);
        // Handle 0 is the document itself: the tables hang off the root.
        code(330, "0");
        subclass("AcDbSymbolTable");
    }
    code(70, count);
    return owner;
}

void DxfWriter::table_record(const char* type, const std::string& owner,
                             const char* record_subclass, int handle_code) {
    code(0, type);
    if (dxf_requires_handles(version_)) {
        code(handle_code, next_handle());
        code(330, owner);
        subclass("AcDbSymbolTableRecord");
        subclass(record_subclass);
    }
}

void DxfWriter::write_classes() {
    // R12 has no CLASSES section. Later versions expect one, and an empty one is
    // correct here: it describes application-defined classes, and this program
    // defines none.
    if (!dxf_requires_handles(version_)) return;
    begin_section("CLASSES");
    end_section();
}

void DxfWriter::write_block_records() {
    if (!dxf_requires_handles(version_)) return;

    const std::size_t count = db_.blocks().size() + 2;  // + model and paper space
    const std::string owner = begin_table("BLOCK_RECORD", static_cast<int>(count));

    // Model space first, and its handle is kept: every entity in the drawing
    // names it as owner, and an entity owned by nothing is rejected.
    table_record("BLOCK_RECORD", owner, "AcDbBlockTableRecord");
    model_space_owner_ = to_hex(next_handle_ - 1);
    code(2, "*Model_Space");
    code(70, 0);

    table_record("BLOCK_RECORD", owner, "AcDbBlockTableRecord");
    code(2, "*Paper_Space");
    code(70, 0);

    for (const auto& b : db_.blocks()) {
        if (!b) continue;
        table_record("BLOCK_RECORD", owner, "AcDbBlockTableRecord");
        code(2, b->name);
        code(70, 0);
    }
    code(0, "ENDTAB");
}

void DxfWriter::write_objects() {
    if (!dxf_requires_handles(version_)) return;

    begin_section("OBJECTS");

    // The root dictionary, holding the one entry R2000 cannot do without.
    code(0, "DICTIONARY");
    code(5, root_dict_);
    code(330, "0");
    subclass("AcDbDictionary");
    code(281, 1);
    code(3, "ACAD_PLOTSTYLENAME");
    code(350, plotstyle_dict_);

    // A dictionary WITH A DEFAULT: group 340 names the entry to fall back to,
    // which is what makes an unset plot style resolve rather than dangle.
    code(0, "ACDBDICTIONARYWDFLT");
    code(5, plotstyle_dict_);
    code(330, root_dict_);
    subclass("AcDbDictionary");
    code(281, 1);
    code(3, "Normal");
    code(350, plotstyle_normal_);
    subclass("AcDbDictionaryWithDefault");
    code(340, plotstyle_normal_);

    // The object every layer's group 390 points at. A placeholder is exactly
    // what AutoCAD writes here: the plot style has no content, it only needs to
    // be a thing that exists and can be referenced.
    code(0, "ACDBPLACEHOLDER");
    code(5, plotstyle_normal_);
    code(330, plotstyle_dict_);

    end_section();
}

namespace {

// Discards everything. Used for the counting pass below.
class NullBuf final : public std::streambuf {
protected:
    int overflow(int c) override { return c; }
};

}  // namespace

Handle dxf_count_handles(const Database& db, DxfVersion version) {
    // A version that needs handles needs $HANDSEED in the HEADER, which is
    // written first -- and the seed is one past every handle issued, which is
    // known only at the end. So the document is written once to nowhere to
    // count, then again for real.
    //
    // Two passes rather than buffering the output: a drawing that fills a
    // gigabyte should not need a second gigabyte of memory to be saved, and the
    // pass is cheap next to the I/O it avoids buffering.
    if (!dxf_requires_handles(version)) return 0;

    NullBuf nb;
    std::ostream sink(&nb);
    DxfWriter counter(sink, db, version);
    counter.write_document();
    return counter.handle_seed();
}

bool write_dxf_file(const Database& db, const std::string& path, DxfVersion version) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    DxfWriter w(out, db, version);
    w.set_handle_seed_hint(dxf_count_handles(db, version));
    w.write_document();
    return out.good();
}

}  // namespace ncad
