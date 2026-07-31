// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Emits a sample R12 DXF exercising the parts most likely to be wrong.
//
// This is the project's real correctness gate: open the output in another CAD
// application and confirm the tilted entities land where they should. Headless
// tests can prove the maths is self-consistent, but only a second implementation
// reading the file proves the format is right.
//
// The drawing itself lives in sample_drawing.cpp, shared with the viewer.
#include "sample_drawing.hpp"

#include "ncad/database.hpp"
#include "ncad/dxf.hpp"

#include <cstdio>

using namespace ncad;

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "notocad_sample.dxf";

    Database db;
    build_sample_drawing(db);

    if (!write_dxf_file(db, path)) {
        std::fprintf(stderr, "error: could not write %s\n", path);
        return 1;
    }

    const BBox e = db.extents();
    std::printf("wrote %s\n", path);
    std::printf("  %zu entities, %zu layers\n", db.size(), db.layers().size());
    std::printf("  extents  min (%g %g %g)\n", e.min.x, e.min.y, e.min.z);
    std::printf("           max (%g %g %g)\n", e.max.x, e.max.y, e.max.z);
    return 0;
}
