// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "qpainter_renderer.hpp"

#include "noto/database.hpp"

#include <algorithm>
#include <cmath>

namespace noto {
namespace {

// The first nine ACI slots, which are the ones R12 users actually set by hand
// and the only ones the format pins down by name. Index 7 is the drawing
// default: white on a dark background, as R12 on a CRT.
constexpr struct {
    int r, g, b;
} kAciLow[10] = {
    {255, 255, 255},  // 0 BYBLOCK, resolved before it gets here
    {255, 0, 0},      // 1 red
    {255, 255, 0},    // 2 yellow
    {0, 255, 0},      // 3 green
    {0, 255, 255},    // 4 cyan
    {0, 0, 255},      // 5 blue
    {255, 0, 255},    // 6 magenta
    {255, 255, 255},  // 7 white
    {128, 128, 128},  // 8 dark grey
    {192, 192, 192},  // 9 light grey
};

// Screen coordinates beyond this are refused rather than handed to QPainter.
// Qt's raster engine works in 26.6 fixed point and wraps on large values, so a
// deeply zoomed-in view would otherwise draw lines in visibly wrong places
// instead of simply off-screen. Clipping happens well before that limit.
constexpr double kCoordinateLimit = 1.0e7;

// Liang-Barsky. Clips the segment to `rect`, returning false if it lies wholly
// outside. Works in double precision, before anything reaches Qt.
bool clip_segment(const QRectF& rect, QPointF& a, QPointF& b) {
    double t0 = 0.0;
    double t1 = 1.0;
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();

    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {a.x() - rect.left(), rect.right() - a.x(), a.y() - rect.top(),
                         rect.bottom() - a.y()};

    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) return false;  // parallel to this edge and outside it
            continue;
        }
        const double t = q[i] / p[i];
        if (p[i] < 0.0) {
            if (t > t1) return false;
            if (t > t0) t0 = t;
        } else {
            if (t < t0) return false;
            if (t < t1) t1 = t;
        }
    }

    const QPointF a0 = a;
    a = QPointF(a0.x() + t0 * dx, a0.y() + t0 * dy);
    b = QPointF(a0.x() + t1 * dx, a0.y() + t1 * dy);
    return true;
}

}  // namespace

QColor aci_color(std::int16_t index) {
    if (index < 0) index = static_cast<std::int16_t>(-index);  // "off" layers carry a sign
    if (index < 10) {
        const auto& c = kAciLow[index];
        return QColor(c.r, c.g, c.b);
    }
    if (index >= 250 && index <= 255) {
        // The greyscale ramp at the top of the palette.
        const int v = 33 + (index - 250) * 36;
        return QColor(v, v, v);
    }
    if (index > 255) return QColor(255, 255, 255);

    // 10..249 is 24 hues by 10 variants: five brightness levels, each at full
    // and half saturation. This reproduces the palette's structure rather than
    // its exact table -- close enough to keep drawings legible and to keep
    // distinct indices distinguishable, which is all the display needs. The
    // exact values are only required if we ever write a colour table to file.
    const int slot = index - 10;
    const int hue = (slot / 10) * 15;  // 24 hues, 15 degrees apart
    const int variant = slot % 10;
    constexpr double kValue[5] = {1.0, 0.8, 0.63, 0.45, 0.3};
    const double value = kValue[variant / 2];
    const double saturation = (variant % 2 == 0) ? 1.0 : 0.5;
    return QColor::fromHsvF(static_cast<float>(hue) / 360.0F, static_cast<float>(saturation),
                            static_cast<float>(value));
}

QPainterRenderer::QPainterRenderer(QPainter& painter, const Viewport& viewport, const Database& db)
    : painter_(painter), viewport_(viewport), db_(db) {}

std::int16_t QPainterRenderer::effective_color(const EntityProps& props) const {
    if (props.color == kColorByLayer || props.color == kColorByBlock) {
        // BYBLOCK resolves to BYLAYER outside a block insertion, which is the
        // only context that exists until INSERT lands.
        if (props.layer < db_.layers().size()) {
            const std::int16_t c = db_.layer(props.layer).color;
            return (c < 0) ? static_cast<std::int16_t>(-c) : c;
        }
        return 7;
    }
    return props.color;
}

void QPainterRenderer::begin_entity(const EntityProps& props) {
    QPen pen(aci_color(effective_color(props)));
    // Cosmetic: a wireframe line is one pixel wide at every zoom. R12 had no
    // lineweights, and giving them width here would only misrepresent it.
    pen.setWidth(0);
    painter_.setPen(pen);
    // Wireframe only: a closed polyline is an outline, never a filled face.
    painter_.setBrush(Qt::NoBrush);
}

void QPainterRenderer::polyline(const Vec3* pts, std::size_t count, bool closed) {
    if (count < 2) return;

    const QRectF clip(0.0, 0.0, static_cast<double>(viewport_.width()),
                      static_cast<double>(viewport_.height()));

    scratch_.resize(static_cast<int>(count));
    bool all_finite = true;
    bool all_inside = true;
    for (std::size_t i = 0; i < count; ++i) {
        const ScreenPoint sp = viewport_.project(pts[i]);
        if (!std::isfinite(sp.x) || !std::isfinite(sp.y)) {
            all_finite = false;
            break;
        }
        if (std::abs(sp.x) > kCoordinateLimit || std::abs(sp.y) > kCoordinateLimit ||
            !clip.contains(sp.x, sp.y)) {
            all_inside = false;
        }
        scratch_[static_cast<int>(i)] = QPointF(sp.x, sp.y);
    }
    if (!all_finite) return;

    // The common case: everything on screen, drawn as one path so joins are
    // clean and antialiasing does not double up at the vertices.
    if (all_inside) {
        if (closed) {
            painter_.drawPolygon(scratch_);
        } else {
            painter_.drawPolyline(scratch_);
        }
        return;
    }

    // Otherwise clip segment by segment, so nothing oversized reaches Qt.
    const int n = static_cast<int>(count);
    const int last = closed ? n : n - 1;
    for (int i = 0; i < last; ++i) {
        QPointF a = scratch_[i];
        QPointF b = scratch_[(i + 1) % n];
        if (clip_segment(clip, a, b)) painter_.drawLine(a, b);
    }
}

}  // namespace noto
