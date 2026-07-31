// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The R12 command line: a scrollback of what has happened and one input line.
//
// It knows nothing about commands. It emits the line that was typed and shows
// whatever it is told to show -- PromptSession decides what a line means, in
// exactly the same code `ncad` runs. The widget's only opinion is that the
// prompt text sits in front of the input, as R12 has it, rather than being
// printed into the scrollback where it would scroll away.
#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;

class QKeyEvent;

namespace ncad {

class CommandLineWidget : public QWidget {
    Q_OBJECT

public:
    explicit CommandLineWidget(QWidget* parent = nullptr);

    void set_prompt(const QString& text);
    void append_output(const QString& text);
    void append_error(const QString& text);

    // Echoes what the user typed into the scrollback, prefixed with the prompt
    // that was showing -- otherwise the history reads as a list of bare words
    // with no record of what was being asked.
    void echo_input(const QString& prompt, const QString& text);

    // Types text into the input line and takes focus. This is how a keypress in
    // the viewport reaches the command line, which is what R12 does: there is
    // no separate "focus the command line" step, you just start typing.
    void insert_typed_text(const QString& text);

    // Hands a key press to the input as though it had been focused all along:
    // takes focus, then re-delivers the event. The one mechanism by which a
    // keystroke landing anywhere else in the window reaches the command line,
    // whatever key it is.
    void deliver_key(QKeyEvent* event);

    void focus_input();
    bool input_has_focus() const;

    // Text size, in points, for the transcript, the prompt and the input --
    // they share one size because they are read as one thing.
    //
    // Adjustable at run time because a high-resolution display driven WITHOUT
    // desktop scaling makes the system's default fixed font unreadably small,
    // and that is a per-machine fact the application cannot guess.
    void set_font_points(int points);
    int font_points() const { return font_points_; }
    void step_font_size(int delta) { set_font_points(font_points_ + delta); }

signals:
    void lineEntered(const QString& line);

    // Escape at the command line cancels the running command, as in R12.
    void cancelRequested();

private slots:
    void on_return_pressed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;


private:
    void append(const QString& text, const QColor& color);

    QPlainTextEdit* history_{nullptr};
    QLabel* prompt_{nullptr};
    QLineEdit* input_{nullptr};

    int font_points_{0};  // 0 until the system default is read in the ctor

    QStringList recall_;
    qsizetype recall_pos_{0};  // qsizetype, not int: it indexes recall_
};

}  // namespace ncad
