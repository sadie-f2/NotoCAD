// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// PLOT's PDF writer. See plot.hpp for why it is here and not in the Qt shell.

#include "ncad/plot.hpp"

#include "ncad/database.hpp"
#include "ncad/ecs.hpp"
#include "ncad/render.hpp"
#include "ncad/scene.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace ncad {
namespace {

// PDF's user space unit is 1/72 inch, so everything reaches the page in points.
constexpr double kPointsPerMm = 72.0 / 25.4;

// Two decimals of a point is 3.5 microns. Far finer than any plotter, and it
// keeps the content stream small on a drawing with a million vertices.
std::string num(double v) {
    if (!std::isfinite(v)) v = 0.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

// A line width that survives being looked at. R12 had no lineweights and the
// viewport draws everything one pixel wide; on paper a zero-width line is a
// hairline whose thickness the reader's device chooses, which is not the same
// as thin. A quarter point is thin and predictable.
constexpr double kLineWidthPt = 0.25;

// Projects world points onto the plot's frame and writes them as PDF path
// operators.
//
// A Renderer like any other, which is the point: the same `draw_database` walk
// that feeds the screen and the DXF probes feeds this, so what plots is what is
// drawn. `CLAUDE.md` calls rendering the same database two ways a correctness
// check worth having; this makes it three.
class PdfRenderer final : public Renderer {
public:
    PdfRenderer(const PlotView& view, const PlotPaper& paper, std::string& out)
        : view_(view), out_(out) {
        // Fit, preserving aspect: the larger of the two ratios would crop, and
        // a plot that silently loses its right-hand edge is worse than one that
        // comes out smaller than asked for.
        const double avail_w = (paper.width_mm - 2.0 * paper.margin_mm) * kPointsPerMm;
        const double avail_h = (paper.height_mm - 2.0 * paper.margin_mm) * kPointsPerMm;
        scale_ = std::min(avail_w / view.width(), avail_h / view.height());

        // Centred in what is left, so the margins come out even.
        origin_x_ = paper.margin_mm * kPointsPerMm + (avail_w - view.width() * scale_) * 0.5;
        origin_y_ = paper.margin_mm * kPointsPerMm + (avail_h - view.height() * scale_) * 0.5;
    }

    void begin_entity(const EntityProps&) override {
        // Black line work only -- see plot.hpp. Colour arrives with the pen
        // table, which is also where lineweight comes from.
    }

    void polyline(const Vec3* pts, std::size_t count, bool closed) override {
        if (count < 2) return;

        for (std::size_t i = 0; i < count; ++i) {
            double px = 0.0;
            double py = 0.0;
            place(pts[i], px, py);
            out_ += num(px);
            out_ += ' ';
            out_ += num(py);
            out_ += (i == 0 ? " m\n" : " l\n");
        }
        // `h` closes the subpath rather than repeating the first point, which
        // is what makes the join a corner instead of two ends meeting.
        if (closed) out_ += "h\n";
        out_ += "S\n";
    }

private:
    void place(const Vec3& p, double& px, double& py) const {
        const Vec3 rel = p - view_.origin;
        px = origin_x_ + (dot(rel, view_.ax) - view_.min_x) * scale_;
        py = origin_y_ + (dot(rel, view_.ay) - view_.min_y) * scale_;
    }

    PlotView view_;
    std::string& out_;
    double scale_{1.0};
    double origin_x_{0.0};
    double origin_y_{0.0};
};

// The document around the content stream.
//
// Five objects and an xref table. No fonts, no images, no resources at all --
// which is the whole reason this is a page of code rather than a library.
std::string wrap_pdf(const std::string& content, const PlotPaper& paper) {
    const std::string w = num(paper.width_mm * kPointsPerMm);
    const std::string h = num(paper.height_mm * kPointsPerMm);

    std::vector<std::string> objects;
    objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    objects.push_back("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + w + " " + h +
                      "] /Contents 4 0 R /Resources << >> >>");
    objects.push_back("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" +
                      content + "endstream");

    std::string pdf = "%PDF-1.4\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }

    // The xref offsets have to be byte positions in the finished file, which is
    // why the objects are built first and measured as they go.
    const std::size_t xref_at = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n";
    pdf += "0000000000 65535 f \n";
    for (const std::size_t off : offsets) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%010zu 00000 n \n", off);
        pdf += buf;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\n";
    pdf += "startxref\n" + std::to_string(xref_at) + "\n%%EOF\n";
    return pdf;
}

}  // namespace

const char* plot_area_name(PlotArea a) {
    switch (a) {
        case PlotArea::Display: return "Display";
        case PlotArea::Extents: return "Extents";
        case PlotArea::Limits: return "Limits";
        case PlotArea::Window: return "Window";
    }
    return "?";
}

bool PlotView::valid() const {
    return width() > kEps && height() > kEps && !is_zero(ax) && !is_zero(ay);
}

bool PlotPaper::valid() const {
    return width_mm - 2.0 * margin_mm > kEps && height_mm - 2.0 * margin_mm > kEps &&
           margin_mm >= 0.0;
}

PlotView plot_view_for_box(const BBox& box, const Vec3& normal) {
    PlotView view;
    if (!box.valid()) return view;

    // The same arbitrary-axis basis every entity uses for its own plane, so a
    // plot of a tilted UCS comes out the way that plane is drawn rather than
    // the way world XY would show it.
    const Basis b = arbitrary_axis(is_zero(normal) ? kWorldZ : normalize(normal));
    view.origin = box.min;
    view.ax = b.ax;
    view.ay = b.ay;

    // Every corner, not two: the box's min and max are world-axis extremes and
    // say nothing about the extremes along a tilted frame.
    bool first = true;
    for (const double x : {box.min.x, box.max.x}) {
        for (const double y : {box.min.y, box.max.y}) {
            for (const double z : {box.min.z, box.max.z}) {
                const Vec3 rel = Vec3{x, y, z} - view.origin;
                const double vx = dot(rel, view.ax);
                const double vy = dot(rel, view.ay);
                if (first) {
                    view.min_x = view.max_x = vx;
                    view.min_y = view.max_y = vy;
                    first = false;
                } else {
                    view.min_x = std::min(view.min_x, vx);
                    view.max_x = std::max(view.max_x, vx);
                    view.min_y = std::min(view.min_y, vy);
                    view.max_y = std::max(view.max_y, vy);
                }
            }
        }
    }
    return view;
}

std::string plot_pdf_text(const Database& db, const PlotView& view, const PlotPaper& paper) {
    if (!view.valid() || !paper.valid()) return {};

    std::string content;
    content += num(kLineWidthPt) + " w\n";
    content += "0 0 0 RG\n";
    // Butt caps and mitred joins, which is what a pen plotter draws and what
    // keeps a corner looking like a corner.
    content += "0 J\n0 j\n";

    PdfRenderer r(view, paper, content);

    // No clip: what is outside the plotted rectangle simply lands outside the
    // MediaBox and is not shown. Clipping here would mean flattening curves
    // against a tolerance chosen for the screen, and the plot has its own.
    DrawContext ctx;
    ctx.chord_tolerance = std::max(view.width(), view.height()) * 1e-4;
    draw_database(db, ctx, r);

    return wrap_pdf(content, paper);
}

bool write_plot_pdf(const Database& db, const PlotView& view, const PlotPaper& paper,
                    const std::string& path, std::string& error) {
    const std::string pdf = plot_pdf_text(db, view, paper);
    if (pdf.empty()) {
        error = view.valid() ? "the paper leaves no room to plot on"
                             : "nothing to plot -- the area is empty";
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "cannot write " + path;
        return false;
    }
    out << pdf;
    if (!out.good()) {
        error = "could not finish writing " + path;
        return false;
    }
    return true;
}

}  // namespace ncad
