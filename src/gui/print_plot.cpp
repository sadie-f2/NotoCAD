// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "print_plot.hpp"

#include "ncad/database.hpp"
#include "ncad/scene.hpp"

#include <QPagedPaintDevice>
#include <QPainter>
#include <QPageLayout>
#include <QPageSize>
#include <QPolygonF>
#include <QSizeF>

namespace ncad {

QPainterPlotRenderer::QPainterPlotRenderer(QPainter& painter, const PlotPlacement& placement)
    : painter_(painter), placement_(placement) {}

QPointF QPainterPlotRenderer::page_point(const Vec3& p) const {
    double px = 0.0;
    double py = 0.0;
    placement_.place(p, px, py);
    return QPointF(px, placement_.page_height() - py);
}

void QPainterPlotRenderer::begin_entity(const EntityProps&) {
    // Black line work only, exactly as the PDF writer does it -- see plot.hpp.
    // Colour arrives with the pen table, which is also where lineweight comes
    // from, and it must arrive in BOTH paths on the same day or the picture
    // diff between them stops meaning anything.
}

void QPainterPlotRenderer::polyline(const Vec3* pts, std::size_t count, bool closed) {
    if (count < 2) return;

    QPolygonF poly;
    poly.reserve(static_cast<int>(count));
    for (std::size_t i = 0; i < count; ++i) poly << page_point(pts[i]);

    // No clipping and no coordinate guard, unlike the screen renderer: a page
    // is a fixed, small number of points across, and the placement maps a
    // bounded view onto it, so the huge-coordinate case QPainterRenderer
    // defends against cannot arise here. Geometry outside the plotted
    // rectangle simply falls off the sheet, which is what the PDF path does.
    if (closed) {
        painter_.drawPolygon(poly);
    } else {
        painter_.drawPolyline(poly);
    }
}

bool paint_plot(QPagedPaintDevice& device, const Database& db, const PlotView& view,
                const PlotPaper& paper, std::string& error) {
    if (!view.valid()) {
        error = "nothing to plot: the view is empty";
        return false;
    }
    // The SAME refusal the file writer makes, from the same function, so a
    // printer cannot quietly accept paper a PDF would have declined.
    const std::string problem = plot_paper_problem(paper);
    if (!problem.empty()) {
        error = problem;
        return false;
    }

    const PlotPlacement placement(view, paper);

    // The page is set from the plot's paper, not from whatever the device
    // happened to be left on. Points, because that is what the placement
    // speaks; QPageSize takes them directly.
    const QPageSize size(QSizeF(placement.page_width(), placement.page_height()),
                         QPageSize::Point, QString(), QPageSize::ExactMatch);
    device.setPageSize(size);
    // Margins are already inside the placement -- it centres the drawing in
    // what is left after them. Asking the device for margins too would take
    // them twice and shrink the plot.
    device.setPageMargins(QMarginsF(0, 0, 0, 0));

    QPainter painter;
    if (!painter.begin(&device)) {
        error = "the printer would not accept a drawing";
        return false;
    }
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(Qt::black);
    // Hairline: R12 had no lineweights, and the PDF writer sets its own fixed
    // width for the same reason. A cosmetic pen keeps it one device unit at
    // any resolution, which is what a plotter pen was.
    pen.setWidthF(0.0);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    QPainterPlotRenderer renderer(painter, placement);
    const DrawContext ctx = plot_draw_context(view);
    draw_database(db, ctx, renderer);

    painter.end();
    return true;
}

}  // namespace ncad
