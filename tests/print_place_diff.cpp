// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Do the printer and the PDF put a world point in the same place on the page?
//
// That question is the whole of brief 13's narrowed invariant. PLOT lives in
// the core so both front ends decide the SAME plot; a printer is a destination
// the window adds on top. What may differ is where the marks go. What may NOT
// differ is where they land on the sheet -- and a printer path that fits or
// centres by its own arithmetic comes off the printer at the wrong size, which
// is invisible on screen and only shows up later, in the hand, against a ruler.
//
// So this reads the PDF writer's OWN output -- parsing the `x y m` / `x y l`
// operators out of the content stream, rather than recomputing what it should
// have written -- and compares those coordinates against the ones the Qt
// printer renderer produces for the same drawing. Same relationship
// `graph_closure` has to the patch emitter in the engine: an instrument that
// recomputed what it measures could agree with itself while both were wrong.
//
// The y flip is expected and is the one permitted difference: PDF measures up
// from the bottom-left, Qt measures down from the top-left.
//
// --selftest displaces the Qt side by one point and asserts the comparison
// SEES it, because a comparison that cannot fail is not a comparison. That is
// `render_diff`'s convention and this file keeps it.

#include "ncad/database.hpp"
#include "ncad/entities.hpp"
#include "ncad/plot.hpp"
#include "ncad/scene.hpp"
#include "print_plot.hpp"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Pt {
    double x, y;
};

// Every point the PDF content stream actually draws, in page points.
std::vector<Pt> pdf_points(const std::string& pdf) {
    std::vector<Pt> out;
    std::size_t at = 0;
    while (at < pdf.size()) {
        const std::size_t nl = pdf.find('\n', at);
        const std::size_t stop = nl == std::string::npos ? pdf.size() : nl;
        const std::string line = pdf.substr(at, stop - at);
        at = stop + 1;
        // "<x> <y> m" or "<x> <y> l" -- the only two that carry coordinates.
        if (line.size() < 5) continue;
        const char op = line.back();
        if (op != 'm' && op != 'l') continue;
        double x = 0.0;
        double y = 0.0;
        char verb = 0;
        if (std::sscanf(line.c_str(), "%lf %lf %c", &x, &y, &verb) == 3 &&
            (verb == 'm' || verb == 'l')) {
            out.push_back({x, y});
        }
    }
    return out;
}

// The same points as the Qt printer renderer places them, un-flipped back into
// PDF's bottom-left origin so the two lists are directly comparable.
class Capture final : public ncad::Renderer {
public:
    Capture(const ncad::PlotPlacement& p, double nudge) : place_(p), nudge_(nudge) {}

    void begin_entity(const ncad::EntityProps&) override {}

    void polyline(const ncad::Vec3* pts, std::size_t count, bool closed) override {
        (void)closed;
        for (std::size_t i = 0; i < count; ++i) {
            double px = 0.0;
            double py = 0.0;
            place_.place(pts[i], px, py);
            // Through the page and back, exactly as print_plot.cpp does it, so
            // an error in that flip shows up here rather than being cancelled.
            const double page_y = place_.page_height() - py;
            out.push_back({px + nudge_, place_.page_height() - page_y});
        }
    }

    std::vector<Pt> out;

private:
    const ncad::PlotPlacement& place_;
    double nudge_;
};

void build_drawing(ncad::Database& db) {
    db.add(std::make_unique<ncad::Line>(ncad::Vec3{0, 0, 0}, ncad::Vec3{40, 0, 0}));
    db.add(std::make_unique<ncad::Line>(ncad::Vec3{40, 0, 0}, ncad::Vec3{40, 25, 0}));
    db.add(std::make_unique<ncad::Line>(ncad::Vec3{40, 25, 0}, ncad::Vec3{0, 25, 0}));
    db.add(std::make_unique<ncad::Circle>(ncad::Vec3{20, 12.5, 0}, 8.0));
}

int compare(double nudge, bool expect_match) {
    ncad::Database db;
    build_drawing(db);
    const ncad::PlotView view = ncad::plot_view_for_box(db.extents(), ncad::Vec3{0, 0, 1});
    const ncad::PlotPaper paper;  // the A4 default

    const std::vector<Pt> want = pdf_points(ncad::plot_pdf_text(db, view, paper));
    if (want.empty()) {
        std::printf("FAIL: the PDF writer produced no coordinates to compare against\n");
        return 1;
    }

    const ncad::PlotPlacement placement(view, paper);
    Capture got(placement, nudge);
    const ncad::DrawContext ctx = ncad::plot_draw_context(view);
    ncad::draw_database(db, ctx, got);

    if (got.out.size() != want.size()) {
        std::printf("FAIL: the two paths drew %zu and %zu points\n", want.size(),
                    got.out.size());
        return 1;
    }

    // Two decimals is what the PDF writer rounds to, so anything below that is
    // not a disagreement the file could express.
    const double tol = 0.005;
    std::size_t bad = 0;
    double worst = 0.0;
    for (std::size_t i = 0; i < want.size(); ++i) {
        const double dx = std::fabs(want[i].x - got.out[i].x);
        const double dy = std::fabs(want[i].y - got.out[i].y);
        const double d = std::max(dx, dy);
        if (d > worst) worst = d;
        if (d > tol) ++bad;
    }

    const bool matched = bad == 0;
    std::printf("%-28s %zu points, %zu differ, worst %.4f pt\n",
                nudge == 0.0 ? "printer vs PDF:" : "printer vs PDF (+1pt):", want.size(), bad,
                worst);
    if (matched != expect_match) {
        std::printf("FAIL: expected %s\n", expect_match ? "a match" : "a difference");
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    int bad = 0;
    // They agree.
    bad += compare(0.0, true);
    // And the comparison can fail: one point of displacement is seen.
    bad += compare(1.0, false);

    if (bad == 0) std::printf("print placement: the two destinations agree\n");
    return bad == 0 ? 0 : 1;
}
