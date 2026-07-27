// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The viewport: draws the database, and answers prompts by picking points.
//
// It is an InputSource whose next_value() always returns false. That is not a
// stub -- it is the design. A GUI has no value to hand over when asked, because
// the value arrives later as an event; returning false makes the engine suspend
// and give control back, and the mouse handler calls CommandEngine::supply()
// when the click actually happens. There is a test pinning that the engine
// suspends rather than blocking on exactly this kind of source.
//
// The widget stays thin: it converts a click into a world point and hands it
// over. It decides nothing about what the point means.
#pragma once

#include "noto/command.hpp"
#include "noto/viewport.hpp"

#include <QPoint>
#include <QWidget>

namespace noto {

class Database;

class ViewportWidget : public QWidget, public InputSource {
    Q_OBJECT

public:
    explicit ViewportWidget(const Database& db, QWidget* parent = nullptr);

    Viewport& viewport() { return viewport_; }
    const Viewport& viewport() const { return viewport_; }

    // Not owned. The viewport reads the prompt to know whether a click means
    // anything, and calls supply() when it does.
    void set_engine(CommandEngine* engine) { engine_ = engine; }

    // InputSource. Always false: see the note above.
    bool next_value(const Prompt& prompt, InputValue& out) override;

    void zoom_extents();
    void set_plan_view();

signals:
    // A click answered a prompt, so the command line needs to catch up. Carries
    // the prompt as it read before the click and the point that answered it:
    // R12 echoes picked coordinates into the transcript, and without them the
    // history shows a prompt with no visible answer.
    void pointPicked(const QString& prompt, const QString& answer);

    // Typing in the viewport belongs to the command line. R12 has no separate
    // step for focusing it -- you just start typing.
    void textTyped(const QString& text);

    void cancelRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    enum class Drag { None, Pan, Orbit };

    // True when the running command wants a point, so a click means something
    // and a rubber band is worth drawing.
    bool wants_point() const;

    // Screen to world, on the construction plane. Without a UCS the plane is
    // world XY, at the elevation of the rubber-band base when there is one --
    // so the second point of a LINE lands in the same plane as the first rather
    // than dropping to z=0.
    Vec3 pick_point(const QPoint& pos) const;

    void draw_rubber_band(QPainter& painter) const;

    const Database& db_;
    Viewport viewport_;
    CommandEngine* engine_{nullptr};
    Drag drag_{Drag::None};
    QPoint last_pos_;
    QPoint cursor_pos_;
    bool cursor_inside_{false};
    bool framed_{false};
};

}  // namespace noto
