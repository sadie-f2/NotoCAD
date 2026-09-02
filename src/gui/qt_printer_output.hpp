// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The Qt shell's printer: the native dialog, and a page painted through the
// core's own placement.
//
// **Why this opens its dialog synchronously**, where `offer_file_dialog`
// defers with `singleShot`. That deferral exists for the AUTOMATIC offer --
// a dialog raised by the prompt advancing, from inside the key handler that
// advanced it. This one is raised by the user explicitly asking for a printer,
// which is the same shape as R12's `~`, and `on_line_entered` already runs
// `run_file_dialog()` straight through for that case. The dialog is parented,
// so it is window-modal and the command line cannot be typed into while a
// command is suspended inside it -- which is the reentrancy that would
// otherwise matter.
#pragma once

#include "ncad/printer_output.hpp"

class QWidget;

namespace ncad {

class QtPrinterOutput final : public PrinterOutput {
public:
    explicit QtPrinterOutput(QWidget* parent) : parent_(parent) {}

    bool print(const Database& db, const PlotView& view, PlotPaper& paper,
               std::string& error) override;

private:
    QWidget* parent_{nullptr};
};

}  // namespace ncad
