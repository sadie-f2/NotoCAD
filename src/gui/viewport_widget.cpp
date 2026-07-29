// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "viewport_widget.hpp"

#include "noto/dash.hpp"
#include "noto/database.hpp"
#include "noto/highlight.hpp"
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

const QColor kOsnapMarker(250, 200, 60);

// AutoCAD's axis colours, which are near enough universal now: X red, Y green,
// Z blue. Dimmed from full saturation because full red on the dark background
// glares next to width-0 wireframe.
const QColor kAxisX(220, 70, 70);
const QColor kAxisY(90, 200, 90);
const QColor kAxisZ(90, 140, 230);

// Fixed pixel geometry: the icon shows an orientation, not a size.
constexpr double kIconLength = 34.0;
constexpr double kIconMargin = 46.0;

// ACI 6, magenta: not a colour much geometry is drawn in, which is the only
// real requirement -- a highlight that matches the entity underneath says
// nothing. R12 highlighted by dashing instead, which is not available here
// without naming a linetype the drawing may not have.
constexpr std::int16_t kHighlightColor = 6;

// ACI 4, cyan: distinct from both the highlight and from what is likely to be
// underneath, since a ghost is read against the geometry it is about to become.
constexpr std::int16_t kGhostColor = 4;

// One wheel notch is 120 eighths of a degree; this is the zoom per notch.
constexpr double kZoomPerNotch = 1.15;
constexpr double kDegreesPerNotch = 120.0;

// Markers do not scale with zoom: they annotate the cursor, not the drawing.
constexpr double kMarkerHalfPx = 6.0;

// R12 remembered ten previous views.
constexpr std::size_t kMaxPreviousViews = 10;

// R12's glyph vocabulary. The shape carries the meaning -- you learn to read
// "square" as endpoint without reading the label -- so they must stay distinct
// from each other at six pixels rather than merely being drawn.
void paint_osnap_glyph(QPainter& p, OsnapType type, QPointF c, double h) {
    const double x = c.x();
    const double y = c.y();

    switch (type) {
        case OsnapType::Endpoint:
            p.drawRect(QRectF(x - h, y - h, h * 2.0, h * 2.0));
            break;

        case OsnapType::Midpoint: {
            const QPointF tri[3] = {{x, y - h}, {x - h, y + h}, {x + h, y + h}};
            p.drawPolygon(tri, 3);
            break;
        }

        case OsnapType::Center:
            p.drawEllipse(QRectF(x - h, y - h, h * 2.0, h * 2.0));
            break;

        case OsnapType::Node:
            p.drawEllipse(QRectF(x - h, y - h, h * 2.0, h * 2.0));
            p.drawLine(QPointF(x - h, y), QPointF(x + h, y));
            p.drawLine(QPointF(x, y - h), QPointF(x, y + h));
            break;

        case OsnapType::Quadrant: {
            const QPointF dia[4] = {{x, y - h}, {x + h, y}, {x, y + h}, {x - h, y}};
            p.drawPolygon(dia, 4);
            break;
        }

        case OsnapType::Intersection:
            p.drawLine(QPointF(x - h, y - h), QPointF(x + h, y + h));
            p.drawLine(QPointF(x - h, y + h), QPointF(x + h, y - h));
            break;

        case OsnapType::Insert: {
            const double s = h * 0.7;
            p.drawRect(QRectF(x - h, y - h, s * 2.0, s * 2.0));
            p.drawRect(QRectF(x - h + s, y - h + s, s * 2.0, s * 2.0));
            break;
        }

        case OsnapType::Perpendicular:
            // The right-angle mark, drawn open at the top left as R12 has it.
            p.drawLine(QPointF(x - h, y - h), QPointF(x - h, y + h));
            p.drawLine(QPointF(x - h, y + h), QPointF(x + h, y + h));
            p.drawLine(QPointF(x - h, y), QPointF(x, y));
            p.drawLine(QPointF(x, y), QPointF(x, y + h));
            break;

        case OsnapType::Tangent:
            p.drawEllipse(QRectF(x - h, y - h * 0.6, h * 2.0, h * 1.6));
            p.drawLine(QPointF(x - h, y - h), QPointF(x + h, y - h));
            break;

        case OsnapType::Nearest: {
            // An hourglass: two triangles meeting at the point itself.
            const QPointF bow[4] = {{x - h, y - h}, {x + h, y + h}, {x + h, y - h}, {x - h, y + h}};
            p.drawPolygon(bow, 4);
            break;
        }
    }
}

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

void ViewportWidget::push_view() {
    view_stack_.push_back(viewport_);
    // Oldest out first. Unbounded history of a thing nobody scrolls back
    // through is just memory.
    if (view_stack_.size() > kMaxPreviousViews) view_stack_.erase(view_stack_.begin());
}

void ViewportWidget::zoom_extents() {
    push_view();
    viewport_.zoom_extents(db_.extents());
    update();
}

void ViewportWidget::set_plan_view(const Vec3& normal) {
    // This is where the two parted company, now that UCS exists. Looking down
    // the construction plane's normal IS the plan view of it; the argument was
    // ignored while every plane was world XY, and honouring it is the whole of
    // what PLAN in a tilted UCS means.
    push_view();
    if (is_zero(normal) || near_equal(normalize(normal), kWorldZ, 1e-12)) {
        viewport_.set_plan_view();
    } else {
        viewport_.set_view_direction(normalize(normal));
    }
    update();
}

void ViewportWidget::set_view_direction(const Vec3& world_direction) {
    push_view();
    viewport_.set_view_direction(world_direction);
    update();
}

Vec3 ViewportWidget::view_direction() const { return viewport_.view_direction(); }

void ViewportWidget::zoom_window(const Vec3& a, const Vec3& b) {
    BBox box;
    box.expand(a);
    box.expand(b);
    if (!box.valid()) return;
    push_view();
    viewport_.zoom_extents(box);
    update();
}

void ViewportWidget::zoom_scale(double factor) {
    if (!(factor > 0.0)) return;
    push_view();
    viewport_.zoom(factor);
    update();
}

bool ViewportWidget::zoom_previous() {
    if (view_stack_.empty()) return false;
    viewport_ = view_stack_.back();
    view_stack_.pop_back();
    update();
    return true;
}

void ViewportWidget::pan(const Vec3& from, const Vec3& to) {
    push_view();
    const ScreenPoint a = viewport_.project(from);
    const ScreenPoint b = viewport_.project(to);
    if (!std::isfinite(a.x) || !std::isfinite(b.x)) return;
    viewport_.pan_pixels(b.x - a.x, b.y - a.y);
    update();
}

Basis ViewportWidget::view_basis() const { return viewport_.basis(); }

DrawContext ViewportWidget::draw_context() const { return viewport_.draw_context(); }

bool ViewportWidget::wants_point() const {
    if (!engine_ || !engine_->active()) return false;
    // Not "is this a Point prompt". A radius is answered by pointing at where
    // the circle should pass through, and an angle by pointing along it -- so
    // the click, the snap and the rubber band all belong at those prompts too.
    return prompt_takes_point(engine_->prompt().kind);
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
    snap_ = OsnapHit{};
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

    if (engine_->prompt().rubber_band == RubberBand::Box) {
        // A selection window, drawn where it actually is: screen-aligned, with
        // the anchor at the opposite corner. Normalised because the corners
        // arrive in whichever order they were clicked, and QRectF with a
        // negative extent draws nothing at all.
        //
        // Which of window and crossing this is comes from the typed W or C
        // keyword, so the box does not have to say -- the prompt already does,
        // wording itself "First corner of crossing" either way.
        painter.drawRect(
            QRectF(QPointF(base.x, base.y), QPointF(cursor.x, cursor.y)).normalized());
        return;
    }

    painter.drawLine(QPointF(base.x, base.y), QPointF(cursor.x, cursor.y));
}

void ViewportWidget::draw_ucs_icon(QPainter& painter) const {
    const int mode = db_.sysvars().get_int(Sysvar::UcsIcon);
    if (mode == 0) return;  // UCSICON OFF

    const Ucs ucs = db_.current_ucs().normalized();
    const ScreenPoint o = viewport_.project(ucs.origin);
    if (!std::isfinite(o.x) || !std::isfinite(o.y)) return;

    // Where the icon sits. Mode 2 is "at the origin", but only when the origin
    // is actually on screen -- R12 falls back to the corner rather than drawing
    // it somewhere it cannot be seen, which is also what stops the icon
    // vanishing the moment you pan away from it.
    QPointF anchor(kIconMargin, height() - kIconMargin);
    if (mode == 2) {
        const QPoint op(static_cast<int>(o.x), static_cast<int>(o.y));
        if (rect().adjusted(kIconLength, kIconLength, -kIconLength, -kIconLength).contains(op)) {
            anchor = QPointF(o.x, o.y);
        }
    }

    // Orientation comes from projecting each axis; the drawn length is fixed in
    // pixels, so the icon reads the same at any zoom. Screen deltas for a unit
    // world vector scale with the zoom, so an axis is judged edge-on by its
    // size RELATIVE to the largest of the three rather than against a constant
    // -- zoomed far out, all three are tiny and none of them is edge-on.
    struct Axis {
        QPointF d;
        double len;
        QColor colour;
        const char* label;
    };

    auto screen_delta = [&](const Vec3& dir) {
        const ScreenPoint p = viewport_.project(ucs.origin + dir);
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return QPointF(0, 0);
        // Y is flipped on the way in already; this is pure screen space.
        return QPointF(p.x - o.x, p.y - o.y);
    };

    Axis axes[3] = {
        {screen_delta(ucs.xdir), 0.0, kAxisX, "X"},
        {screen_delta(ucs.ydir), 0.0, kAxisY, "Y"},
        {screen_delta(ucs.zdir()), 0.0, kAxisZ, "Z"},
    };

    double longest = 0.0;
    for (Axis& a : axes) {
        a.len = std::hypot(a.d.x(), a.d.y());
        longest = std::max(longest, a.len);
    }
    if (longest <= 0.0) return;

    painter.save();
    QFont font = painter.font();
    font.setPointSizeF(std::max(6.0, font.pointSizeF() - 1.0));
    painter.setFont(font);

    for (const Axis& a : axes) {
        // Pointing at or away from the eye: it would draw as a stub of
        // meaningless direction, so it is left out. Seeing Z disappear as the
        // view goes to plan is correct and is how R12's icon behaves.
        if (a.len < longest * 0.05) continue;

        const QPointF unit(a.d.x() / a.len, a.d.y() / a.len);
        const QPointF tip = anchor + unit * kIconLength;

        QPen pen(a.colour);
        pen.setWidth(0);
        painter.setPen(pen);
        painter.drawLine(anchor, tip);
        painter.drawText(tip + unit * 6.0 + QPointF(-3, 4), QString::fromLatin1(a.label));
    }

    // R12 marks the world system with a W on the icon. It is the cheapest way
    // to answer "am I typing coordinates in the system I think I am", which is
    // the question the icon exists for.
    if (ucs.is_world()) {
        QPen pen(kRubberBand);
        pen.setWidth(0);
        painter.setPen(pen);
        painter.drawText(anchor + QPointF(-14, 14), QStringLiteral("W"));
    }

    painter.restore();
}

void ViewportWidget::refresh_osnap() {
    update_osnap();
    update();
}

void ViewportWidget::update_osnap() {
    snap_ = OsnapHit{};
    if (!cursor_inside_ || !wants_point()) return;

    OsnapQuery q;
    // A typed override replaces OSMODE entirely for this one pick, rather than
    // adding to it -- that is what makes "cen" mean cen and nothing else. An
    // override of kOsnapNone is NON: snap to nothing, deliberately.
    q.mask = engine_->has_osnap_override()
                 ? engine_->osnap_override()
                 : static_cast<OsnapMask>(db_.sysvars().get_int(Sysvar::OsMode));
    if (q.mask == kOsnapNone) return;

    q.aperture_px = aperture_px();
    // The construction-plane point under the cursor -- what NEAREST measures to,
    // and the same point a click would produce.
    q.reference = pick_point(cursor_pos_);
    q.has_reference = true;

    // Where this segment starts. PER and TAN are measured from here, not from
    // the cursor. Prompt::base is the rubber-band origin, which is exactly the
    // point "perpendicular to that, from here" means.
    if (engine_->prompt().has_base) {
        q.from_point = engine_->prompt().base;
        q.has_from_point = true;
    } else if (engine_->has_last_point()) {
        q.from_point = engine_->last_point();
        q.has_from_point = true;
    }

    const ScreenPoint sp{static_cast<double>(cursor_pos_.x()),
                         static_cast<double>(cursor_pos_.y())};
    snap_ = osnap_search(db_, viewport_, sp, q);
}

void ViewportWidget::draw_osnap_marker(QPainter& painter) const {
    if (!snap_.valid) return;

    const ScreenPoint sp = viewport_.project(snap_.pos);
    if (!std::isfinite(sp.x) || !std::isfinite(sp.y)) return;

    QPen pen(kOsnapMarker);
    pen.setWidth(0);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    paint_osnap_glyph(painter, snap_.type, QPointF(sp.x, sp.y), kMarkerHalfPx);

    // R12 put the active mode on the status line. There is no status line yet
    // and no OSNAP command, so naming it beside the glyph is how you can tell
    // which snap you are about to get.
    painter.drawText(QPointF(sp.x + kMarkerHalfPx + 4.0, sp.y - kMarkerHalfPx - 2.0),
                     QString::fromLatin1(osnap_name(snap_.type)));
}

void ViewportWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), kBackground);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterRenderer renderer(painter, viewport_, db_);
    // Dashes are cut here, between the scene walk and the backend, so QPainter
    // and a future GL backend share one implementation -- and so the hit-test
    // probes, which drive Entity::draw() directly, never see the gaps.
    DashRenderer dashed(renderer, db_, db_.sysvars().get_real(Sysvar::LtScale));
    // The tolerance comes from the viewport, so tessellation tracks zoom: the
    // same circle costs eight segments across three pixels and hundreds across
    // the whole window.
    const DrawContext ctx = viewport_.draw_context();

    // What the running command would do if the cursor were clicked now. Derived
    // afresh every paint and never stored, which is what stops it going stale
    // behind an undo -- see inflight.hpp. Cheap when nothing is running: the
    // engine answers false without asking the command.
    InFlight flight;
    if (engine_ != nullptr && cursor_inside_ && wants_point()) {
        // The same point a click would supply, snap included. A ghost that
        // ignored the snap would sit somewhere the click will not land, which
        // is worse than no ghost.
        const Vec3 p = snap_.valid ? snap_.pos : pick_point(cursor_pos_);
        engine_->preview(InputValue::of_point(p), flight);
    }

    draw_database(db_, ctx, dashed, flight.suppressed);

    // Then the selection, over the top and in one colour. A second pass rather
    // than a flag on the first: the alternative is teaching draw_database what
    // a selection is, and the whole point of the wrapper is that it does not
    // need to know. Costs one extra walk of the selection, not of the drawing.
    //
    // Skipped while ghosts are showing: the ghosts already say which entities
    // are in play, and highlighting the originals underneath them as well would
    // draw the selection twice in two colours.
    if (engine_ != nullptr && flight.empty() && !engine_->selection().empty()) {
        HighlightRenderer hi(dashed, kHighlightColor);
        draw_handles(db_, ctx, hi, engine_->selection().handles());
    }

    if (!flight.empty()) {
        HighlightRenderer hi(dashed, kGhostColor);
        draw_entities(flight.ghosts, ctx, hi);
    }

    draw_ucs_icon(painter);
    draw_rubber_band(painter);
    draw_osnap_marker(painter);
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

        // Carries where it was picked, not only which one. BREAK takes the pick
        // point as its first break point, which is R12's sequence and cannot be
        // recovered from a handle.
        engine_->supply(InputValue::of_picked_entity(r.entity, pick_point(event->pos())));
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
        // Refreshed rather than trusted: snap_ is otherwise only updated on
        // movement, so a click with no intervening move -- the first click of a
        // command, say -- could otherwise use a snap found for a previous
        // prompt, at a position the cursor is no longer being asked about.
        cursor_pos_ = event->pos();
        cursor_inside_ = true;
        update_osnap();

        // The payoff for all of it: when a snap is showing, the click takes
        // that exact point rather than wherever the cursor happened to be.
        const Vec3 p = snap_.valid ? snap_.pos : pick_point(event->pos());

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
        if (wants_pick()) {
            update_osnap();
            update();
        }
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
            set_plan_view(kWorldZ);
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
