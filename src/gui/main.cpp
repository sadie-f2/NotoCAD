// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// ncad_gui: the Qt shell.
//
// The same drawing, engine and interpreter as `ncad`, with a viewport attached.
// A DXF named on the command line is opened; with no arguments the in-tree
// sample drawing is shown instead, so a bare launch still has something in it.
#include "main_window.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QStringList>

#include <cstdio>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("NotoCAD"));

    QString drawing;
    bool reset_ui = false;

    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString& arg = args.at(i);

        if (arg == QLatin1String("-h") || arg == QLatin1String("--help")) {
            std::printf(
                "ncad_gui -- NotoCAD with a viewport\n\n"
                "Usage:\n"
                "  ncad_gui [options] [drawing.dxf]\n\n"
                "Options:\n"
                "  --reset-ui   forget remembered toolbar placement and window size\n"
                "  -h           show this help\n\n"
                "Toolbar placement, window size and command-line text size are\n"
                "remembered in:\n  %s\n",
                ncad::MainWindow::settings_path().toLocal8Bit().constData());
            return 0;
        }
        if (arg == QLatin1String("--reset-ui")) {
            reset_ui = true;
            continue;
        }
        // Qt eats its own options (-style, -platform ...) before this, so
        // anything still beginning with a dash is a mistake worth naming
        // rather than a file called "-x".
        if (arg.startsWith(QLatin1Char('-'))) {
            std::fprintf(stderr, "ncad_gui: unknown option %s\n", arg.toLocal8Bit().constData());
            return 2;
        }
        if (!drawing.isEmpty()) {
            std::fprintf(stderr, "ncad_gui: only one drawing can be opened; %s was not\n",
                         arg.toLocal8Bit().constData());
            return 2;
        }
        drawing = arg;
    }

    // Checked here rather than left to OPEN, so a mistyped name fails on the
    // command line where it was typed instead of opening an empty window with
    // an error buried in its transcript.
    if (!drawing.isEmpty() && !QFileInfo::exists(drawing)) {
        std::fprintf(stderr, "ncad_gui: %s: no such file\n", drawing.toLocal8Bit().constData());
        return 1;
    }

    if (reset_ui) ncad::MainWindow::forget_window_state();

    // Sizing and placement come from the remembered state inside the window,
    // which is why nothing is resized here any more.
    ncad::MainWindow window(drawing);
    window.setWindowTitle(drawing.isEmpty()
                              ? QStringLiteral("NotoCAD")
                              : QStringLiteral("NotoCAD -- %1").arg(QFileInfo(drawing).fileName()));
    window.show();

    return app.exec();
}
