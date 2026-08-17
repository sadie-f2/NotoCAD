// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "sample_drawing.hpp"

#include "ncad/database.hpp"
#include "ncad/blocks.hpp"
#include "ncad/entities.hpp"
#include "ncad/mat4.hpp"

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

    // A POLYLINE, with a bulged segment.
    //
    // Here because of what its ABSENCE cost. The README records it: the claim
    // that "emitted DXF opens in AutoCAD" was tested on this drawing, which
    // contained no polylines at all, so it never exercised the VERTEX and
    // SEQEND records -- which is exactly where the bug was. A gate that does
    // not contain a thing does not test that thing.
    //
    // It earned its place a second way in the audit of 2026-08-17: a DXF
    // mutation fuzzer seeded from this drawing found nothing, because the
    // drawing has no BLOCKS section and four use-after-frees were sitting in
    // the block reader.
    {
        auto pl = std::make_unique<Polyline>();
        pl->add({0.0, -8.0, 0.0});
        pl->add({6.0, -8.0, 0.0}, 0.5);  // bulged: an arc segment
        pl->add({10.0, -4.0, 0.0});
        pl->add({0.0, -4.0, 0.0});
        pl->set_closed(true);
        pl->props().layer = frame;
        db.add(std::move(pl));
    }

    // A block definition, and both an INSERT and a MINSERT of it -- so the
    // BLOCKS section, the nested-entity path and the array path all appear in
    // the file the correctness gate opens.
    {
        BlockDef def;
        def.name = "TARGET";
        def.base = {0, 0, 0};
        def.entities.push_back(std::make_unique<Circle>(Vec3{0, 0, 0}, 1.0));
        def.entities.push_back(std::make_unique<Line>(Vec3{-1.5, 0, 0}, Vec3{1.5, 0, 0}));
        def.entities.push_back(std::make_unique<Line>(Vec3{0, -1.5, 0}, Vec3{0, 1.5, 0}));
        const BlockId id = db.add_block(std::move(def));

        if (const BlockDef* target = db.block(id)) {
            auto one = std::make_unique<Insert>(target, Mat4::translation({-6.0, 5.0, 0.0}));
            one->props().layer = flat;
            db.add(std::move(one));

            auto many = std::make_unique<Insert>(target, Mat4::translation({-6.0, -8.0, 0.0}));
            many->set_array(2, 3, 4.0, 4.0);
            many->props().layer = tilted;
            db.add(std::move(many));
        }
    }
}
