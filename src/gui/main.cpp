// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// ncad_gui: the Qt shell.
//
// The same drawing, engine and interpreter as `ncad`, with a viewport attached.
// There is no DXF reader yet, so it opens with the in-tree sample drawing and
// takes no file argument.
#include "main_window.hpp"

#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("NotoCAD"));

    noto::MainWindow window;
    window.setWindowTitle(QStringLiteral("NotoCAD"));
    window.resize(1000, 800);
    window.show();

    return app.exec();
}
