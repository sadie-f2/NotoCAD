// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// TEXT: the entity, drawn with the bundled Hershey stroke font.
#include "ncad/dxf.hpp"
#include "ncad/ecs.hpp"
#include "ncad/entities.hpp"
#include "ncad/font.hpp"
#include "ncad/render.hpp"

#include <cmath>
#include <numbers>
#include <vector>

namespace ncad {

double Text::text_width() const {
    return StrokeFont::romans().width(value_) * height_ * width_factor_;
}

Vec3 Text::baseline_origin() const {
    const Basis b = arbitrary_axis(props().normal);
    const Vec3 along = (b.ax * std::cos(rotation_) + b.ay * std::sin(rotation_));
    const Vec3 up = (b.ay * std::cos(rotation_) - b.ax * std::sin(rotation_));

    // How far back from the justification point the box's own corner sits.
    // Aligned and Fit both place text between two points, so their reference is
    // the left end like Left is.
    double dx = 0.0;
    switch (h_align_) {
        case TextHAlign::Left:
        case TextHAlign::Aligned:
        case TextHAlign::Fit: dx = 0.0; break;
        case TextHAlign::Center:
        case TextHAlign::Middle: dx = -0.5 * text_width(); break;
        case TextHAlign::Right: dx = -text_width(); break;
    }

    // Baseline and Bottom differ by the descender depth, which the font knows:
    // Bottom puts the deepest descender on the insertion point, so the baseline
    // sits that far above it. Until there was a font these were fudged together.
    double dy = 0.0;
    switch (v_align_) {
        case TextVAlign::Baseline: dy = 0.0; break;
        case TextVAlign::Bottom: dy = StrokeFont::romans().descender() * height_; break;
        case TextVAlign::Middle: dy = -0.5 * height_; break;
        case TextVAlign::Top: dy = -height_; break;
    }

    // Middle is the one mode that is vertically centred by its horizontal code
    // rather than by group 73, which is why it appears in both switches.
    //
    // And it is NOT the same as MC, which is the trap: AutoCAD's Middle centres
    // on the text INCLUDING descenders, while MC centres on the uppercase
    // height. They coincide only for a string that has no descender, which is
    // why the difference survives casual testing. The font is what makes the
    // distinction expressible at all -- before it, both were height/2.
    if (h_align_ == TextHAlign::Middle && v_align_ == TextVAlign::Baseline) {
        dy = -0.5 * height_ * (1.0 - StrokeFont::romans().descender());
    }

    return position_for_drawing() + along * dx + up * dy;
}

EntityPtr Text::clone() const {
    auto copy = std::make_unique<Text>(pos_, value_, height_);
    copy->rotation_ = rotation_;
    copy->width_factor_ = width_factor_;
    copy->oblique_ = oblique_;
    copy->align_point_ = align_point_;
    copy->h_align_ = h_align_;
    copy->v_align_ = v_align_;
    copy_common_to(*copy);
    return copy;
}

void Text::transform(const Mat4& m) {
    pos_ = m.transform_point(pos_);
    align_point_ = m.transform_point(align_point_);

    // Rotation and height are carried by the plane's own axes rather than
    // stored in world terms, so they follow from the transformed basis. Text
    // scales with the drawing: SCALE on a drawing full of annotation that left
    // the annotation alone would be worse than useless.
    const Basis b = arbitrary_axis(props().normal);
    const Vec3 baseline = m.transform_vector(b.ax * std::cos(rotation_) + b.ay * std::sin(rotation_));
    const Vec3 up = m.transform_vector(b.ay);

    Vec3 n = normalize(cross(baseline, up));
    if (!is_zero(n)) props().normal = n;

    const Basis nb = arbitrary_axis(props().normal);
    if (!is_zero(baseline)) {
        rotation_ = std::atan2(dot(baseline, nb.ay), dot(baseline, nb.ax));
    }
    height_ *= length(m.transform_vector(b.ay));
}

BBox Text::bbox() const {
    // The cell the text occupies: from the deepest descender to the cap height,
    // across the exact advance width. Deliberately the cell rather than the ink,
    // so that a string of lowercase letters does not report a box half the
    // height of the same string with a capital in it -- ZOOM Extents and picking
    // both want the line, not the letterforms.
    const Basis b = arbitrary_axis(props().normal);
    const Vec3 along = (b.ax * std::cos(rotation_) + b.ay * std::sin(rotation_));
    const Vec3 up = (b.ay * std::cos(rotation_) - b.ax * std::sin(rotation_));

    const double w = text_width();
    const double below = StrokeFont::romans().descender() * height_;
    const Vec3 origin = baseline_origin();

    BBox box;
    for (const double dx : {0.0, w}) {
        for (const double dy : {-below, height_}) box.expand(origin + along * dx + up * dy);
    }
    return box;
}

void Text::osnap_points(std::vector<OsnapPoint>& out) const {
    // INSERT is what text offers in R12: the insertion point, which is the one
    // place on a piece of text that means something exact. For justified text
    // that is the justification point, not the abandoned group 10.
    out.push_back({position_for_drawing(), OsnapType::Insert});
}

void Text::grips(std::vector<Grip>& out) const {
    out.push_back(Grip{position_for_drawing(), GripKind::Stretch, 0});
}

void Text::stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        // Both points move together: they are two records of one location, and
        // moving only one would leave the text somewhere neither of them says.
        if (indices[i] == 0) {
            pos_ = pos_ + delta;
            align_point_ = align_point_ + delta;
        }
    }
}

void draw_text_line(const std::string& text, const Vec3& origin, const Vec3& along,
                    const Vec3& up, double height, double width_factor, double oblique,
                    Renderer& r) {
    if (text.empty() || height <= 0.0) return;

    const StrokeFont& font = StrokeFont::romans();

    // Oblique leans the glyph by shearing x with y, which is what a slant is --
    // the baseline does not move, so obliqued text still sits on its line.
    const double shear = std::tan(oblique);

    // Glyph coordinates arrive with the baseline at y = 0 and the cap height at
    // y = 1, so `height` scales them directly and text height means cap height,
    // exactly as R12 says.
    std::vector<Vec3> pts;
    double pen = 0.0;
    for (const char ch : text) {
        const Glyph g = font.glyph(static_cast<unsigned char>(ch));

        for (std::uint32_t s = 0; s < g.stroke_count; ++s) {
            const std::uint32_t first = g.stroke_begin[s];
            const std::uint32_t last = g.stroke_begin[s + 1];

            pts.clear();
            pts.reserve(last - first);
            for (std::uint32_t k = first; k < last; ++k) {
                const Vec3& p = g.points[k];
                const double x = (pen + p.x + p.y * shear) * width_factor * height;
                pts.push_back(origin + along * x + up * (p.y * height));
            }

            // A one-point stroke is a dot in the source data and nothing on
            // screen; emitting it as a polyline would draw a zero-length
            // segment that hit-testing then has to reason about.
            if (pts.size() >= 2) r.polyline(pts.data(), pts.size(), false);
        }

        pen += g.advance;
    }
}

void Text::draw(const DrawContext&, Renderer& r) const {
    if (value_.empty() || height_ <= 0.0) return;

    const Basis b = arbitrary_axis(props().normal);
    const Vec3 along = (b.ax * std::cos(rotation_) + b.ay * std::sin(rotation_));
    const Vec3 up = (b.ay * std::cos(rotation_) - b.ax * std::sin(rotation_));

    draw_text_line(value_, baseline_origin(), along, up, height_, width_factor_, oblique_, r);
}

void Text::dxf_write(DxfWriter& w) const {
    const Mat4 to_ecs = world_to_ecs(props().normal);

    w.write_common(*this);
    w.point(10, to_ecs.transform_point(pos_));
    w.code(40, height_);
    w.code(1, value_);
    if (rotation_ != 0.0) w.code(50, rotation_ * 180.0 / std::numbers::pi);
    if (width_factor_ != 1.0) w.code(41, width_factor_);
    if (oblique_ != 0.0) w.code(51, oblique_ * 180.0 / std::numbers::pi);
    // Groups 72 and 73 take the enumerator values directly; see entities.hpp
    // for why they are numbered the way they are. Both are omitted when they
    // are the default, which keeps the common case byte-identical to before.
    if (h_align_ != TextHAlign::Left) w.code(72, static_cast<std::int16_t>(h_align_));

    // TEXT declares AcDbText TWICE, and the vertical justification belongs
    // after the second one. That is genuinely how the format is shaped -- group
    // 73 sits in a later part of the class than groups 72 and 11 -- and a
    // reader that meets 73 before the marker rejects the record.
    //
    // R12 has no markers at all and its readers ask for codes rather than
    // walking them in order, so its layout is left exactly as it was: the
    // output is confirmed good in AutoCAD and is not worth disturbing to share
    // a code path.
    if (dxf_requires_handles(w.version())) {
        if (is_justified()) w.point(11, to_ecs.transform_point(align_point_));
        w.write_extrusion(props().normal);
        if (v_align_ != TextVAlign::Baseline) {
            w.subclass("AcDbText");
            w.code(73, static_cast<std::int16_t>(v_align_));
        }
        return;
    }

    if (v_align_ != TextVAlign::Baseline) w.code(73, static_cast<std::int16_t>(v_align_));
    if (is_justified()) w.point(11, to_ecs.transform_point(align_point_));
    w.write_extrusion(props().normal);
}

}  // namespace ncad
