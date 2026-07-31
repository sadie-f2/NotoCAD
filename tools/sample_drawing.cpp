// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "sample_drawing.hpp"

#include "ncad/database.hpp"
#include "ncad/entities.hpp"

#include <memory>
#include <numbers>

using namespace ncad;

void build_sample_drawing(Database& db) {
    constexpr double pi = std::numbers::pi;

    const LayerId flat = db.add_layer("FLAT", 7);
    const LayerId tilted = db.add_layer("TILTED", 1);
    const LayerId frame = db.add_layer("FRAME", 3);

    // A square in the world plane, as a baseline that must appear unremarkable.
    const Vec3 corners[4] = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
    for (int i = 0; i < 4; ++i) {
        auto l = std::make_unique<Line>(corners[i], corners[(i + 1) % 4]);
        l->props().layer = frame;
        db.add(std::move(l));
    }

    // Circle and arc in the world plane.
    auto c0 = std::make_unique<Circle>(Vec3{5, 5, 0}, 3.0);
    c0->props().layer = flat;
    db.add(std::move(c0));

    auto a0 = std::make_unique<Arc>(Vec3{5, 5, 0}, 4.5, 0.0, pi / 2.0);
    a0->props().layer = flat;
    db.add(std::move(a0));

    // The interesting cases: entities whose planes are not parallel to world XY.
    // If the arbitrary axis algorithm or the ECS conversion is wrong, these are
    // the ones that land in the wrong place when another CAD tool reads the file.
    const Vec3 normals[3] = {{1, 0, 0}, {0, 1, 0}, {1, 1, 1}};
    for (int i = 0; i < 3; ++i) {
        const Vec3 center{15.0 + 10.0 * static_cast<double>(i), 5.0, 0.0};

        auto c = std::make_unique<Circle>(center, 3.0, normals[i]);
        c->props().layer = tilted;
        db.add(std::move(c));

        // A quarter arc in the same plane, so the start/end orientation is visible.
        auto a = std::make_unique<Arc>(center, 4.0, 0.0, pi / 2.0, normals[i]);
        a->props().layer = tilted;
        db.add(std::move(a));

        // A line along the plane's normal, marking which way the circle faces.
        auto n = std::make_unique<Line>(center, center + normalize(normals[i]) * 5.0);
        n->props().layer = frame;
        db.add(std::move(n));
    }
}
