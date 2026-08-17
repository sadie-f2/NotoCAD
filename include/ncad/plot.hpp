// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// PLOT: the drawing on paper, as PDF.
//
// **In the core, not in the Qt shell**, and that is the decision this file
// exists to record. Qt would give PDF nearly free -- `QPrinter` is a
// `QPaintDevice`, so the existing `QPainterRenderer` would drive it unchanged --
// but it would strand `ncad`, which has no Qt, and `CLAUDE.md`'s rule is that
// both front ends agree. A command that only works in the window is one that
// `(command "PLOT" ...)` cannot drive, and no command may be like that.
//
// So the PDF writer lives here and both front ends get it, headless plotting
// over SSH works, and the Qt shell is free to ADD the native print dialog on
// top for real printers and queues -- the `FILEDIA` arrangement exactly.
//
// **The reason this is small: there are no fonts.** TEXT is drawn with the
// bundled Hershey stroke font, so every mark in a NotoCAD drawing is already a
// polyline. A PDF that carries only line work needs no embedding, no encoding
// tables, no CID maps -- which is the whole of what makes PDF generation
// unpleasant. It is a direct dividend of the Hershey decision.
//
// **Black line work only, for now.** That is what a plot was in R12: a pen
// plotter had one pen, and colour meant a PEN NUMBER through the colour-to-pen
// table, which is where lineweight came from too. That table is the right way
// to get varied weight and is a clean second phase. `aci_color` currently lives
// in the GUI and returns a `QColor`; sharing the palette with the core belongs
// with the pen table rather than ahead of it.
#pragma once

#include "ncad/bbox.hpp"
#include "ncad/vec3.hpp"

#include <string>

namespace ncad {

class Database;

// Which part of the drawing goes on the page. R12's names.
enum class PlotArea : std::uint8_t {
    Display,  // what the viewport is showing -- needs a view, so window only
    Extents,  // everything the drawing contains
    Limits,   // the LIMITS rectangle, whether or not anything is in it
    Window,   // a rectangle the user picked
};

const char* plot_area_name(PlotArea a);

// The orthographic frame a plot is taken through, and the rectangle of it that
// reaches the page.
//
// Deliberately the same shape as `DrawContext`'s clip -- an origin plus two
// unit axes plus two intervals -- so PLOT Display is a copy of what the
// viewport already computed rather than a second way of saying the same thing.
// The reason that shape is right is recorded there: a view turned off axis has
// no useful world-space bounding box, since the visible volume is an infinite
// slab.
struct PlotView {
    Vec3 origin{};
    Vec3 ax{1.0, 0.0, 0.0};  // page right, in world
    Vec3 ay{0.0, 1.0, 0.0};  // page up, in world
    double min_x{0.0};
    double max_x{0.0};
    double min_y{0.0};
    double max_y{0.0};

    double width() const { return max_x - min_x; }
    double height() const { return max_y - min_y; }

    // False for a degenerate rectangle -- an empty drawing plotted to Extents,
    // or LIMITS never set. Refusing beats dividing by zero and writing a page
    // with everything at one point.
    bool valid() const;
};

// Paper, in millimetres. A4 landscape, which is what fits a screenful of
// drawing without anyone having to think about it.
struct PlotPaper {
    double width_mm{297.0};
    double height_mm{210.0};
    double margin_mm{10.0};

    bool valid() const;
};

// A plan view over `box`, looking down `normal`. What Extents, Limits and
// Window all reduce to, since each of them names a world-space rectangle and
// says nothing about the direction it is seen from.
PlotView plot_view_for_box(const BBox& box, const Vec3& normal);

// The whole document, as text. `write_plot_pdf` is this plus a file, which is
// the same split `write_dxf_text` and `write_dxf_file` already use and for the
// same reason: the tests want the bytes without a temporary file.
//
// Empty on a view or paper that is not `valid()`.
std::string plot_pdf_text(const Database& db, const PlotView& view, const PlotPaper& paper);

bool write_plot_pdf(const Database& db, const PlotView& view, const PlotPaper& paper,
                    const std::string& path, std::string& error);

}  // namespace ncad
