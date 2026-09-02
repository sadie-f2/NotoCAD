// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// PLOT to a real printer, which is the one thing the core cannot do.
//
// **The split this file sits on.** `plot.hpp` puts plotting in the core so both
// front ends have it and headless plotting over SSH works; a printer is
// hardware attached to a session, so reaching one is the window's business and
// `ncad` refuses it in words. WHAT to plot -- area, view, paper -- is decided
// in the core and is identical either way. WHERE it goes is what differs.
//
// **What must not differ is the picture.** This paints through the same
// `PlotPlacement` the PDF writer uses and the same `plot_draw_context`, so the
// two destinations cannot disagree about scale, centring or how finely a curve
// was flattened. A printer path with its own fit would come off the printer at
// the wrong size, and that is not visible on screen -- only later, in the hand,
// against a ruler.
#pragma once

#include "ncad/plot.hpp"
#include "ncad/render.hpp"

#include <QPointF>

class QPainter;
class QPagedPaintDevice;

namespace ncad {

class Database;

// Paints one plot into `painter`, whose device must already be the page size
// `placement` was built for.
//
// A `Renderer` like any other -- the fourth implementation, after the screen,
// the PDF writer and the picture-diff backend -- so it reaches the drawing
// through `draw_database` and never grows its own walk.
class QPainterPlotRenderer final : public Renderer {
public:
    QPainterPlotRenderer(QPainter& painter, const PlotPlacement& placement);

    void begin_entity(const EntityProps& props) override;
    void polyline(const Vec3* pts, std::size_t count, bool closed) override;

private:
    // PDF measures up from the bottom-left; Qt measures down from the
    // top-left. The flip happens here, in one place, rather than inside
    // `PlotPlacement` -- the placement answers "where on the sheet", which is
    // a fact about paper, and the y direction is a fact about the toolkit.
    QPointF page_point(const Vec3& p) const;

    QPainter& painter_;
    const PlotPlacement& placement_;
};

// Draws `db` onto `device` at `view`/`paper`. Returns false, with `error` set,
// when the view or the paper is unusable -- the same refusals the PDF path
// makes, taken from the same `plot_paper_problem`, so a printer cannot accept
// paper the file writer would decline.
bool paint_plot(QPagedPaintDevice& device, const Database& db, const PlotView& view,
                const PlotPaper& paper, std::string& error);

}  // namespace ncad
