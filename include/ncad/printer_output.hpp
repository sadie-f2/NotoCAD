// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// How PLOT reaches a real printer.
//
// An interface for the same reason `ViewControl` and `ClipboardTransport` are:
// the core cannot see Qt. The Qt shell hands the engine one backed by
// `QPrinter` and the native print dialog; `ncad` is handed nothing, and a null
// printer is the TRUTH rather than a stub that fails later.
//
// **This is where the both-front-ends-agree rule is scoped, deliberately.**
// `plot.hpp` puts plotting in the core so both front ends decide the same plot
// and headless plotting over SSH works, and that stays true. What a printer
// adds is a DESTINATION, and a destination is allowed to differ: a printer is
// hardware attached to a session, so `ncad` over SSH has no honest answer to
// "which printer". It says so in words rather than falling back to a file --
// the idiom `SOLIDS` uses for a missing engine, and for the same reason. A
// silent fallback would write a PDF somebody never asked for and never looks
// for.
//
// What may NOT differ is the picture. The shell's implementation paints
// through the same `PlotPlacement` and `plot_draw_context` the PDF writer
// uses, so the two destinations cannot disagree about scale, centring or how
// finely a curve was flattened. `tests/print_place_diff.cpp` asserts exactly
// that, by reading the PDF writer's own output rather than recomputing it.
#pragma once

#include <string>

namespace ncad {

class Database;
struct PlotView;
struct PlotPaper;

class PrinterOutput {
public:
    virtual ~PrinterOutput() = default;

    // Puts the plot on paper, asking the user which printer and on what sheet.
    //
    // Returns false when nothing was printed. `error` is empty if the user
    // simply cancelled -- declining a dialog is not a fault and must not be
    // reported as one -- and carries a sentence otherwise.
    //
    // `paper` is in/out: it arrives holding what the PAPERSIZE sysvars say, and
    // the implementation writes back whatever the dialog's page setup was left
    // on, so the choice is made once and the rest of the program reads it from
    // where it has always read it.
    virtual bool print(const Database& db, const PlotView& view, PlotPaper& paper,
                       std::string& error) = 0;
};

}  // namespace ncad
