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
// The widget stays thin. It turns a click into whatever the running prompt
// asked for -- a world point, or the handle of the entity under the pick box --
// and hands it over. Every judgement behind that is a core call with its own
// headless test; what is left here is routing.
#pragma once

#include "noto/command.hpp"
#include "noto/osnap_search.hpp"
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

    // Re-runs the snap search under the stationary cursor and repaints. The
    // command line calls this after every entered line: typing an override, or
    // a command that draws something, both change what the cursor is over
    // without the mouse having moved.
    void refresh_osnap();

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

    // What a left click would mean right now. These are separate because the
    // answers differ: a point prompt takes a coordinate, an entity prompt takes
    // a handle, and supplying the wrong one fails the command.
    bool wants_point() const;
    bool wants_entity() const;
    bool wants_pick() const { return wants_point() || wants_entity(); }

    // R12 draws the pick box when selecting objects and the larger aperture
    // when snapping, so the cursor says which question is being asked. Both are
    // half-heights in pixels: the box is twice the value on a side.
    double pickbox_px() const;
    double aperture_px() const;

    // Screen to world, on the construction plane. Without a UCS the plane is
    // world XY, at the elevation of the rubber-band base when there is one --
    // so the second point of a LINE lands in the same plane as the first rather
    // than dropping to z=0.
    Vec3 pick_point(const QPoint& pos) const;

    void draw_rubber_band(QPainter& painter) const;

    // Markers are painted here rather than through Renderer on purpose: they
    // are screen-space and a fixed pixel size, which is the opposite of what a
    // world-space polyline interface describes. The aperture box has always
    // been drawn this way.
    void draw_osnap_marker(QPainter& painter) const;

    // Refreshes snap_ from the cursor position. Cheap when OSMODE is zero,
    // which is a drawing's default state.
    void update_osnap();

    const Database& db_;
    Viewport viewport_;
    CommandEngine* engine_{nullptr};
    Drag drag_{Drag::None};
    QPoint last_pos_;
    QPoint cursor_pos_;
    bool cursor_inside_{false};
    bool framed_{false};

    // The snap under the cursor, or an invalid hit. Cached between the move
    // that found it and the paint that draws it, so the search runs once per
    // mouse move rather than once per repaint.
    OsnapHit snap_{};
};

}  // namespace noto
