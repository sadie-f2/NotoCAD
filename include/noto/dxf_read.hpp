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
// R12 only. A later version's file is refused rather than half-read, because a
// half-read drawing looks like a damaged one.
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
