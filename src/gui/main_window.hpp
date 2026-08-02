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

#include <QKeySequence>
#include <QMainWindow>

#include <memory>

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

    void refresh_prompt();

    // Routes PromptSession output into the command line widget.
    class WidgetOutput;

    Database db_;
    lisp::Context ctx_;
    lisp::Interp interp_;
    lisp::InterpScriptLoader script_loader_{interp_};
    CommandEngine engine_;

    ViewportWidget* view_{nullptr};
    CommandLineWidget* command_line_{nullptr};

    std::unique_ptr<WidgetOutput> output_;
    std::unique_ptr<app::PromptSession> session_;
};

}  // namespace ncad
