// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// TEXT: the entity, with a placeholder where the glyphs will go.
#include "noto/dxf.hpp"
#include "noto/ecs.hpp"
#include "noto/entities.hpp"
#include "noto/render.hpp"

#include <cmath>
#include <numbers>

namespace noto {
namespace {

// Character cell width as a fraction of height, for the placeholder box. R12's
// stroke fonts run around this; it is a guess and is used for nothing that has
// to be right.
constexpr double kNominalAspect = 0.6;

}  // namespace

double Text::approximate_width() const {
    return static_cast<double>(value_.size()) * height_ * kNominalAspect * width_factor_;
}

Vec3 Text::box_origin() const {
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
        case TextHAlign::Middle: dx = -0.5 * approximate_width(); break;
        case TextHAlign::Right: dx = -approximate_width(); break;
    }

    // Baseline and Bottom differ only by the descender depth, which needs a
    // font to know. They are treated alike until there is one, which is a
    // visible approximation rather than a silent one -- see CLAUDE.md.
    double dy = 0.0;
    switch (v_align_) {
        case TextVAlign::Baseline:
        case TextVAlign::Bottom: dy = 0.0; break;
        case TextVAlign::Middle: dy = -0.5 * height_; break;
        case TextVAlign::Top: dy = -height_; break;
    }

    // Middle is the one mode that is vertically centred by its horizontal code
    // rather than by group 73, which is why it appears in both switches.
    if (h_align_ == TextHAlign::Middle && v_align_ == TextVAlign::Baseline) {
        dy = -0.5 * height_;
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
    // The placeholder's extent. Wrong in detail until there is a font, but the
    // right order of magnitude -- and a box that is roughly right keeps text
    // pickable and inside ZOOM Extents, where an empty box would not.
    const Basis b = arbitrary_axis(props().normal);
    const Vec3 along = (b.ax * std::cos(rotation_) + b.ay * std::sin(rotation_));
    const Vec3 up = (b.ay * std::cos(rotation_) - b.ax * std::sin(rotation_));

    const Vec3 origin = box_origin();
    BBox box;
    box.expand(origin);
    box.expand(origin + along * approximate_width());
    box.expand(origin + up * height_);
    box.expand(origin + along * approximate_width() + up * height_);
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

void Text::draw(const DrawContext&, Renderer& r) const {
    if (value_.empty() || height_ <= 0.0) return;

    const Basis b = arbitrary_axis(props().normal);
    const Vec3 along = (b.ax * std::cos(rotation_) + b.ay * std::sin(rotation_));
    const Vec3 up = (b.ay * std::cos(rotation_) - b.ax * std::sin(rotation_));

    const double w = approximate_width();
    const Vec3 o = box_origin();
    const Vec3 box[4] = {o, o + along * w, o + along * w + up * height_, o + up * height_};
    r.polyline(box, 4, true);
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
    if (v_align_ != TextVAlign::Baseline) w.code(73, static_cast<std::int16_t>(v_align_));
    if (is_justified()) w.point(11, to_ecs.transform_point(align_point_));
    w.write_extrusion(props().normal);
}

}  // namespace noto
