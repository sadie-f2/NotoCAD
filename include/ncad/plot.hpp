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

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ncad {

class Database;
class Sysvars;

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
//
// These defaults are load-bearing: they are what every plot this program has
// ever written used, and PAPERSIZE/PAPERWIDTH/PAPERHEIGHT/PAPERMARGIN default
// to exactly them, so the A4 default keeps producing the same bytes it always
// has. Moving one of these numbers moves every existing plot.
struct PlotPaper {
    double width_mm{297.0};
    double height_mm{210.0};
    double margin_mm{10.0};

    bool valid() const;
};

// Empty when the paper can be plotted on; otherwise the sentence explaining
// what is wrong with it, with the numbers in it.
//
// It exists because paper is now USER INPUT -- PAPERWIDTH is a real with no
// range check, like every other real here -- so "1 x 1 mm with 10 mm margins"
// is reachable by typing, and the fit divides by what the margins leave. A
// bare false would make PLOT say only that the paper is wrong; the person who
// typed the number needs to be told which number.
std::string plot_paper_problem(const PlotPaper& paper);

// The sheets PAPERSIZE knows, in millimetres.
//
// A bare name is LANDSCAPE and a "P" suffix is portrait, which is the opposite
// way round from ISO's own definition of the A series. Two reasons: the paper
// this program has always plotted on is A4 LANDSCAPE at 297x210 and that
// default may not move, so "A4" has to mean the landscape sheet; and a drawing
// is wider than it is tall more often than not, so the short name should be
// the common answer.
//
// Names rather than numbers because that is how the request is made -- a
// designer asks for A3, not for 420x297 -- and because a name is the only form
// of it that can be checked when it is typed.
struct PlotPaperSize {
    const char* name;
    double width_mm;
    double height_mm;
};

const PlotPaperSize* plot_paper_sizes(std::size_t& count);

// Case-insensitive, like find_sysvar. Null when there is no such sheet, which
// is how SETVAR PAPERSIZE refuses a typo instead of quietly plotting A4.
const PlotPaperSize* find_plot_paper_size(std::string_view name);

// What PAPERSIZE reads when the sheet was given as numbers instead of named.
// Not a row in the table above: it has no dimensions of its own, it points at
// PAPERWIDTH and PAPERHEIGHT.
inline constexpr const char* kPlotPaperUser = "USER";

// The paper PLOT is currently set to.
//
// Reads PAPERWIDTH, PAPERHEIGHT and PAPERMARGIN and NOT PAPERSIZE, because the
// name is kept in step with the numbers as they are written rather than
// resolved here: setting PAPERSIZE writes the two dimensions, and setting
// either dimension sets PAPERSIZE to USER. So the numbers always describe the
// actual sheet and there is only one place that decides what A3 means. See the
// sysvar table for the full rule.
PlotPaper plot_paper_from_sysvars(const Sysvars& vars);

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
