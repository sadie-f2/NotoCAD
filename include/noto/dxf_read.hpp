// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Reading R12 DXF.
//
// The other half of the correctness gate. Writing DXF and opening it elsewhere
// proves the geometry; reading DXF back proves the two agree, and it is what
// makes the drawings this program produces re-openable by itself.
//
// The rule that shapes everything here: nothing is dropped. An entity with no
// class in this program becomes a Proxy holding the groups it was read from,
// which writes back unchanged. Opening a file and saving it must never quietly
// empty it, and "we do not support DIMENSION yet" is not a reason to destroy
// one.
//
// READING IS DELIBERATELY AHEAD OF WRITING, and that asymmetry is the point:
// read the format the rest of the world emits, guarantee the one the rest of
// the world can read. Writing stays AC1009.
//
// This comment used to claim a later version's file was refused. It never was --
// `newer_version` is reported and OPEN warns, which is what the tests pin. And
// modern files turn out to read well, because the structure was already
// tolerant rather than because anything was written for it: the CLASSES and
// OBJECTS sections are stepped over, group 100 subclass markers and group 5
// handles are simply not asked for, and anything with no class here becomes a
// Proxy. So the work of importing a 2018 file is a list of entity mappings, not
// a parser.
//
// Read natively but never written: ELLIPSE, LWPOLYLINE and SPLINE. The first
// and last close a real hole -- the database has held both exactly for a while
// and could not read one back, so a round trip through DXF turned an ellipse
// into a polyline permanently. LWPOLYLINE matters because modern AutoCAD writes
// it where R12 wrote POLYLINE, which is most polylines in most files.
//
// Still Proxy, on purpose: MTEXT, HATCH, DIMENSION, LEADER, TABLE, IMAGE, and
// the ACIS entities. Each needs a decision about what it degrades TO, and a
// guess there is worse than an honest passthrough.
#pragma once

#include <string>

namespace noto {

class Database;

struct DxfReadResult {
    bool ok{false};
    std::string error;

    // What came back, for the message the OPEN command prints.
    std::size_t entities{0};
    std::size_t proxies{0};
    std::size_t layers{0};
    std::size_t linetypes{0};
    std::size_t blocks{0};
    std::size_t coordinate_systems{0};

    // INSERTs naming a block the file never defined. They are kept, with no
    // definition, so the drawing is not silently short an entity -- but the
    // caller should say so, because it means the file is broken rather than
    // merely unusual.
    std::size_t unresolved_inserts{0};

    // Set when the file names a version this reader does not claim to handle.
    // Reading continues anyway -- an R13 file is mostly R12 plus entities that
    // become proxies -- but the caller may want to say so.
    bool newer_version{false};
    std::string version;
};

// Replaces the contents of `db`. On failure the database is left cleared rather
// than half-populated, since a partly-read drawing is worse than none.
DxfReadResult read_dxf_file(Database& db, const std::string& path);

// The same, from text already in memory. This is what the tests drive.
DxfReadResult read_dxf_text(Database& db, const std::string& text);

}  // namespace noto
