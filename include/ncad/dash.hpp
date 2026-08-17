// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Linetype dashes, as a Renderer that wraps another one.
//
// Entity::draw() emits solid polylines and knows nothing about linetypes. This
// sits between draw_database() and the real backend, cuts each run into dashes
// and gaps, and passes the pieces through. QPainter and a future GL backend
// therefore share one implementation, which is the same argument that put curve
// flattening in the kernel.
//
// Why a wrapper rather than something draw() does: hit-testing and region
// selection also drive Entity::draw(), each with its own probe. Dashes emitted
// there would put gaps in what the probes see, so a dashed line could not be
// picked between its dashes and a crossing window through a gap would miss it.
// AutoCAD picks a dashed line anywhere along it. The probes simply do not wrap,
// so this is right by construction rather than by remembering a flag.
#pragma once

#include "ncad/entity.hpp"
#include "ncad/render.hpp"
#include "ncad/vec3.hpp"

#include <cstdint>
#include <vector>

namespace ncad {

class Database;

class DashRenderer final : public Renderer {
public:
    // `ltscale` is R12's LTSCALE: every pattern length is multiplied by it, so
    // one drawing's dashes can be made to read at its own scale.
    DashRenderer(Renderer& target, const Database& db, double ltscale);

    void begin_entity(const EntityProps& props) override;
    void polyline(const Vec3* pts, std::size_t count, bool closed) override;

private:
    void flush();
    // Not named `emit`: Qt defines that as a macro, and this header is
    // included by the GUI.
    void extend(const Vec3& a, const Vec3& b);

    Renderer& target_;
    const Database& db_;
    double ltscale_;

    // The pattern in force for the entity being drawn, already scaled. Empty
    // means continuous, which is the common case and costs one branch.
    std::vector<double> pattern_;
    double period_{0.0};

    // Where in the pattern the current position sits, and what is being built.
    std::size_t index_{0};
    double remaining_{0.0};
    bool drawing_{true};
    std::vector<Vec3> run_;
};

}  // namespace ncad
