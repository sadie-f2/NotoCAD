// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "main_window.hpp"

#include "ncad/commands.hpp"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include "command_icons.hpp"
#include "command_line_widget.hpp"

#include <QShortcut>
#include "sample_drawing.hpp"
#include "viewport_widget.hpp"

#include <QCloseEvent>
#include <QSplitter>

#include <memory>

namespace ncad {

// Toolbar ink. Slightly softer than the command line's text so a wall of
// icons does not out-shout the drawing, which is the thing being looked at.
const QColor kToolbarInk(196, 200, 210);

// Dark with the command line rather than with the desktop, for the reason the
// command line gives: a light strip against a black viewport reads as an
// unfinished window.
const char* const kToolbarStyle =
    "QToolBar { background: #121216; border: 0px; spacing: 1px; }"
    "QToolBar::separator { background: #33333c; width: 1px; height: 1px; margin: 4px; }"
    "QToolButton { padding: 3px; border-radius: 3px; }"
    "QToolButton:hover { background: #2a2a33; }"
    "QToolButton:pressed { background: #3a3a46; }";

// One INI on both platforms -- QSettings puts IniFormat under ~/.config even
// on macOS -- because a settings file worth having is one you can name, read
// and delete when it goes wrong.
const char* const kSettingsOrg = "NotoCAD";
const char* const kSettingsApp = "ncad_gui";

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

MainWindow::MainWindow(const QString& drawing, QWidget* parent)
    : QMainWindow(parent), interp_(ctx_), engine_(db_) {
    interp_.set_database(&db_);
    interp_.set_command_engine(&engine_);
    engine_.set_script_loader(&script_loader_);
    // The system clipboard, so COPYCLIP reaches other windows and other
    // applications. `ncad` wires an in-process one here instead.
    engine_.set_clipboard(&clipboard_);

    // Something to look at on startup, when nothing was asked for. A named
    // drawing is opened further down instead -- after the command line exists,
    // so that opening it can report through the transcript like any other OPEN.
    if (drawing.isEmpty()) {
        build_sample_drawing(db_);

        // And it does not count as unsaved work. Building it journals every
        // entity, so without this a window nobody has touched is already dirty
        // and closing it asks whether to save a drawing the user did not make
        // -- which teaches people to dismiss that question without reading it,
        // on the day it is about their own work. Undo still reaches back
        // through it.
        db_.journal().mark_saved();
    }

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

    // The platform Copy/Cut/Paste chords -- Ctrl on Linux and Windows, Cmd on
    // macOS. ApplicationShortcut for the reason the zoom shortcuts are, and
    // registered HERE, not on the command line widget: whether the chord means
    // text or geometry is a decision that needs the whole window's state, and
    // a widget-scoped shortcut only fired while focus sat inside the widget --
    // which is exactly why transcript copy never worked after a viewport
    // click.
    auto* copy_sc = new QShortcut(QKeySequence::Copy, this);
    copy_sc->setContext(Qt::ApplicationShortcut);
    connect(copy_sc, &QShortcut::activated, this, &MainWindow::on_copy_shortcut);
    auto* cut_sc = new QShortcut(QKeySequence::Cut, this);
    cut_sc->setContext(Qt::ApplicationShortcut);
    connect(cut_sc, &QShortcut::activated, this, &MainWindow::on_cut_shortcut);
    auto* paste_sc = new QShortcut(QKeySequence::Paste, this);
    paste_sc->setContext(Qt::ApplicationShortcut);
    connect(paste_sc, &QShortcut::activated, this, &MainWindow::on_paste_shortcut);

    build_toolbars();

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

    // After the toolbars exist: restoreState matches them up by objectName, and
    // a toolbar created afterwards would be placed wherever addToolBar put it
    // rather than where it was left.
    restore_window_state();

    // The named drawing, opened the way anything else opens one -- through the
    // command, so DWGNAME, the transcript line and the error path are the ones
    // OPEN already has. Two lines rather than one because a path may contain
    // spaces, which a single command line would split on.
    if (!drawing.isEmpty()) {
        on_line_entered(QStringLiteral("OPEN"));
        on_line_entered(drawing);
        // Otherwise the camera is still wherever it started and a drawing that
        // does not happen to straddle the origin opens off-screen.
        view_->zoom_extents();
    }

    // Watch every key press in the application, not just this window's, so a
    // keystroke landing on any widget that does not want it still reaches the
    // command line.
    qApp->installEventFilter(this);

    // A backstop for the ways out that are not the close button. Cmd-Q and the
    // Dock's Quit end the application without necessarily delivering a close
    // event to the window, and losing an afternoon's toolbar arrangement to
    // the wrong exit would be a poor reward for having arranged it. Saving
    // twice is harmless; not saving once is not.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() { save_window_state(); });
}

QString MainWindow::settings_path() {
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp));
    return s.fileName();
}

void MainWindow::forget_window_state() {
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp));
    s.clear();
}

void MainWindow::save_window_state() {
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp));
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    s.setValue(QStringLiteral("toolbars"), saveState());
    // A per-machine fact the program cannot guess -- see set_font_points --
    // so it is exactly the kind of thing worth remembering.
    s.setValue(QStringLiteral("commandFontPoints"), command_line_->font_points());
}

void MainWindow::restore_window_state() {
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp));

    const QByteArray geometry = s.value(QStringLiteral("geometry")).toByteArray();
    if (geometry.isEmpty()) {
        resize(1000, 800);  // the first-run size, which used to live in main()
    } else {
        restoreGeometry(geometry);
    }

    restoreState(s.value(QStringLiteral("toolbars")).toByteArray());

    const int points = s.value(QStringLiteral("commandFontPoints"), 0).toInt();
    if (points > 0) command_line_->set_font_points(points);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() != QEvent::KeyPress) return QMainWindow::eventFilter(watched, event);

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

void MainWindow::contextMenuEvent(QContextMenuEvent* event) {
    // Qt builds this from the toolbars that actually exist, so it cannot drift
    // as groups are added, and it lists hidden ones too -- which is the whole
    // point. A toolbar switched back on returns to where it was left, because
    // QMainWindow keeps a hidden toolbar's place in its layout rather than
    // forgetting it.
    //
    // Offered everywhere rather than only while something is hidden: a control
    // that appears only once you are stuck is one you cannot have learned
    // before you needed it. The text widgets keep their own copy/paste menus,
    // since those consume the click before it reaches here.
    const std::unique_ptr<QMenu> menu(createPopupMenu());
    if (!menu) return;
    menu->exec(event->globalPos());
    event->accept();
}

void MainWindow::quit_application() {
    // Said outright rather than left to Qt's quitOnLastWindowClosed.
    //
    // That default quits when the last top-level window closes, and a native
    // file dialog on macOS leaves a hidden QFileDialog behind that still counts
    // as one -- so after any Open or Save, closing the window left the process
    // running with nothing on screen and no way to reach it. Observed exactly
    // that: idle in a healthy event loop, zero windows, 0% CPU.
    //
    // This program is one window over one drawing, so its lifetime IS the
    // window's. Stating that costs a line and removes every dependence on what
    // Qt happens to be counting.
    QCoreApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Already settled: QUIT asked and was answered, or this question was.
    if (closing_ || !db_.journal().dirty()) {
        save_window_state();
        event->accept();
        quit_application();
        return;
    }

    // Three answers, for the reason QUIT gives for having three: without
    // Cancel there is no way to back out of a close you did not mean, and
    // two answers make "No" ambiguous between "do not save" and "do not
    // close". A window can offer the third properly, so it does.
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("NotoCAD"));
    box.setText(QStringLiteral("The drawing has unsaved changes."));
    box.setInformativeText(QStringLiteral("Save before closing?"));
    box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Save);

    switch (box.exec()) {
        case QMessageBox::Discard:
            closing_ = true;
            save_window_state();
            event->accept();
            quit_application();
            return;

        case QMessageBox::Save:
            // A save may need a file name, and asking for one puts up another
            // dialog -- which cannot happen inside this event. So the close is
            // refused and tried again once the save has actually finished,
            // which is also what makes a cancelled save leave the window open.
            event->ignore();
            close_after_save_ = true;
            run_command(QStringLiteral("QSAVE"));
            return;

        default:
            event->ignore();
            return;
    }
}

MainWindow::~MainWindow() {
    // Removed explicitly, because otherwise it outlives what it reads. The
    // filter is installed on qApp and stays installed until ~QObject, which
    // runs LAST -- after every member here is gone and after ~QWidget has
    // deleted command_line_, which the filter dereferences on its first line.
    // Any key delivered during teardown would land in freed memory.
    qApp->removeEventFilter(this);

    // And the widgets go BEFORE the things they hold references to.
    //
    // The ordering is otherwise inverted, and not by anyone's choice: db_ and
    // engine_ are members, so they are destroyed when this body finishes, while
    // the widgets are children of a QObject and are deleted later still, by
    // ~QWidget. ViewportWidget holds `const Database&` and `CommandEngine*`, so
    // between those two moments it is a live object pointing at dead ones.
    //
    // Nothing reaches it today -- ~ViewportWidget is defaulted and the only
    // event Qt may still deliver during teardown touches neither. That is a
    // property of the code as it stands rather than a guarantee, and any
    // handler added to ViewportWidget that reads db_ or engine_ would make it
    // an immediate use-after-free with no obvious cause.
    //
    // Deleting the central widget here takes the whole tree with it, in the one
    // place where the order is still ours to choose.
    delete takeCentralWidget();
    view_ = nullptr;
    command_line_ = nullptr;
}

void MainWindow::refresh_prompt() {
    command_line_->set_prompt(QString::fromStdString(session_->current_prompt()));
    offer_file_dialog();
}

void MainWindow::on_line_entered(const QString& line) {
    // R12's `~`: ask for the dialog at a file prompt even when FILEDIA says
    // not to offer one. The whole convention lives here rather than in the
    // parser, because wanting a dialog is a thing to say to a window and means
    // nothing at a terminal.
    if (line == QLatin1String("~") && engine_.active() &&
        engine_.prompt().file != FileIntent::None) {
        run_file_dialog();
        return;
    }

    command_line_->echo_input(QString::fromStdString(session_->current_prompt()), line);

    if (!session_->feed_line(line.toStdString())) {
        // QUIT has already asked whatever needed asking.
        closing_ = true;
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

    // A close that was waiting on a save. Retried only once the save has
    // really finished -- it may have gone through a file dialog and a version
    // question to get here -- and abandoned if the drawing is still dirty,
    // because a save that failed or was cancelled must not close the window
    // over the work it did not write.
    if (close_after_save_ && !engine_.active()) {
        close_after_save_ = false;
        if (!db_.journal().dirty()) {
            closing_ = true;
            close();
        }
    }
}

void MainWindow::on_point_picked(const QString& prompt, const QString& answer) {
    // A click answered a prompt inside the viewport, so the command line has to
    // catch up with a question it never saw the answer to.
    command_line_->echo_input(prompt, answer);

    // And with the ANSWER, when that click finished the command. The viewport
    // calls CommandEngine::supply() directly rather than feeding a line, so
    // nothing had reported the outcome: MEASUREGEOM worked out the distance and
    // never said it, and a command that failed on a click said nothing at all.
    session_->report_if_finished();

    refresh_prompt();
}

void MainWindow::on_cancel_requested() {
    // A close waiting on a save is abandoned here, not only in on_line_entered.
    // Cancelling the save dialog cancels the COMMAND, which never reaches that
    // path -- so the flag stayed set, and the window would then close by itself
    // the next time any command finished with the drawing clean. Refusing the
    // save has to refuse the close with it.
    close_after_save_ = false;

    // Escape is the engine's business, not each command's: committed work
    // survives it, as in R12.
    if (engine_.active()) {
        engine_.cancel();
        command_line_->append_output("*Cancel*\n");
    }
    refresh_prompt();
    // refresh_osnap rather than a plain repaint, for the reason the typed path
    // already does it: update_osnap clears the hit when nothing wants a point,
    // and a repaint alone left the marker and its label painted over a drawing
    // with no command running. Escaping out of LINE with the cursor sitting on
    // an endpoint showed an ENDPOINT square until the mouse was jiggled.
    view_->refresh_osnap();
    command_line_->focus_input();
}

bool MainWindow::session_idle() const {
    return !engine_.active() && !session_->continuing() && !session_->confirming_quit();
}

void MainWindow::on_copy_shortcut() {
    // A text selection is deliberate -- the transcript cannot even hold focus,
    // so one only exists because the user dragged across it -- and wins.
    if (command_line_->has_text_selection()) {
        command_line_->copy_selection();
        return;
    }
    if (session_idle()) on_line_entered(QStringLiteral("COPYCLIP"));
    // Mid-command with nothing selected there is nothing the chord can mean;
    // feeding COPYCLIP would be taken as the answer to the standing prompt.
}

void MainWindow::on_cut_shortcut() {
    if (command_line_->has_text_selection()) {
        command_line_->cut_selection();
        return;
    }
    if (session_idle()) on_line_entered(QStringLiteral("CUTCLIP"));
}

void MainWindow::on_paste_shortcut() {
    // Focus is the context, and supplying it is the user's job -- Sadie's
    // call, in place of sniffing the clipboard to guess intent. Focus in the
    // command line means characters, into the input; focus anywhere else
    // means geometry, and runs PASTECLIP, which says so itself when the
    // clipboard turns out not to hold DXF. The cost of the rule is that
    // pasting geometry right after typing means clicking the viewport first;
    // the gain is that nothing is ever decided by what the bytes look like.
    QWidget* focus = QApplication::focusWidget();
    if (focus != nullptr && command_line_->isAncestorOf(focus)) {
        command_line_->paste_into_input();
        return;
    }
    if (session_idle()) on_line_entered(QStringLiteral("PASTECLIP"));
    // Mid-command the chord has no meaning that would not be swallowed as
    // the answer to the standing prompt, so it does nothing, like Copy.
}

// --- toolbars ---------------------------------------------------------------

QToolBar* MainWindow::add_toolbar(const QString& title, Qt::ToolBarArea area,
                                  std::initializer_list<const char*> commands) {
    auto* bar = new QToolBar(title, this);
    bar->setObjectName(title);  // so a future saveState() has something to key on
    bar->setIconSize(QSize(22, 22));
    bar->setFloatable(false);

    // Styled per toolbar, NOT on the window. A stylesheet set on the
    // QMainWindow takes over styling for every child, which silently undoes
    // the command line's palette and leaves its transcript white on white --
    // found by looking at the window rather than by anything failing.
    bar->setStyleSheet(kToolbarStyle);

    for (const char* name : commands) {
        if (name[0] == '|') {
            bar->addSeparator();
            continue;
        }
        const QString command = QString::fromLatin1(name);

        // The tooltip teaches the keyboard rather than restating the picture:
        // the point of the program is the command line, and a button that
        // never mentions what to type keeps you clicking forever.
        QString tip = command;
        for (const CommandAlias& a : command_aliases()) {
            if (command.compare(QString::fromStdString(a.name), Qt::CaseInsensitive) == 0) {
                tip += QStringLiteral(" (") + QString::fromStdString(a.alias) + QLatin1Char(')');
                break;
            }
        }

        auto* act = bar->addAction(command_icon(command, kToolbarInk), command);
        act->setToolTip(tip);
        connect(act, &QAction::triggered, this, [this, command]() { run_command(command); });
    }

    addToolBar(area, bar);
    return bar;
}

void MainWindow::build_toolbars() {
    add_toolbar(QStringLiteral("File"), Qt::TopToolBarArea,
                {"NEW", "OPEN", "SAVE", "|", "UNDO", "REDO", "|", "ZOOM", "PAN", "PLAN", "|",
                 "UCS", "UCSICON", "VPOINT", "|", "LAYER", "MEASUREGEOM"});

    add_toolbar(QStringLiteral("Draw"), Qt::LeftToolBarArea,
                {"LINE", "PLINE", "CIRCLE", "ARC", "ELLIPSE", "SPLINE", "|", "POINT", "TEXT",
                 "SOLID", "|", "BLOCK", "INSERT"});

    add_toolbar(QStringLiteral("Modify"), Qt::RightToolBarArea,
                {"ERASE", "MOVE", "COPY", "|", "ROTATE", "SCALE", "MIRROR", "ARRAY", "STRETCH",
                 "|", "TRIM", "EXTEND", "BREAK", "|", "OFFSET", "FILLET", "CHAMFER", "|",
                 "EXPLODE"});
}

void MainWindow::run_command(const QString& name) {
    // An unterminated LISP form owns the next line, and feeding a command name
    // into it would become part of the expression rather than a command.
    if (session_->continuing()) {
        command_line_->append_error("Finish the AutoLISP expression first.\n");
        return;
    }
    if (session_->confirming_quit()) return;

    // R12: a command typed at a prompt cancels the one running, and a button
    // is the same act by another means. Committed work survives, as ever.
    if (engine_.active()) {
        engine_.cancel();
        command_line_->append_output("*Cancel*\n");
    }
    on_line_entered(name);
    command_line_->focus_input();
}

// --- file dialogs -----------------------------------------------------------

void MainWindow::offer_file_dialog() {
    if (!engine_.active()) {
        // Nothing is standing to answer, so anything the dialog was holding for
        // a later prompt is stale. Clearing here rather than at each exit is
        // what stops a stored answer surfacing in a command that never asked.
        file_prompt_token_.clear();
        pending_format_.clear();
        pending_overwrite_ok_ = false;
        return;
    }
    const Prompt& p = engine_.prompt();

    // Two questions the save dialog already asked. Putting them again on the
    // command line is asking twice, which is the whole complaint the file-type
    // list was added to answer.
    if (p.file_overwrite && pending_overwrite_ok_) {
        pending_overwrite_ok_ = false;
        QTimer::singleShot(0, this, [this]() { on_line_entered(QStringLiteral("Yes")); });
        return;
    }
    if (p.file_format && !pending_format_.isEmpty()) {
        const QString answer = pending_format_;
        pending_format_.clear();
        QTimer::singleShot(0, this, [this, answer]() { on_line_entered(answer); });
        return;
    }

    if (p.file == FileIntent::None) {
        file_prompt_token_.clear();
        return;
    }
    // FILEDIA 0 means type it, even here. That is what makes a script or a
    // LISP routine that drives OPEN safe to run in the window -- and the
    // reason `~` exists as the one-shot way back to the dialog.
    if (db_.sysvars().get_int(Sysvar::FileDia) == 0) return;

    const QString token =
        QString::fromLatin1(engine_.command_name()) + QLatin1Char('|') +
        QString::fromStdString(p.message);
    if (token == file_prompt_token_) return;  // already offered, and declined
    file_prompt_token_ = token;

    QTimer::singleShot(0, this, &MainWindow::run_file_dialog);
}

void MainWindow::run_file_dialog() {
    if (!engine_.active()) return;

    // By value: feeding the answer below advances the command, which replaces
    // the prompt this reference would have pointed at.
    const Prompt p = engine_.prompt();
    if (p.file == FileIntent::None) return;

    const QString ext = QString::fromStdString(p.file_extension);
    const QString caption = QString::fromStdString(p.message);

    // When the command is going to ask what format to write, the choice goes
    // in the dialog's file-type list -- which on macOS is the native "File
    // Format" popup, and is where people look for it. Each entry keeps the
    // same extension, so choosing one does not rename anything.
    QStringList filters;
    for (const std::string& format : p.file_formats) {
        filters << QStringLiteral("%1 (*.%2)").arg(QString::fromStdString(format), ext);
    }
    if (filters.isEmpty()) {
        filters << (ext.isEmpty() ? QStringLiteral("All files (*)")
                                  : QStringLiteral("%1 files (*.%2)").arg(ext.toUpper(), ext));
    }
    filters << QStringLiteral("All files (*)");

    QString chosen_filter;
    const QString filter = filters.join(QStringLiteral(";;"));
    const QString path =
        p.file == FileIntent::Open
            ? QFileDialog::getOpenFileName(this, caption, QString(), filter, &chosen_filter)
            : QFileDialog::getSaveFileName(this, caption, QString(), filter, &chosen_filter);

    if (path.isEmpty()) {
        // Declining the dialog cancels the command rather than dropping back to
        // typing: the prompt would otherwise still be standing with no way to
        // answer it that the user has not just refused.
        pending_format_.clear();
        pending_overwrite_ok_ = false;
        on_cancel_requested();
        return;
    }

    // Getting a path out of a SAVE dialog means any replacement was already
    // agreed to -- Qt confirms overwriting unless told not to, on every
    // platform -- so the command's own question has been answered.
    pending_overwrite_ok_ = p.file == FileIntent::Save;

    // Which format the file-type list was left on, matched back by name rather
    // than by position so a reordered list cannot silently change the answer.
    pending_format_.clear();
    for (const std::string& format : p.file_formats) {
        const QString label = QString::fromStdString(format);
        if (chosen_filter.startsWith(label + QLatin1String(" ("))) {
            pending_format_ = label;
            break;
        }
    }

    on_line_entered(path);
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
