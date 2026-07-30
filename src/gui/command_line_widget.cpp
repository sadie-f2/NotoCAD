// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "command_line_widget.hpp"

#include <QColor>
#include <QEvent>
#include <QFontDatabase>

#include <algorithm>
#include <QSizePolicy>
#include <QHBoxLayout>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QPalette>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QVBoxLayout>

namespace noto {
namespace {

// The command line is styled to match the viewport rather than the desktop
// theme. The two sit against each other with no border between them, and a
// light panel beside a black viewport reads as an unfinished window -- and the
// output colours below are chosen for a dark ground, so inheriting a light
// theme would leave the text nearly invisible.
const QColor kPanelBackground(18, 18, 22);
const QColor kNormalText(220, 220, 220);
const QColor kErrorText(255, 120, 110);
const QColor kEchoText(150, 150, 160);

// Enough scrollback to follow a session, bounded so a LISP loop printing in a
// tight loop cannot grow the widget without limit.
constexpr int kMaxScrollbackLines = 2000;

// A font may report a pixel size rather than a point size, in which case
// pointSize() is -1 and there is nothing to step from.
constexpr int kFallbackFontPoints = 10;

// Wide enough to cover an unscaled 4K panel at one end and a projector at the
// other. Clamped rather than unbounded: a zero or negative size is not a small
// font, it is an invisible one.
constexpr int kMinFontPoints = 5;
constexpr int kMaxFontPoints = 48;

}  // namespace

CommandLineWidget::CommandLineWidget(QWidget* parent) : QWidget(parent) {
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font_points_ = mono.pointSize() > 0 ? mono.pointSize() : kFallbackFontPoints;

    QPalette dark;
    dark.setColor(QPalette::Base, kPanelBackground);
    dark.setColor(QPalette::Window, kPanelBackground);
    dark.setColor(QPalette::Text, kNormalText);
    dark.setColor(QPalette::WindowText, kNormalText);
    // Scrollbars take their colour from the Button roles, not Base, and a light
    // scrollbar across the bottom of a dark panel is the one bit of the desktop
    // theme that shows through otherwise.
    dark.setColor(QPalette::Button, kPanelBackground);
    dark.setColor(QPalette::ButtonText, kNormalText);
    setPalette(dark);
    setAutoFillBackground(true);

    history_ = new QPlainTextEdit(this);
    history_->setReadOnly(true);
    // Never takes keyboard focus. It is read-only, so anything typed into it
    // vanishes without a trace -- which is indistinguishable from the
    // application having stopped responding to the keyboard.
    history_->setFocusPolicy(Qt::NoFocus);
    history_->setFont(mono);
    history_->setMaximumBlockCount(kMaxScrollbackLines);
    history_->setFrameShape(QFrame::NoFrame);
    // Wrapping off: coordinate output lines up in columns and rewrapping it
    // makes a transcript much harder to read back.
    history_->setLineWrapMode(QPlainTextEdit::NoWrap);

    prompt_ = new QLabel(this);
    prompt_->setFont(mono);
    prompt_->setTextInteractionFlags(Qt::NoTextInteraction);
    // Fixed, not Preferred: a Preferred label will shrink below its sizeHint
    // when the line edit wants room, which silently clips the prompt to
    // "Specify n". The prompt is the one thing on screen that must be readable.
    prompt_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    input_ = new QLineEdit(this);
    input_->setFont(mono);
    input_->setFrame(false);
    input_->installEventFilter(this);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(prompt_);
    row->addWidget(input_, 1);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(4, 2, 4, 2);
    column->setSpacing(2);
    column->addWidget(history_, 1);
    column->addLayout(row);

    connect(input_, &QLineEdit::returnPressed, this, &CommandLineWidget::on_return_pressed);
}

void CommandLineWidget::append(const QString& text, const QColor& color) {
    if (text.isEmpty()) return;

    // Trailing newlines are the caller's line terminator, not a blank line to
    // reproduce: appendPlainText already starts a new block.
    QString body = text;
    while (body.endsWith('\n')) body.chop(1);

    QTextCharFormat format;
    format.setForeground(color);
    history_->mergeCurrentCharFormat(format);
    history_->appendPlainText(body);
    history_->verticalScrollBar()->setValue(history_->verticalScrollBar()->maximum());
}

void CommandLineWidget::set_prompt(const QString& text) { prompt_->setText(text); }

void CommandLineWidget::append_output(const QString& text) { append(text, kNormalText); }

void CommandLineWidget::append_error(const QString& text) { append(text, kErrorText); }

void CommandLineWidget::echo_input(const QString& prompt, const QString& text) {
    append(prompt + text, kEchoText);
}

void CommandLineWidget::insert_typed_text(const QString& text) {
    input_->setFocus(Qt::OtherFocusReason);
    input_->insert(text);
}

void CommandLineWidget::deliver_key(QKeyEvent* event) {
    input_->setFocus(Qt::OtherFocusReason);
    QCoreApplication::sendEvent(input_, event);
}

void CommandLineWidget::focus_input() { input_->setFocus(Qt::OtherFocusReason); }

bool CommandLineWidget::input_has_focus() const { return input_->hasFocus(); }

void CommandLineWidget::on_return_pressed() {
    const QString line = input_->text();
    input_->clear();

    // Blank lines are meaningful -- Enter repeats the last command -- but they
    // are not worth recalling.
    if (!line.isEmpty()) {
        recall_.append(line);
        if (recall_.size() > 200) recall_.removeFirst();
    }
    recall_pos_ = recall_.size();

    emit lineEntered(line);
}

bool CommandLineWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched != input_ || event->type() != QEvent::KeyPress) {
        return QWidget::eventFilter(watched, event);
    }

    auto* key = static_cast<QKeyEvent*>(event);
    switch (key->key()) {
        case Qt::Key_Escape:
            // R12: Escape cancels the command, it does not clear the line only.
            input_->clear();
            emit cancelRequested();
            return true;

        case Qt::Key_Up:
            if (recall_pos_ > 0) {
                --recall_pos_;
                input_->setText(recall_.value(recall_pos_));
            }
            return true;

        case Qt::Key_Down:
            if (recall_pos_ < recall_.size() - 1) {
                ++recall_pos_;
                input_->setText(recall_.value(recall_pos_));
            } else {
                recall_pos_ = recall_.size();
                input_->clear();
            }
            return true;

        case Qt::Key_Space:
            // At a command prompt space acts as Enter, as R12 has it -- but not
            // mid-word, where it is separating answers on one line, and not at a
            // string prompt, where a file name may contain spaces. Both of those
            // are decided by split_prompt_line, so the widget stays out of it
            // and only forwards the keystroke.
            break;

        default:
            break;
    }
    return QWidget::eventFilter(watched, event);
}

void CommandLineWidget::set_font_points(int points) {
    const int clamped = std::clamp(points, kMinFontPoints, kMaxFontPoints);
    if (clamped == font_points_ && history_ != nullptr && history_->font().pointSize() == clamped) {
        return;
    }
    font_points_ = clamped;

    // All three together. They are read as one panel, so a transcript in one
    // size above an input line in another is worse than either size alone.
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(font_points_);
    history_->setFont(f);
    prompt_->setFont(f);
    input_->setFont(f);
}

}  // namespace noto
