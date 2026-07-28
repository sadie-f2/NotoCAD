// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// DXF R12 (AC1009) writer.
//
// Native and in-tree by design: R12 DXF is small, text, and fully documented, and
// it is this project's actual interchange path. DWG stays an optional import-only
// module so its licence never reaches the core.
#pragma once

#include "noto/entity.hpp"
#include "noto/vec3.hpp"

#include <iosfwd>
#include <string>

namespace noto {

class Database;

// Formats a double the way DXF wants it: full round-trip precision, and always
// carrying a decimal point so strict readers don't take it for an integer.
std::string dxf_real(double v);

class DxfWriter {
public:
    DxfWriter(std::ostream& out, const Database& db) : out_(out), db_(db) {}

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

    // For entities that write more than one record -- POLYLINE emits VERTEX and
    // SEQEND after itself, and each needs the same handle and layer.
    std::string handle_text(Handle h) const;
    std::string layer_name(const Entity& e) const;

    // Group 210/220/230, emitted only when the extrusion is not world Z.
    void write_extrusion(const Vec3& normal);

    const Database& db() const { return db_; }

private:
    void write_header();
    void write_tables();
    void write_blocks();
    void write_entities();

    void begin_section(const char* name);
    void end_section();

    std::ostream& out_;
    const Database& db_;
};

// Convenience: write the whole database to a file. Returns false if the file
// could not be opened.
bool write_dxf_file(const Database& db, const std::string& path);

}  // namespace noto
