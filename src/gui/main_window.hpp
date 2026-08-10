// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The application window: viewport above, command line below.
//
// It owns the drawing, the interpreter and the engine, and connects them. All
// of the semantics live elsewhere -- PromptSession decides what a typed line
// means (the same code `ncad` runs), CommandEngine holds the suspended command,
// and the viewport turns clicks into points. This class is wiring.
#pragma once

#include "ncad/command.hpp"
#include "ncad/database.hpp"
#include "ncad/lisp/eval.hpp"
#include "ncad/lisp/interp_script_loader.hpp"
#include "prompt.hpp"
#include "qt_clipboard.hpp"

#include <QKeySequence>
#include <QMainWindow>

#include <memory>

class QToolBar;

namespace ncad {

class CommandLineWidget;
class ViewportWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    Database& database() { return db_; }
    ViewportWidget* viewport_widget() { return view_; }

private slots:
    void on_line_entered(const QString& line);
    void on_cancel_requested();
    void on_point_picked(const QString& prompt, const QString& answer);

protected:
    // Printable keystrokes reach the command line from wherever focus happens
    // to be. R12 has no focus step -- you just type -- and this is what makes
    // that true regardless of what was clicked last.
    bool eventFilter(QObject* watched, QEvent* event) override;

    // Set while a key is being handed to the command line, so the re-delivered
    // event cannot come back round and recur.
    bool routing_key_{false};

private:
    // Binds one key sequence to a text-size step. Separate so the three
    // bindings cannot drift apart.
    void add_zoom_shortcut(const QKeySequence& keys, int delta);

    // The toolbars. Every button does exactly what typing the command does --
    // it feeds the name to the same PromptSession -- so a button can never
    // mean something the command line does not, and no command needs a second
    // implementation to be clickable.
    void build_toolbars();
    QToolBar* add_toolbar(const QString& title, Qt::ToolBarArea area,
                          std::initializer_list<const char*> commands);
    void run_command(const QString& name);

    // A file prompt is standing and FILEDIA says to offer a dialog for it.
    // Deferred out of the current event, because a modal window must not open
    // inside the key handler that led to it.
    void offer_file_dialog();
    void run_file_dialog();

    // Copy, Cut and Paste each mean two things in a CAD window -- characters
    // or geometry -- and this is where the two are told apart. Copy and Cut
    // go by evidence: a text selection wins, otherwise an idle Command:
    // prompt runs COPYCLIP/CUTCLIP exactly as if typed. Paste goes by focus:
    // in the command line it is characters, anywhere else it is PASTECLIP.
    void on_copy_shortcut();
    void on_cut_shortcut();
    void on_paste_shortcut();

    // True at a plain Command: prompt -- no command mid-flight, no LISP form
    // left open, no QUIT confirmation pending. The only state in which a
    // shortcut may feed a command name without it being swallowed as the
    // answer to some other question.
    bool session_idle() const;

    void refresh_prompt();

    // Routes PromptSession output into the command line widget.
    class WidgetOutput;

    Database db_;
    lisp::Context ctx_;
    lisp::Interp interp_;
    lisp::InterpScriptLoader script_loader_{interp_};
    QtClipboard clipboard_;
    CommandEngine engine_;

    ViewportWidget* view_{nullptr};
    CommandLineWidget* command_line_{nullptr};

    std::unique_ptr<WidgetOutput> output_;
    std::unique_ptr<app::PromptSession> session_;

    // Which file prompt has already been offered a dialog, so that declining
    // one does not immediately raise it again. Command name and prompt text
    // together, because one command asks for more than one file.
    QString file_prompt_token_;
};

}  // namespace ncad
