// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "viewport_widget.hpp"

#include "noto/database.hpp"
#include "noto/pick.hpp"
#include "noto/scene.hpp"
#include "noto/sysvar.hpp"
#include "qpainter_renderer.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

namespace noto {
namespace {

// R12 on a CRT: light geometry on a near-black field.
const QColor kBackground(24, 24, 28);
const QColor kRubberBand(180, 180, 190);
const QColor kCrosshair(90, 90, 100);

// One wheel notch is 120 eighths of a degree; this is the zoom per notch.
constexpr double kZoomPerNotch = 1.15;
constexpr double kDegreesPerNotch = 120.0;

}  // namespace

ViewportWidget::ViewportWidget(const Database& db, QWidget* parent) : QWidget(parent), db_(db) {
    setFocusPolicy(Qt::StrongFocus);
    // Needed for the rubber band and the crosshair: both follow the cursor with
    // no button held.
    setMouseTracking(true);
    // Qt would otherwise paint the widget background before paintEvent, which
    // costs a full-window fill we immediately overwrite.
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::CrossCursor);
    viewport_.set_size(width(), height());
}

bool ViewportWidget::next_value(const Prompt&, InputValue&) {
    // Never. The value arrives as a mouse event, not as an answer to this call;
    // returning false is what makes the engine suspend and hand control back to
    // the event loop instead of blocking inside it.
    return false;
}

void ViewportWidget::zoom_extents() {
    viewport_.zoom_extents(db_.extents());
    update();
}

void ViewportWidget::set_plan_view() {
    viewport_.set_plan_view();
    update();
}

bool ViewportWidget::wants_point() const {
    if (!engine_ || !engine_->active()) return false;
    return engine_->prompt().kind == PromptKind::Point;
}

bool ViewportWidget::wants_entity() const {
    if (!engine_ || !engine_->active()) return false;
    return engine_->prompt().kind == PromptKind::Entity;
}

double ViewportWidget::pickbox_px() const {
    return static_cast<double>(db_.sysvars().get_int(Sysvar::PickBox));
}

double ViewportWidget::aperture_px() const {
    return static_cast<double>(db_.sysvars().get_int(Sysvar::Aperture));
}

Vec3 ViewportWidget::pick_point(const QPoint& pos) const {
    const ScreenPoint sp{static_cast<double>(pos.x()), static_cast<double>(pos.y())};

    Vec3 plane_point{};
    if (engine_ && engine_->active() && engine_->prompt().has_base) {
        plane_point = engine_->prompt().base;
    } else if (engine_ && engine_->has_last_point()) {
        plane_point = engine_->last_point();
    }

    Vec3 hit{};
    if (viewport_.unproject(sp, plane_point, kWorldZ, &hit)) return hit;

    // World XY seen edge-on: there is no sensible point on it, so fall back to
    // the plane facing the viewer. Better than refusing the click outright.
    return viewport_.unproject_to_target_plane(sp);
}

void ViewportWidget::resizeEvent(QResizeEvent* event) {
    viewport_.set_size(width(), height());
    QWidget::resizeEvent(event);
}

void ViewportWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!framed_) {
        framed_ = true;
        zoom_extents();
    }
}

void ViewportWidget::leaveEvent(QEvent* event) {
    cursor_inside_ = false;
    update();
    QWidget::leaveEvent(event);
}

void ViewportWidget::draw_rubber_band(QPainter& painter) const {
    if (!cursor_inside_ || !wants_pick()) return;

    const ScreenPoint cursor{static_cast<double>(cursor_pos_.x()),
                             static_cast<double>(cursor_pos_.y())};

    // R12's pick indicator, sized by the variable that governs the query it
    // stands for: the pick box when selecting objects, the aperture when
    // snapping. Seeing the box change size is how you know which one is live.
    const double half = wants_entity() ? pickbox_px() : aperture_px();
    QPen pen(kCrosshair);
    pen.setWidth(0);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(cursor.x - half, cursor.y - half, half * 2.0, half * 2.0));

    if (!wants_point() || !engine_->prompt().has_base) return;

    // Rubber band from wherever the command says it starts. Prompt::base exists
    // for this and nothing else -- the engine never reads it.
    const ScreenPoint base = viewport_.project(engine_->prompt().base);
    if (!std::isfinite(base.x) || !std::isfinite(base.y)) return;

    QPen band(kRubberBand);
    band.setWidth(0);
    band.setStyle(Qt::DashLine);
    painter.setPen(band);
    painter.drawLine(QPointF(base.x, base.y), QPointF(cursor.x, cursor.y));
}

void ViewportWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), kBackground);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterRenderer renderer(painter, viewport_, db_);
    // The tolerance comes from the viewport, so tessellation tracks zoom: the
    // same circle costs eight segments across three pixels and hundreds across
    // the whole window.
    draw_database(db_, viewport_.draw_context(), renderer);

    draw_rubber_band(painter);
}

void ViewportWidget::mousePressEvent(QMouseEvent* event) {
    // Middle-drag pans and shift-middle-drag orbits, which is where AutoCAD put
    // them. The left button belongs to the running command.
    if (event->button() == Qt::MiddleButton) {
        drag_ = (event->modifiers() & Qt::ShiftModifier) ? Drag::Orbit : Drag::Pan;
        last_pos_ = event->pos();
        setCursor(drag_ == Drag::Pan ? Qt::ClosedHandCursor : Qt::SizeAllCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && wants_entity()) {
        const QString asked = QString::fromStdString(engine_->prompt().text());
        const ScreenPoint sp{static_cast<double>(event->pos().x()),
                             static_cast<double>(event->pos().y())};

        const PickResult r = pick_entity(db_, viewport_, sp, pickbox_px());
        if (!r.hit()) {
            // A miss is silent, as in R12: nothing is said and the prompt
            // stands. Announcing every empty click would be noise, since
            // missing is how you decide you have finished selecting.
            event->accept();
            return;
        }

        engine_->supply(InputValue::of_entity(r.entity));
        update();
        // The bare handle, which is exactly what could have been typed at this
        // prompt -- input_text.cpp parses a decimal handle here.
        emit pointPicked(asked, QString::number(r.entity));
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && wants_point()) {
        // Captured before supply(): afterwards the prompt is the next one, and
        // the transcript would attribute the answer to the wrong question.
        const QString asked = QString::fromStdString(engine_->prompt().text());
        const Vec3 p = pick_point(event->pos());

        // The whole point of the phase: a click is just another way to answer a
        // prompt, indistinguishable to the command from a typed coordinate.
        engine_->supply(InputValue::of_point(p));
        update();
        emit pointPicked(asked, QStringLiteral("%1,%2,%3")
                                    .arg(p.x, 0, 'f', 4)
                                    .arg(p.y, 0, 'f', 4)
                                    .arg(p.z, 0, 'f', 4));
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    cursor_pos_ = event->pos();
    cursor_inside_ = true;

    if (drag_ == Drag::None) {
        // Repaint only when something actually follows the cursor, so an idle
        // mouse over a large drawing costs nothing.
        if (wants_pick()) update();
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint delta = event->pos() - last_pos_;
    last_pos_ = event->pos();

    if (drag_ == Drag::Pan) {
        viewport_.pan_pixels(delta.x(), delta.y());
    } else {
        viewport_.orbit_pixels(delta.x(), delta.y());
    }
    update();
    event->accept();
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (drag_ != Drag::None && event->button() == Qt::MiddleButton) {
        drag_ = Drag::None;
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ViewportWidget::wheelEvent(QWheelEvent* event) {
    const double notches = event->angleDelta().y() / kDegreesPerNotch;
    if (notches == 0.0) {
        event->ignore();
        return;
    }

    // Zoom about the cursor, so the point under the pointer stays put.
    const QPointF pos = event->position();
    viewport_.zoom(std::pow(kZoomPerNotch, notches), ScreenPoint{pos.x(), pos.y()});
    update();
    event->accept();
}

void ViewportWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        emit cancelRequested();
        return;
    }

    // View control is on keys that cannot be confused with typing a command,
    // because everything printable belongs to the command line. ZOOM and PLAN
    // become real commands once they exist, and these become their shortcuts.
    if (event->key() == Qt::Key_Home) {
        if (event->modifiers() & Qt::ControlModifier) {
            set_plan_view();
        } else {
            zoom_extents();
        }
        return;
    }

    // Anything that produces text goes to the command line, which is how R12
    // behaves: there is no focus step, you just type.
    const QString text = event->text();
    if (!text.isEmpty() && text.at(0).isPrint()) {
        emit textTyped(text);
        return;
    }

    QWidget::keyPressEvent(event);
}

}  // namespace noto
