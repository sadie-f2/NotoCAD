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

#include "ncad/command.hpp"
#include "ncad/osnap_search.hpp"
#include "ncad/view_control.hpp"
#include "ncad/viewport.hpp"

#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QWidget>

#include <vector>

namespace ncad {

class Database;

class ViewportWidget : public QWidget, public InputSource, public ViewControl {
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

    // ViewControl. The commands reach the view through these; the keyboard
    // shortcuts below call the same ones, so ZOOM and Home cannot drift apart.
    void set_plan_view(const Vec3& normal) override;
    void set_view_direction(const Vec3& world_direction) override;
    Vec3 view_direction() const override;
    void zoom_extents() override;
    void zoom_window(const Vec3& a, const Vec3& b) override;
    void zoom_scale(double factor) override;
    bool zoom_previous() override;
    void pan(const Vec3& from, const Vec3& to) override;
    Basis view_basis() const override;
    DrawContext draw_context() const override;

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

    // Escape. Handled by the engine rather than by each command, so that
    // committed work survives it as in R12.
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

    // The world point a click would supply RIGHT NOW. One implementation
    // because the click, the in-flight ghost and the rubber band must agree
    // about where the cursor is; three copies of "snap, or ortho, or neither"
    // would drift the moment any one of them changed.
    Vec3 cursor_point() const;

    // ORTHO. Constrains `p` to the current UCS's X or Y axis from the standing
    // prompt's base, keeping whichever component is larger.
    Vec3 apply_ortho(const Vec3& p) const;

    void draw_rubber_band(QPainter& painter) const;

    // Markers are painted here rather than through Renderer on purpose: they
    // are screen-space and a fixed pixel size, which is the opposite of what a
    // world-space polyline interface describes. The aperture box has always
    // been drawn this way.
    void draw_osnap_marker(QPainter& painter) const;

    // The corner nameplate. Painted rather than made a child widget, which is
    // what makes it transparent to input: a painted thing cannot receive an
    // event at all, so there is no flag to get wrong and nothing to sit in the
    // way of a pick in that corner.
    void draw_nameplate(QPainter& painter) const;

    // The UCS icon, honouring UCSICON's 0/1/2. Screen-space for the same
    // reason as the markers above: it is a fixed pixel size showing an
    // orientation, not geometry that lives in the drawing.
    void draw_ucs_icon(QPainter& painter) const;

    // The crosshairs, drawn rather than left to the window system, so they lie
    // along the current UCS instead of along the screen. Length from CURSORSIZE.
    void draw_cursor(QPainter& painter) const;

    // The current UCS axes as unit screen directions. Shared by the icon and
    // the cursor so the two cannot disagree about which way the construction
    // plane points -- they are the same statement drawn at two sizes.
    //
    // `usable` is false for an axis pointing at or away from the eye, which
    // projects to nothing and would otherwise draw as a stub in a meaningless
    // direction.
    struct AxisFrame {
        QPointF dir[3]{};
        QColor colour[3]{};
        bool usable[3]{false, false, false};
        bool valid{false};
    };
    AxisFrame ucs_axis_frame() const;

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

    // ZOOM Previous. Whole view states, not zoom rectangles: whether a change
    // of view direction should be undone by Previous is a policy question, and
    // storing only an extent would answer it permanently and by accident.
    void push_view();
    std::vector<Viewport> view_stack_;
};

}  // namespace ncad
