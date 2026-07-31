// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// MTEXT: the paragraph entity, and the layout that turns it into lines.
//
// The raw string is what the entity holds; everything visible is derived from
// it on demand. That is the whole design decision -- stripping the inline codes
// at read time is cheaper and destroys the entity, so a file opened and saved
// would lose its formatting for good.
#include "noto/dxf.hpp"
#include "noto/ecs.hpp"
#include "noto/entities.hpp"
#include "noto/font.hpp"
#include "noto/render.hpp"

#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace noto {
namespace {

// AutoCAD's default line spacing is "3-on-5": five units of leading for three
// of text, so successive baselines sit 5/3 of the cap height apart. Group 44
// scales that rather than replacing it.
constexpr double kDefaultLineSpacing = 5.0 / 3.0;

// Strips MTEXT's inline formatting and splits on explicit paragraph breaks.
//
// Everything here is DISCARDED rather than honoured, and that is a deliberate
// limit: this program has one stroke font at one height, so a font switch or a
// height change has nothing to switch to. The codes are consumed correctly so
// that their arguments do not leak into the visible text, which is the failure
// that would actually look broken.
void split_and_strip(const std::string& in, std::vector<std::string>& out) {
    std::string line;

    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];

        // Grouping braces scope a format change and carry no text of their own.
        if (c == '{' || c == '}') continue;

        if (c != '\\') {
            line.push_back(c);
            continue;
        }
        if (i + 1 >= in.size()) break;

        const char code = in[++i];
        switch (code) {
            // A paragraph break is the one code that survives, as a line.
            case 'P':
                out.push_back(line);
                line.clear();
                break;

            // An escaped literal: the character itself, not a code.
            case '\\':
            case '{':
            case '}': line.push_back(code); break;

            // A non-breaking space is still a space once nothing can rewrap it
            // differently from the way this already did.
            case '~': line.push_back(' '); break;

            // Toggles. Two characters, no argument, nothing to consume.
            case 'L':
            case 'l':
            case 'O':
            case 'o':
            case 'K':
            case 'k': break;

            // A stacked fraction: \S upper ^ lower ;. Rendered flat as "a/b",
            // which is readable and honest -- there is no way to stack strokes
            // at one height, and dropping it would silently delete a dimension.
            case 'S': {
                std::string upper;
                std::string lower;
                bool in_lower = false;
                while (++i < in.size() && in[i] != ';') {
                    const char s = in[i];
                    if (s == '^' || s == '#') {
                        in_lower = true;
                        continue;
                    }
                    (in_lower ? lower : upper).push_back(s);
                }
                line += upper;
                if (!lower.empty()) {
                    line.push_back('/');
                    line += lower;
                }
                break;
            }

            // Everything else takes an argument terminated by a semicolon:
            // \H2.5x; \fArial|b0; \C1; \W0.8; \Q15; \A1; \T1; \pxi-2;
            default:
                while (i + 1 < in.size() && in[i + 1] != ';') ++i;
                if (i + 1 < in.size()) ++i;  // the semicolon itself
                break;
        }
    }

    out.push_back(line);
}

// Breaks one logical line to fit `limit` world units, measuring with the same
// font that will draw it.
void wrap(const std::string& line, double limit, double height, std::vector<std::string>& out) {
    const StrokeFont& font = StrokeFont::romans();
    if (limit <= 0.0 || line.empty()) {
        out.push_back(line);
        return;
    }

    std::string current;
    std::size_t i = 0;
    while (i < line.size()) {
        // One word, plus the run of spaces in front of it, so that breaking
        // between them does not carry the spaces to the start of the next line.
        std::size_t start = i;
        while (i < line.size() && line[i] == ' ') ++i;
        while (i < line.size() && line[i] != ' ') ++i;
        const std::string word = line.substr(start, i - start);

        const std::string candidate = current + word;
        if (!current.empty() && font.width(candidate) * height > limit) {
            out.push_back(current);
            // The leading spaces belong to the break, not to the new line.
            std::size_t skip = 0;
            while (skip < word.size() && word[skip] == ' ') ++skip;
            current = word.substr(skip);
        } else {
            current = candidate;
        }
    }
    out.push_back(current);
}

}  // namespace

double MText::line_height() const { return height_ * kDefaultLineSpacing * line_spacing_; }

void MText::layout(std::vector<std::string>& out) const {
    out.clear();

    std::vector<std::string> paragraphs;
    split_and_strip(text_, paragraphs);

    for (const std::string& p : paragraphs) wrap(p, width_, height_, out);
}

double MText::laid_out_width() const {
    std::vector<std::string> lines;
    layout(lines);

    const StrokeFont& font = StrokeFont::romans();
    double widest = 0.0;
    for (const std::string& l : lines) widest = std::max(widest, font.width(l) * height_);
    return widest;
}

Vec3 MText::baseline_origin() const {
    const Basis b = arbitrary_axis(props().normal);
    const Vec3 along = (b.ax * std::cos(rotation_) + b.ay * std::sin(rotation_));
    const Vec3 up = (b.ay * std::cos(rotation_) - b.ax * std::sin(rotation_));

    std::vector<std::string> lines;
    layout(lines);
    const std::size_t count = lines.empty() ? 1 : lines.size();

    // The block's own extent. Its width is the widest line unless a reference
    // width was given, in which case the attachment is measured against the box
    // the author asked for rather than against what happened to fit in it.
    const double block_w = (width_ > 0.0) ? width_ : laid_out_width();
    const double block_h = static_cast<double>(count - 1) * line_height() + height_;

    const int a = static_cast<int>(attach_);
    const int column = (a - 1) % 3;  // 0 left, 1 centre, 2 right
    const int row = (a - 1) / 3;     // 0 top, 1 middle, 2 bottom

    double dx = 0.0;
    if (column == 1) dx = -0.5 * block_w;
    if (column == 2) dx = -block_w;

    // Down from the top of the first line's cap height to its baseline.
    double dy = -height_;
    if (row == 1) dy += 0.5 * block_h;
    if (row == 2) dy += block_h;

    return pos_ + along * dx + up * dy;
}

EntityPtr MText::clone() const {
    auto copy = std::make_unique<MText>(pos_, text_, height_, width_);
    copy->rotation_ = rotation_;
    copy->line_spacing_ = line_spacing_;
    copy->attach_ = attach_;
    copy_common_to(*copy);
    return copy;
}

void MText::transform(const Mat4& m) {
    pos_ = m.transform_point(pos_);

    // Same reasoning as Text::transform: rotation and height ride on the
    // plane's axes rather than being stored in world terms, so they follow from
    // the transformed basis. The reference width scales with the text, or a
    // scaled paragraph would rewrap and change shape.
    const Basis b = arbitrary_axis(props().normal);
    const Vec3 baseline =
        m.transform_vector(b.ax * std::cos(rotation_) + b.ay * std::sin(rotation_));
    const Vec3 up = m.transform_vector(b.ay);

    const Vec3 n = normalize(cross(baseline, up));
    if (!is_zero(n)) props().normal = n;

    const Basis nb = arbitrary_axis(props().normal);
    if (!is_zero(baseline)) {
        rotation_ = std::atan2(dot(baseline, nb.ay), dot(baseline, nb.ax));
    }

    const double scale = length(m.transform_vector(b.ay));
    height_ *= scale;
    width_ *= scale;
}

BBox MText::bbox() const {
    const Basis b = arbitrary_axis(props().normal);
    const Vec3 along = (b.ax * std::cos(rotation_) + b.ay * std::sin(rotation_));
    const Vec3 up = (b.ay * std::cos(rotation_) - b.ax * std::sin(rotation_));

    std::vector<std::string> lines;
    layout(lines);
    const std::size_t count = lines.empty() ? 1 : lines.size();

    const double w = (width_ > 0.0) ? width_ : laid_out_width();
    const double below = StrokeFont::romans().descender() * height_;
    const double drop = static_cast<double>(count - 1) * line_height();
    const Vec3 origin = baseline_origin();

    BBox box;
    for (const double dx : {0.0, w}) {
        for (const double dy : {height_, -drop - below}) box.expand(origin + along * dx + up * dy);
    }
    return box;
}

void MText::osnap_points(std::vector<OsnapPoint>& out) const {
    // The insertion point, which is the one place on a paragraph that means
    // something exact -- and unlike TEXT's, it is the attachment point, so it
    // is where the author positioned the block rather than where line one
    // happens to start.
    out.push_back({pos_, OsnapType::Insert});
}

void MText::grips(std::vector<Grip>& out) const {
    out.push_back(Grip{pos_, GripKind::Stretch, 0});
}

void MText::stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (indices[i] == 0) pos_ = pos_ + delta;
    }
}

void MText::draw(const DrawContext&, Renderer& r) const {
    if (height_ <= 0.0) return;

    const Basis b = arbitrary_axis(props().normal);
    const Vec3 along = (b.ax * std::cos(rotation_) + b.ay * std::sin(rotation_));
    const Vec3 up = (b.ay * std::cos(rotation_) - b.ax * std::sin(rotation_));

    std::vector<std::string> lines;
    layout(lines);

    const StrokeFont& font = StrokeFont::romans();
    const double block_w = (width_ > 0.0) ? width_ : laid_out_width();
    const int column = (static_cast<int>(attach_) - 1) % 3;

    Vec3 origin = baseline_origin();
    for (const std::string& line : lines) {
        // Lines are justified within the block, not merely stacked: a centred
        // attachment centres every line, which is what the attachment means.
        double indent = 0.0;
        if (column != 0) {
            const double w = font.width(line) * height_;
            indent = (column == 1) ? (block_w - w) * 0.5 : (block_w - w);
        }
        draw_text_line(line, origin + along * indent, along, up, height_, 1.0, 0.0, r);
        origin = origin - up * line_height();
    }
}

void MText::dxf_write(DxfWriter& w) const {
    if (height_ <= 0.0) return;

    // R2000 has MTEXT, so the paragraph goes out whole -- the raw string with
    // its inline codes, the reference width, the attachment. The R12 path below
    // flattens it to a run of TEXT records and cannot be rejoined.
    if (dxf_has_modern_entities(w.version())) {
        const Mat4 to_ecs = world_to_ecs(props().normal);
        w.write_common(*this);
        w.subclass("AcDbMText");
        w.point(10, to_ecs.transform_point(pos_));
        w.write_extrusion(props().normal);
        w.code(40, height_);
        w.code(41, width_);
        w.code(71, static_cast<int>(attach_));
        w.code(72, 1);  // drawing direction: by style
        if (line_spacing_ != 1.0) {
            w.code(73, 2);  // exact, scaled by the factor below
            w.code(44, line_spacing_);
        }
        if (rotation_ != 0.0) w.code(50, rotation_ * 180.0 / std::numbers::pi);

        // Text over 250 characters is split across group 3 chunks with the tail
        // in group 1 -- the same rule the reader honours, from the other side.
        constexpr std::size_t kChunk = 250;
        std::size_t at = 0;
        while (text_.size() - at > kChunk) {
            w.code(3, text_.substr(at, kChunk));
            at += kChunk;
        }
        w.code(1, text_.substr(at));
        return;
    }

    // THE DIVERGENCE, PAID FOR HERE. AC1009 has no MTEXT, so this writes what
    // R12 itself would have held: one TEXT record per laid-out line, formatting
    // discarded and the wrap already applied. Every reader understands it.
    //
    // Consequence, stated rather than discovered: a round trip through DXF is
    // LOSSY. The paragraph comes back as separate lines and nothing can rejoin
    // them. Same bargain as ELLIPSE and SPLINE, and the reason DXF READ takes
    // MTEXT natively -- what this program writes it cannot read back as one
    // entity, but what AutoCAD writes it can.
    if (height_ <= 0.0) return;

    const Mat4 to_ecs = world_to_ecs(props().normal);
    const Basis b = arbitrary_axis(props().normal);
    const Vec3 along = (b.ax * std::cos(rotation_) + b.ay * std::sin(rotation_));
    const Vec3 up = (b.ay * std::cos(rotation_) - b.ax * std::sin(rotation_));

    std::vector<std::string> lines;
    layout(lines);

    const StrokeFont& font = StrokeFont::romans();
    const double block_w = (width_ > 0.0) ? width_ : laid_out_width();
    const int column = (static_cast<int>(attach_) - 1) % 3;

    Vec3 origin = baseline_origin();
    bool first = true;
    for (const std::string& line : lines) {
        double indent = 0.0;
        if (column != 0) {
            const double width = font.width(line) * height_;
            indent = (column == 1) ? (block_w - width) * 0.5 : (block_w - width);
        }

        // The first record carries this entity's own handle and properties; the
        // rest repeat them, as POLYLINE's VERTEX records do.
        if (first) {
            w.write_common_as(*this, "TEXT");
            first = false;
        } else {
            w.code(0, "TEXT");
                w.code(8, w.layer_name(*this));
        }

        w.point(10, to_ecs.transform_point(origin + along * indent));
        w.code(40, height_);
        w.code(1, line);
        if (rotation_ != 0.0) w.code(50, rotation_ * 180.0 / std::numbers::pi);
        w.write_extrusion(props().normal);

        origin = origin - up * line_height();
    }
}

}  // namespace noto
