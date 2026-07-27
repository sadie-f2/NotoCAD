// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The application window: viewport above, command line below.
//
// It owns the drawing, the interpreter and the engine, and connects them. All
// of the semantics live elsewhere -- PromptSession decides what a typed line
// means (the same code `ncad` runs), CommandEngine holds the suspended command,
// and the viewport turns clicks into points. This class is wiring.
#pragma once

#include "noto/command.hpp"
#include "noto/database.hpp"
#include "noto/lisp/eval.hpp"
#include "prompt.hpp"

#include <QMainWindow>

#include <memory>

namespace noto {

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
    void on_text_typed(const QString& text);

private:
    void refresh_prompt();

    // Routes PromptSession output into the command line widget.
    class WidgetOutput;

    Database db_;
    lisp::Context ctx_;
    lisp::Interp interp_;
    CommandEngine engine_;

    ViewportWidget* view_{nullptr};
    CommandLineWidget* command_line_{nullptr};

    std::unique_ptr<WidgetOutput> output_;
    std::unique_ptr<app::PromptSession> session_;
};

}  // namespace noto
