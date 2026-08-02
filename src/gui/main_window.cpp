// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "main_window.hpp"

#include <QApplication>
#include <QKeyEvent>

#include "command_line_widget.hpp"

#include <QShortcut>
#include "sample_drawing.hpp"
#include "viewport_widget.hpp"

#include <QCloseEvent>
#include <QSplitter>

namespace ncad {

// The GUI half of PromptOutput. `ncad` has StreamOutput; this is the only
// difference between the two front ends.
class MainWindow::WidgetOutput final : public app::PromptOutput {
public:
    explicit WidgetOutput(CommandLineWidget* widget) : widget_(widget) {}

    void write(const std::string& text) override {
        widget_->append_output(QString::fromStdString(text));
    }

    void write_error(const std::string& text) override {
        widget_->append_error(QString::fromStdString(text));
    }

private:
    CommandLineWidget* widget_;
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), interp_(ctx_), engine_(db_) {
    interp_.set_database(&db_);
    interp_.set_command_engine(&engine_);
    engine_.set_script_loader(&script_loader_);

    // Something to look at on startup. Goes away when there is a DXF reader and
    // a file to open instead.
    build_sample_drawing(db_);

    view_ = new ViewportWidget(db_, this);
    view_->set_engine(&engine_);
    engine_.set_view_control(view_);
    command_line_ = new CommandLineWidget(this);

    output_ = std::make_unique<WidgetOutput>(command_line_);
    // interactive: Enter repeats the last command, exactly as at the terminal.
    session_ = std::make_unique<app::PromptSession>(ctx_, interp_, engine_, *output_, true);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(view_);
    splitter->addWidget(command_line_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({600, 150});
    setCentralWidget(splitter);

    // Text size, on the platform's own zoom shortcuts. QKeySequence resolves
    // these per platform -- Ctrl+= and Ctrl+- on Linux and Windows, Cmd on
    // macOS -- so the binding is right on the Mac without a second code path.
    //
    // ApplicationShortcut, not WindowShortcut: the viewport takes every
    // keystroke it can so that typing anywhere reaches the command line, and a
    // window-scoped shortcut would be swallowed by that same rule.
    add_zoom_shortcut(QKeySequence::ZoomIn, 1);
    add_zoom_shortcut(QKeySequence::ZoomOut, -1);
    // QKeySequence::ZoomIn is Ctrl+Plus, which needs Shift on most layouts.
    // Ctrl+= is what people actually press, and every browser accepts both.
    add_zoom_shortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), 1);

    connect(command_line_, &CommandLineWidget::lineEntered, this, &MainWindow::on_line_entered);
    connect(command_line_, &CommandLineWidget::cancelRequested, this,
            &MainWindow::on_cancel_requested);
    connect(view_, &ViewportWidget::pointPicked, this, &MainWindow::on_point_picked);
    connect(view_, &ViewportWidget::cancelRequested, this, &MainWindow::on_cancel_requested);

    command_line_->append_output(
        "NotoCAD -- type ? for commands, ( for AutoLISP, QUIT to exit.\n"
        "Middle-drag pans, shift+middle orbits, wheel zooms, Home is extents.\n"
        "Ctrl +/- resizes this text (Cmd on macOS).\n");
    refresh_prompt();
    command_line_->focus_input();

    // Watch every key press in the application, not just this window's, so a
    // keystroke landing on any widget that does not want it still reaches the
    // command line.
    qApp->installEventFilter(this);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() != QEvent::KeyPress) return QMainWindow::eventFilter(watched, event);

    // The command line already has it: leave the key alone, or every character
    // would be inserted twice and Home would stop moving the cursor.
    if (command_line_->input_has_focus()) return QMainWindow::eventFilter(watched, event);

    // Re-entrancy: delivering the key below sends it to the input, which comes
    // back through here. That normally exits at the test above, but not if
    // focus refused to move -- a disabled input, or a window that is not
    // active -- and then it would recur until the stack ran out.
    if (routing_key_) return QMainWindow::eventFilter(watched, event);

    auto* key = static_cast<QKeyEvent*>(event);

    // The rule, and it is deliberately stated as a default rather than as a
    // list: ANY key pressed anywhere in the window belongs to the command line,
    // unless it is one of the few the viewport genuinely owns. R12 works this
    // way -- there is no focus step, you just type -- and the previous version
    // of this got it half right by routing only PRINTABLE keys, which left
    // Return, Backspace and the history arrows to fall through to a viewport
    // that drops them.
    //
    // Enumerating what to forward is the shape of the bug. Enumerating what to
    // keep is a list of three things that cannot quietly grow.
    switch (key->key()) {
        // View control. These are not text, and the viewport is the only thing
        // that can act on them.
        case Qt::Key_Escape:
        case Qt::Key_Home:
        // A bare modifier is not a keystroke and must not pull focus: holding
        // shift before a middle-drag orbit would otherwise move the caret.
        case Qt::Key_Shift:
        case Qt::Key_Control:
        case Qt::Key_Alt:
        case Qt::Key_Meta:
            return QMainWindow::eventFilter(watched, event);
        default:
            break;
    }

    // Accelerators and window management keep working: a Ctrl or Alt chord is
    // addressed to the application, not typed into a prompt.
    if (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return QMainWindow::eventFilter(watched, event);
    }

    routing_key_ = true;
    command_line_->deliver_key(key);
    routing_key_ = false;
    return true;
}

MainWindow::~MainWindow() = default;

void MainWindow::refresh_prompt() {
    command_line_->set_prompt(QString::fromStdString(session_->current_prompt()));
}

void MainWindow::on_line_entered(const QString& line) {
    command_line_->echo_input(QString::fromStdString(session_->current_prompt()), line);

    if (!session_->feed_line(line.toStdString())) {
        close();
        return;
    }

    // A command may have drawn something, and the prompt has almost certainly
    // changed. Both are cheap to refresh unconditionally, and working out which
    // commands modify the database is exactly the coupling to avoid.
    //
    // The snap goes with them. A typed osnap override changes what the cursor
    // is over without the mouse having moved, and a plain repaint would redraw
    // the marker found under the previous mask -- so the override would look
    // like it had been ignored until the mouse was jiggled.
    refresh_prompt();
    view_->refresh_osnap();
}

void MainWindow::on_point_picked(const QString& prompt, const QString& answer) {
    // A click answered a prompt inside the viewport, so the command line has to
    // catch up with a question it never saw the answer to.
    command_line_->echo_input(prompt, answer);
    refresh_prompt();
}

void MainWindow::on_cancel_requested() {
    // Escape is the engine's business, not each command's: committed work
    // survives it, as in R12.
    if (engine_.active()) {
        engine_.cancel();
        command_line_->append_output("*Cancel*\n");
    }
    refresh_prompt();
    view_->update();
    command_line_->focus_input();
}

void MainWindow::add_zoom_shortcut(const QKeySequence& keys, int delta) {
    auto* sc = new QShortcut(keys, this);
    sc->setContext(Qt::ApplicationShortcut);
    connect(sc, &QShortcut::activated, this, [this, delta]() {
        command_line_->step_font_size(delta);
        command_line_->append_output(
            QStringLiteral("Command line text size %1\n").arg(command_line_->font_points()));
    });
}

}  // namespace ncad
