// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "command_icons.hpp"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QRectF>

#include <cmath>

namespace ncad {
namespace {

// Every icon is drawn on a 0..1 square and scaled at the end, so the drawings
// below read as geometry rather than as pixel arithmetic and one number here
// changes the size of the whole set.
constexpr int kIconPixels = 22;

// Inset from the edges. Toolbar buttons put their own padding around this, and
// an icon drawn hard to its bounds looks bigger than its neighbours rather than
// bolder.
constexpr double kPad = 0.08;

double lerp(double a, double b, double t) { return a + (b - a) * t; }

// A painter whose unit square is the icon, so the drawings can speak in
// fractions and never in pixels.
struct Ink {
    QPainter& p;
    double scale;

    QPointF at(double x, double y) const {
        // Y up, as every other coordinate in this program: an icon of a CAD
        // command should not be the one place where the axis flips.
        return QPointF(lerp(kPad, 1.0 - kPad, x) * scale, lerp(1.0 - kPad, kPad, y) * scale);
    }
    void line(double x0, double y0, double x1, double y1) const {
        p.drawLine(at(x0, y0), at(x1, y1));
    }
    void poly(std::initializer_list<QPointF> pts) const {
        QPolygonF shape;
        for (const QPointF& q : pts) shape << q;
        p.drawPolyline(shape);
    }
    QRectF box(double x0, double y0, double x1, double y1) const {
        return QRectF(at(x0, y1), at(x1, y0));
    }
    void rect(double x0, double y0, double x1, double y1) const { p.drawRect(box(x0, y0, x1, y1)); }
    void ellipse(double cx, double cy, double rx, double ry) const {
        p.drawEllipse(box(cx - rx, cy - ry, cx + rx, cy + ry));
    }
    // Angles in degrees, counterclockwise from +X, as the rest of the program
    // measures them -- Qt wants sixteenths, which is a Qt fact and stays here.
    void arc(double cx, double cy, double rx, double ry, double from, double span) const {
        p.drawArc(box(cx - rx, cy - ry, cx + rx, cy + ry), static_cast<int>(from * 16),
                  static_cast<int>(span * 16));
    }
    void dot(double x, double y, double r = 0.055) const {
        const QBrush saved = p.brush();
        p.setBrush(p.pen().color());
        p.drawEllipse(box(x - r, y - r, x + r, y + r));
        p.setBrush(saved);
    }
    // A small solid arrowhead at (x,y) pointing along (dx,dy).
    void arrow(double x, double y, double dx, double dy, double size = 0.14) const {
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-9) return;
        const double ux = dx / len;
        const double uy = dy / len;
        const double px = -uy;
        const double py = ux;
        QPolygonF head;
        head << at(x, y) << at(x - ux * size + px * size * 0.5, y - uy * size + py * size * 0.5)
             << at(x - ux * size - px * size * 0.5, y - uy * size - py * size * 0.5);
        const QBrush saved = p.brush();
        p.setBrush(p.pen().color());
        p.drawPolygon(head);
        p.setBrush(saved);
    }
};

void dashed(QPainter& p, bool on) {
    QPen pen = p.pen();
    pen.setStyle(on ? Qt::DotLine : Qt::SolidLine);
    p.setPen(pen);
}

// --- the drawings -----------------------------------------------------------
//
// One function per command. Each is the smallest picture that says which
// command it is, which usually means drawing the RESULT rather than the verb:
// OFFSET is two parallel corners, FILLET is a rounded one, CHAMFER a cut one.

void draw_new(const Ink& k) {
    k.poly({k.at(0.25, 0.05), k.at(0.25, 0.95), k.at(0.6, 0.95), k.at(0.78, 0.75),
            k.at(0.78, 0.05), k.at(0.25, 0.05)});
    k.poly({k.at(0.6, 0.95), k.at(0.6, 0.75), k.at(0.78, 0.75)});
}

void draw_open(const Ink& k) {
    // A folder, opening.
    k.poly({k.at(0.06, 0.2), k.at(0.06, 0.8), k.at(0.4, 0.8), k.at(0.48, 0.68), k.at(0.86, 0.68)});
    k.poly({k.at(0.86, 0.68), k.at(0.86, 0.5)});
    k.poly({k.at(0.06, 0.2), k.at(0.72, 0.2), k.at(0.94, 0.56), k.at(0.86, 0.56)});
}

void draw_save(const Ink& k) {
    // The floppy that outlived floppies.
    k.poly({k.at(0.1, 0.1), k.at(0.1, 0.9), k.at(0.78, 0.9), k.at(0.9, 0.78), k.at(0.9, 0.1),
            k.at(0.1, 0.1)});
    k.rect(0.28, 0.55, 0.72, 0.9);   // the shutter
    k.rect(0.24, 0.1, 0.76, 0.42);   // the label
}

void draw_line(const Ink& k) {
    k.line(0.12, 0.18, 0.88, 0.82);
    k.dot(0.12, 0.18);
    k.dot(0.88, 0.82);
}

void draw_pline(const Ink& k) {
    k.poly({k.at(0.08, 0.3), k.at(0.35, 0.75), k.at(0.62, 0.25), k.at(0.92, 0.7)});
    k.dot(0.08, 0.3, 0.05);
    k.dot(0.92, 0.7, 0.05);
}

void draw_circle(const Ink& k) {
    k.ellipse(0.5, 0.5, 0.42, 0.42);
    k.dot(0.5, 0.5, 0.045);
}

void draw_arc(const Ink& k) {
    k.arc(0.5, 0.22, 0.42, 0.42, 20.0, 140.0);
    k.dot(0.11, 0.36, 0.05);
    k.dot(0.89, 0.36, 0.05);
}

void draw_ellipse(const Ink& k) {
    k.ellipse(0.5, 0.5, 0.45, 0.28);
    // The axes rather than a centre dot: an oval with a pupil in it reads as
    // an eye at this size, which is a different icon in every other program.
    k.line(0.05, 0.5, 0.95, 0.5);
    k.line(0.5, 0.22, 0.5, 0.78);
}

void draw_spline(const Ink& k) {
    QPainterPath path(k.at(0.08, 0.28));
    path.cubicTo(k.at(0.3, 1.0), k.at(0.7, 0.0), k.at(0.92, 0.72));
    k.p.drawPath(path);
    k.dot(0.08, 0.28, 0.05);
    k.dot(0.92, 0.72, 0.05);
}

void draw_point(const Ink& k) {
    k.line(0.5, 0.16, 0.5, 0.84);
    k.line(0.16, 0.5, 0.84, 0.5);
    k.dot(0.5, 0.5, 0.11);
}

void draw_text(const Ink& k) {
    k.poly({k.at(0.18, 0.08), k.at(0.5, 0.9), k.at(0.82, 0.08)});
    k.line(0.31, 0.42, 0.69, 0.42);
}

void draw_solid(const Ink& k) {
    QPolygonF shape;
    shape << k.at(0.1, 0.15) << k.at(0.62, 0.2) << k.at(0.9, 0.82) << k.at(0.3, 0.75);
    const QBrush saved = k.p.brush();
    k.p.setBrush(k.p.pen().color());
    k.p.drawPolygon(shape);
    k.p.setBrush(saved);
}

void draw_erase(const Ink& k) {
    k.line(0.06, 0.28, 0.6, 0.28);
    k.line(0.45, 0.55, 0.94, 0.94);
    k.line(0.94, 0.55, 0.45, 0.94);
}

void draw_move(const Ink& k) {
    k.rect(0.1, 0.1, 0.5, 0.5);
    dashed(k.p, true);
    k.rect(0.45, 0.45, 0.85, 0.85);
    dashed(k.p, false);
    k.line(0.3, 0.3, 0.6, 0.6);
    k.arrow(0.66, 0.66, 1.0, 1.0);
}

void draw_copy(const Ink& k) {
    k.rect(0.08, 0.08, 0.55, 0.55);
    k.rect(0.42, 0.42, 0.92, 0.92);
}

void draw_rotate(const Ink& k) {
    k.arc(0.5, 0.5, 0.38, 0.38, 200.0, 250.0);
    k.arrow(0.5, 0.88, -0.9, 0.4);
    k.dot(0.5, 0.5, 0.05);
}

void draw_scale(const Ink& k) {
    k.rect(0.08, 0.08, 0.42, 0.42);
    k.rect(0.08, 0.08, 0.9, 0.9);
    k.line(0.42, 0.42, 0.82, 0.82);
    k.arrow(0.88, 0.88, 1.0, 1.0);
}

void draw_mirror(const Ink& k) {
    k.poly({k.at(0.06, 0.15), k.at(0.36, 0.15), k.at(0.06, 0.85), k.at(0.06, 0.15)});
    k.poly({k.at(0.94, 0.15), k.at(0.64, 0.15), k.at(0.94, 0.85), k.at(0.94, 0.15)});
    dashed(k.p, true);
    k.line(0.5, 0.02, 0.5, 0.98);
    dashed(k.p, false);
}

void draw_array(const Ink& k) {
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const double x = 0.06 + col * 0.34;
            const double y = 0.06 + row * 0.34;
            k.rect(x, y, x + 0.2, y + 0.2);
        }
    }
}

void draw_stretch(const Ink& k) {
    k.poly({k.at(0.08, 0.2), k.at(0.08, 0.7), k.at(0.55, 0.7), k.at(0.55, 0.2), k.at(0.08, 0.2)});
    dashed(k.p, true);
    k.poly({k.at(0.55, 0.7), k.at(0.9, 0.88)});
    dashed(k.p, false);
    k.arrow(0.94, 0.9, 1.0, 0.5);
    k.dot(0.55, 0.7, 0.055);
}

void draw_trim(const Ink& k) {
    k.line(0.5, 0.04, 0.5, 0.96);  // the cutting edge
    k.line(0.04, 0.35, 0.5, 0.35);
    dashed(k.p, true);
    k.line(0.5, 0.35, 0.96, 0.35);  // what goes away
    dashed(k.p, false);
    k.line(0.62, 0.62, 0.84, 0.84);
    k.line(0.84, 0.62, 0.62, 0.84);
}

void draw_extend(const Ink& k) {
    k.line(0.9, 0.04, 0.9, 0.96);  // the boundary
    k.line(0.06, 0.4, 0.45, 0.4);
    dashed(k.p, true);
    k.line(0.45, 0.4, 0.82, 0.4);
    dashed(k.p, false);
    k.arrow(0.88, 0.4, 1.0, 0.0);
}

void draw_break(const Ink& k) {
    k.line(0.06, 0.5, 0.38, 0.5);
    k.line(0.62, 0.5, 0.94, 0.5);
    k.dot(0.38, 0.5, 0.05);
    k.dot(0.62, 0.5, 0.05);
}

void draw_offset(const Ink& k) {
    k.poly({k.at(0.1, 0.9), k.at(0.1, 0.18), k.at(0.85, 0.18)});
    k.poly({k.at(0.34, 0.9), k.at(0.34, 0.42), k.at(0.85, 0.42)});
}

void draw_fillet(const Ink& k) {
    // The corner it makes: two lines and the arc between them.
    k.line(0.1, 0.9, 0.1, 0.45);
    k.arc(0.45, 0.45, 0.35, 0.35, 90.0, 90.0);
    k.line(0.45, 0.1, 0.9, 0.1);
    dashed(k.p, true);
    k.poly({k.at(0.1, 0.45), k.at(0.1, 0.1), k.at(0.45, 0.1)});
    dashed(k.p, false);
}

void draw_chamfer(const Ink& k) {
    k.line(0.1, 0.9, 0.1, 0.42);
    k.line(0.1, 0.42, 0.42, 0.1);
    k.line(0.42, 0.1, 0.9, 0.1);
    dashed(k.p, true);
    k.poly({k.at(0.1, 0.42), k.at(0.1, 0.1), k.at(0.42, 0.1)});
    dashed(k.p, false);
}

void draw_explode(const Ink& k) {
    k.rect(0.36, 0.36, 0.64, 0.64);
    k.arrow(0.14, 0.86, -1.0, 1.0, 0.16);
    k.arrow(0.86, 0.86, 1.0, 1.0, 0.16);
    k.arrow(0.14, 0.14, -1.0, -1.0, 0.16);
    k.arrow(0.86, 0.14, 1.0, -1.0, 0.16);
}

void draw_zoom(const Ink& k) {
    k.ellipse(0.42, 0.58, 0.34, 0.34);
    k.line(0.66, 0.34, 0.94, 0.06);
    k.line(0.28, 0.58, 0.56, 0.58);
    k.line(0.42, 0.44, 0.42, 0.72);
}

void draw_pan(const Ink& k) {
    k.line(0.5, 0.1, 0.5, 0.9);
    k.line(0.1, 0.5, 0.9, 0.5);
    k.arrow(0.5, 0.96, 0.0, 1.0, 0.16);
    k.arrow(0.5, 0.04, 0.0, -1.0, 0.16);
    k.arrow(0.96, 0.5, 1.0, 0.0, 0.16);
    k.arrow(0.04, 0.5, -1.0, 0.0, 0.16);
}

void draw_plan(const Ink& k) {
    k.poly({k.at(0.08, 0.92), k.at(0.08, 0.08), k.at(0.92, 0.08)});
    k.arrow(0.08, 0.98, 0.0, 1.0, 0.15);
    k.arrow(0.98, 0.08, 1.0, 0.0, 0.15);
    k.rect(0.3, 0.3, 0.66, 0.66);
}

void draw_undo(const Ink& k) {
    k.arc(0.5, 0.4, 0.36, 0.3, 20.0, 140.0);
    k.arrow(0.16, 0.5, -0.5, -1.0, 0.16);
}

void draw_redo(const Ink& k) {
    k.arc(0.5, 0.4, 0.36, 0.3, 20.0, 140.0);
    k.arrow(0.84, 0.5, 0.5, -1.0, 0.16);
}

void draw_ucsicon(const Ink& k) {
    k.poly({k.at(0.1, 0.9), k.at(0.1, 0.1), k.at(0.9, 0.1)});
    k.arrow(0.1, 0.96, 0.0, 1.0, 0.15);
    k.arrow(0.96, 0.1, 1.0, 0.0, 0.15);
    k.rect(0.1, 0.1, 0.26, 0.26);
}

void draw_layer(const Ink& k) {
    k.poly({k.at(0.5, 0.9), k.at(0.92, 0.68), k.at(0.5, 0.46), k.at(0.08, 0.68), k.at(0.5, 0.9)});
    k.poly({k.at(0.08, 0.46), k.at(0.5, 0.24), k.at(0.92, 0.46)});
    k.poly({k.at(0.08, 0.28), k.at(0.5, 0.06), k.at(0.92, 0.28)});
}

void draw_block(const Ink& k) {
    k.poly({k.at(0.5, 0.94), k.at(0.92, 0.7), k.at(0.92, 0.3), k.at(0.5, 0.06), k.at(0.08, 0.3),
            k.at(0.08, 0.7), k.at(0.5, 0.94)});
    k.poly({k.at(0.08, 0.7), k.at(0.5, 0.5), k.at(0.92, 0.7)});
    k.line(0.5, 0.5, 0.5, 0.06);
}

void draw_measure(const Ink& k) {
    k.line(0.08, 0.7, 0.92, 0.7);
    k.line(0.08, 0.82, 0.08, 0.58);
    k.line(0.92, 0.82, 0.92, 0.58);
    k.line(0.2, 0.7, 0.2, 0.62);
    k.line(0.5, 0.7, 0.5, 0.6);
    k.line(0.8, 0.7, 0.8, 0.62);
    k.line(0.08, 0.28, 0.92, 0.28);
}

using Drawing = void (*)(const Ink&);

struct Entry {
    const char* command;
    Drawing draw;
};

constexpr Entry kIcons[] = {
    {"NEW", draw_new},         {"OPEN", draw_open},       {"SAVE", draw_save},
    {"QSAVE", draw_save},      {"SAVEAS", draw_save},     {"LINE", draw_line},
    {"PLINE", draw_pline},     {"CIRCLE", draw_circle},   {"ARC", draw_arc},
    {"ELLIPSE", draw_ellipse}, {"SPLINE", draw_spline},   {"POINT", draw_point},
    {"TEXT", draw_text},       {"SOLID", draw_solid},     {"ERASE", draw_erase},
    {"MOVE", draw_move},       {"COPY", draw_copy},       {"ROTATE", draw_rotate},
    {"SCALE", draw_scale},     {"MIRROR", draw_mirror},   {"ARRAY", draw_array},
    {"STRETCH", draw_stretch}, {"TRIM", draw_trim},       {"EXTEND", draw_extend},
    {"BREAK", draw_break},     {"OFFSET", draw_offset},   {"FILLET", draw_fillet},
    {"CHAMFER", draw_chamfer}, {"EXPLODE", draw_explode}, {"ZOOM", draw_zoom},
    {"PAN", draw_pan},         {"PLAN", draw_plan},       {"UNDO", draw_undo},
    {"REDO", draw_redo},       {"UCSICON", draw_ucsicon}, {"LAYER", draw_layer},
    {"BLOCK", draw_block},     {"INSERT", draw_block},    {"MEASUREGEOM", draw_measure},
};

Drawing find(const QString& command) {
    for (const Entry& e : kIcons) {
        if (command.compare(QLatin1String(e.command), Qt::CaseInsensitive) == 0) return e.draw;
    }
    return nullptr;
}

}  // namespace

bool has_command_icon(const QString& command) { return find(command) != nullptr; }

QIcon command_icon(const QString& command, const QColor& ink) {
    // Drawn at the device's own resolution rather than at 22 and scaled up,
    // which is the whole reason these are strokes and not bitmaps.
    const qreal dpr = 2.0;
    QPixmap pm(QSize(kIconPixels, kIconPixels) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(ink);
    // In device-independent units, so the weight is the same on every panel.
    pen.setWidthF(1.35);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const Ink k{p, static_cast<double>(kIconPixels)};
    if (const Drawing d = find(command)) {
        d(k);
    } else {
        // The lettered fallback. Deliberately plain: it should read as "no
        // icon yet" rather than as a design.
        QFont f = p.font();
        f.setPixelSize(static_cast<int>(kIconPixels * 0.52));
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRectF(0, 0, kIconPixels, kIconPixels), Qt::AlignCenter, command.left(2));
    }
    p.end();

    return QIcon(pm);
}

}  // namespace ncad
