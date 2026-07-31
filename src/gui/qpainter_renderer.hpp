// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// A Renderer that paints into a QPainter.
//
// QPainter before OpenGL: R12-era display is wireframe -- lines, arcs, text --
// and QPainter does that with no shader pipeline, no GL context management and
// no driver variability. The move to QOpenGLWidget happens behind this same
// Renderer interface when 3D orbit performance demands it.
//
// This class is the ONLY place Qt and the geometry kernel meet on the draw
// path. Entities emit world-space polylines; projection to pixels happens here.
#pragma once

#include "ncad/render.hpp"
#include "ncad/viewport.hpp"

#include <QColor>
#include <QPainter>
#include <QPolygonF>

namespace ncad {

class Database;

// R12 colour index to a displayable colour. Index 7 is the drawing's default
// and is drawn light here because the viewport background is dark, exactly as
// R12 on a CRT did it.
QColor aci_color(std::int16_t index);

class QPainterRenderer final : public Renderer {
public:
    // `painter`, `viewport` and `db` must outlive the renderer; it is built per
    // paintEvent and thrown away.
    QPainterRenderer(QPainter& painter, const Viewport& viewport, const Database& db);

    void begin_entity(const EntityProps& props) override;
    void polyline(const Vec3* pts, std::size_t count, bool closed) override;

private:
    // Resolves BYLAYER/BYBLOCK against the layer table.
    std::int16_t effective_color(const EntityProps& props) const;

    QPainter& painter_;
    const Viewport& viewport_;
    const Database& db_;
    QPolygonF scratch_;  // reused across entities to keep paintEvent allocation-free
};

}  // namespace ncad
