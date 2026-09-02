// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "qt_printer_output.hpp"

#include "ncad/plot.hpp"
#include "print_plot.hpp"

#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPrintDialog>
#include <QPrinter>
#include <QSizeF>

namespace ncad {

bool QtPrinterOutput::print(const Database& db, const PlotView& view, PlotPaper& paper,
                            std::string& error) {
    error.clear();

    QPrinter printer(QPrinter::HighResolution);

    // The dialog opens on the sheet the drawing already says it wants, rather
    // than on whatever the platform's last-used printer was left on. PAPERSIZE
    // is the drawing's answer to "what am I plotting on" and the dialog is
    // where it gets confirmed, not where it gets ignored.
    const double pts_per_mm = plot_points_per_mm();
    printer.setPageSize(QPageSize(QSizeF(paper.width_mm * pts_per_mm, paper.height_mm * pts_per_mm),
                                  QPageSize::Point, QString(), QPageSize::FuzzyMatch));

    QPrintDialog dialog(&printer, parent_);
    dialog.setWindowTitle(QStringLiteral("Plot"));
    if (dialog.exec() != QDialog::Accepted) {
        // Cancelled. An empty `error` is how printer_output.hpp says "the user
        // decided", and the command reports that rather than a failure.
        return false;
    }

    // What the dialog was actually left on, back in millimetres, so the choice
    // reaches the sysvars the rest of the program reads. The margin is ours,
    // not the dialog's -- see below.
    const QSizeF chosen = printer.pageLayout().pageSize().size(QPageSize::Point);
    if (chosen.width() > 0.0 && chosen.height() > 0.0) {
        paper.width_mm = chosen.width() / pts_per_mm;
        paper.height_mm = chosen.height() / pts_per_mm;
    }

    // The paper must still be usable after the dialog had its say: a page size
    // smaller than the drawing's margin is reachable by choosing a small sheet
    // with a wide PAPERMARGIN set, and refusing here is better than painting
    // an empty page.
    const std::string problem = plot_paper_problem(paper);
    if (!problem.empty()) {
        error = problem;
        return false;
    }

    return paint_plot(printer, db, view, paper, error);
}

}  // namespace ncad
