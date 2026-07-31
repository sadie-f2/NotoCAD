// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The DXF writer, targeting a chosen version.
//
// Native and in-tree by design: R12 DXF is small, text, and fully documented, and
// it is this project's actual interchange path. DWG stays an optional import-only
// module so its licence never reaches the core.
//
// ONE WRITER WITH A VERSION, not two writers. Every entity already knows how to
// write itself at R12; the version picks between that and a native form where a
// later one exists. `write_common_as` is the seam -- it is how ELLIPSE writes
// itself under POLYLINE's name at R12 -- so the branch lives in one place per
// entity rather than in a parallel file that would drift.
#pragma once

#include "noto/entity.hpp"
#include "noto/vec3.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>

namespace noto {

class Database;

// The DXF versions this writes.
//
// AC1009 is R12 and remains the interchange guarantee: everything can read it,
// and a divergence must degrade honestly into it.
//
// AC1015 is R2000, and it is the first version that can NAME what the database
// actually holds -- ELLIPSE, SPLINE, MTEXT and LWPOLYLINE are all real entities
// there. That makes a save lossless where R12 makes it lossy, and it is smaller:
// a spline is fifteen numbers rather than a seventeen-vertex tessellation.
//
// Nothing between them is offered. AC1012/AC1014 add little that AC1015 does not,
// and versions after it differ mostly in DWG rather than in DXF entity content --
// so a longer menu would be a longer menu of the same file.
enum class DxfVersion : std::uint8_t {
    R12,    // AC1009
    R2000,  // AC1015
};

const char* dxf_version_name(DxfVersion v);   // "AC1009"
const char* dxf_version_label(DxfVersion v);  // "R12", for prompts

// True when the version names entities R12 cannot: ELLIPSE, SPLINE, MTEXT,
// LWPOLYLINE. Entities branch on this rather than on the enum, so adding a
// version later does not mean revisiting every entity.
bool dxf_has_modern_entities(DxfVersion v);

// True when the version requires a handle on every record. R12 made them
// optional and we write none there; R13 and later make them mandatory.
bool dxf_requires_handles(DxfVersion v);

// Parses "R12" or "R2000", case-insensitively, and also accepts the AC names.
// Falls back to R12 for anything unrecognised, because the interchange
// guarantee is the safe answer to a value nobody meant.
DxfVersion dxf_version_from_name(const std::string& name);

// Formats a double the way DXF wants it: full round-trip precision, and always
// carrying a decimal point so strict readers don't take it for an integer.
std::string dxf_real(double v);

class DxfWriter {
public:
    DxfWriter(std::ostream& out, const Database& db, DxfVersion version = DxfVersion::R12)
        : out_(out), db_(db), version_(version) {}

    DxfVersion version() const { return version_; }

    // Allocates the next handle, as hex. Only meaningful for a version that
    // requires them; at R12 nothing calls it.
    //
    // Handles must be unique across the whole document INCLUDING subordinate
    // records -- a POLYLINE's VERTEX and SEQEND are not database entities and
    // have none of their own, and giving them the parent's is what made AutoCAD
    // reject our R12 files. They get their own from here.
    std::string next_handle();

    // The value $HANDSEED must carry: one past every handle issued. Known only
    // after the document has been written, which is why writing a version that
    // needs handles takes two passes -- see write_dxf_file.
    Handle handle_seed() const { return next_handle_; }
    void set_handle_seed_hint(Handle h) { seed_hint_ = h; }

    // The owner of everything in model space, as group 330. Empty at R12.
    const std::string& model_space_owner() const { return model_space_owner_; }

    // Emits group 100 subclass markers, which R13 and later require and R12 has
    // no concept of. A no-op at R12, so entities may call it unconditionally.
    void subclass(const char* name);

    // Writes a complete DXF document: HEADER, TABLES, BLOCKS, ENTITIES, EOF.
    void write_document();

    // --- group code emitters, called from Entity::dxf_write ------------------

    void code(int c, const std::string& value);
    void code(int c, const char* value);
    void code(int c, int value);
    void code(int c, double value);

    // Emits a 3D point as codes base, base+10, base+20 (so base 10 gives
    // 10/20/30, the usual primary point).
    void point(int base, const Vec3& p);

    // Entity type marker, handle, layer, colour, linetype and thickness.
    void write_common(const Entity& e);

    // The same, under a different type name. For an entity the target DXF
    // version cannot name and that degrades to one it can -- ELLIPSE written as
    // a POLYLINE. Everything else about the entity, its layer, linetype, colour
    // and handle, is written unchanged, because those DO survive the
    // substitution and losing them would compound the loss.
    void write_common_as(const Entity& e, const char* type_name);

    // For entities that write more than one record -- POLYLINE emits VERTEX and
    // SEQEND after itself, and each needs the same handle and layer.
    std::string handle_text(Handle h) const;
    std::string layer_name(const Entity& e) const;

    // Group 210/220/230, emitted only when the extrusion is not world Z.
    void write_extrusion(const Vec3& normal);

    const Database& db() const { return db_; }

private:
    void write_header();
    void write_classes();
    void write_tables();
    void write_blocks();
    void write_entities();
    void write_objects();

    // A symbol table and its records. At R12 these emit exactly what they
    // always did; at R13 and later the table carries a handle and the records
    // are owned by it, which is the structure a reader walks to find them.
    // Returns the table's handle, which its records need as their owner.
    std::string begin_table(const char* name, int count);
    void table_record(const char* type, const std::string& owner, const char* record_subclass);

    // The BLOCK_RECORD table, which R13 and later require and R12 has no
    // concept of. Fills model_space_owner_, which every entity then names as
    // its owner -- so this must run before any entity is written.
    void write_block_records();

    void begin_section(const char* name);
    void end_section();

    std::ostream& out_;
    const Database& db_;
    DxfVersion version_{DxfVersion::R12};

    Handle next_handle_{1};
    Handle seed_hint_{0};
    std::string model_space_owner_;
};

// Convenience: write the whole database to a file. Returns false if the file
// could not be opened.
bool write_dxf_file(const Database& db, const std::string& path,
                    DxfVersion version = DxfVersion::R12);

// One past the highest handle a write would issue. Zero for a version that
// needs none. Exposed because $HANDSEED must be known before the header.
Handle dxf_count_handles(const Database& db, DxfVersion version);

}  // namespace noto
