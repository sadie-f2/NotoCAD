// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The first three concrete entities: LINE, CIRCLE, ARC.
//
// Storage convention: geometry is held in WORLD coordinates, with `props().normal`
// carrying the extrusion direction. Conversion into the entity coordinate system
// happens only at DXF write time.
//
// This is the opposite of how R12 stores things on disk, and it is deliberate --
// transforms, bounding boxes and osnaps all want world space, and doing the ECS
// conversion once at serialisation keeps the arbitrary axis algorithm confined to
// the DXF layer instead of smeared through the kernel.
#pragma once

#include "noto/entity.hpp"

#include <string>
#include <vector>

namespace noto {

class Line final : public Entity {
public:
    Line() : Entity(EntityType::Line) {}
    Line(const Vec3& start, const Vec3& end) : Entity(EntityType::Line), start_(start), end_(end) {}

    const Vec3& start() const { return start_; }
    const Vec3& end() const { return end_; }
    void set_start(const Vec3& p) { start_ = p; }
    void set_end(const Vec3& p) { end_ = p; }

    Vec3 midpoint() const { return (start_ + end_) * 0.5; }
    Vec3 direction() const { return end_ - start_; }
    double length() const { return noto::length(direction()); }

    EntityPtr clone() const override;
    void transform(const Mat4& m) override;
    BBox bbox() const override;
    void osnap_points(std::vector<OsnapPoint>& out) const override;
    void grips(std::vector<Grip>& out) const override;
    void stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) override;
    void dxf_write(DxfWriter& w) const override;
    void draw(const DrawContext& ctx, Renderer& r) const override;

private:
    Vec3 start_{};
    Vec3 end_{};
};

class Circle final : public Entity {
public:
    Circle() : Entity(EntityType::Circle) {}
    Circle(const Vec3& center, double radius, const Vec3& normal = kWorldZ)
        : Entity(EntityType::Circle), center_(center), radius_(radius) {
        props().normal = normalize(normal);
    }

    const Vec3& center() const { return center_; }
    double radius() const { return radius_; }
    void set_center(const Vec3& c) { center_ = c; }
    void set_radius(double r) { radius_ = r; }

    // The four quadrant points, in the entity's own plane.
    void quadrants(Vec3 out[4]) const;

    EntityPtr clone() const override;
    void transform(const Mat4& m) override;
    BBox bbox() const override;
    void osnap_points(std::vector<OsnapPoint>& out) const override;
    void grips(std::vector<Grip>& out) const override;
    void stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) override;
    void dxf_write(DxfWriter& w) const override;
    void draw(const DrawContext& ctx, Renderer& r) const override;

private:
    Vec3 center_{};
    double radius_{0.0};
};

class Arc final : public Entity {
public:
    Arc() : Entity(EntityType::Arc) {}

    // Angles are radians in the entity's own plane, measured counterclockwise
    // from the ECS X axis derived by arbitrary_axis(normal). The arc always
    // sweeps counterclockwise from start to end, exactly as R12 defines it.
    Arc(const Vec3& center, double radius, double start_angle, double end_angle,
        const Vec3& normal = kWorldZ)
        : Entity(EntityType::Arc),
          center_(center),
          radius_(radius),
          start_angle_(start_angle),
          end_angle_(end_angle) {
        props().normal = normalize(normal);
    }

    const Vec3& center() const { return center_; }
    double radius() const { return radius_; }
    double start_angle() const { return start_angle_; }
    double end_angle() const { return end_angle_; }

    void set_center(const Vec3& c) { center_ = c; }
    void set_radius(double r) { radius_ = r; }
    void set_angles(double start, double end) { start_angle_ = start; end_angle_ = end; }

    // Total counterclockwise sweep in radians, always in (0, 2*pi].
    double sweep() const;

    Vec3 point_at_angle(double angle) const;
    Vec3 start_point() const { return point_at_angle(start_angle_); }
    Vec3 end_point() const { return point_at_angle(end_angle_); }
    Vec3 midpoint() const { return point_at_angle(start_angle_ + sweep() * 0.5); }

    // True if `angle` lies within the counterclockwise sweep.
    bool contains_angle(double angle) const;

    EntityPtr clone() const override;
    void transform(const Mat4& m) override;
    BBox bbox() const override;
    void osnap_points(std::vector<OsnapPoint>& out) const override;
    void grips(std::vector<Grip>& out) const override;
    void stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) override;
    void dxf_write(DxfWriter& w) const override;
    void draw(const DrawContext& ctx, Renderer& r) const override;

private:
    Vec3 center_{};
    double radius_{0.0};
    double start_angle_{0.0};
    double end_angle_{0.0};
};

// A vertex of a polyline. `bulge` is R12's DXF group 42: the tangent of a
// quarter of the included angle of the arc leading to the NEXT vertex, signed
// counterclockwise. Zero is a straight segment, which is the common case.
//
// Bulge rather than an explicit arc because that is what the file format
// stores, and because it is the representation that survives editing: moving a
// vertex keeps the arc's relationship to its neighbours without recomputing a
// centre that might no longer exist.
//
// The widths are DXF groups 40 and 41, and they belong to the segment LEAVING
// this vertex -- so a taper is expressed by one vertex's end width differing
// from the next one's start width. Zero is R12's "no width", which draws as a
// centreline; that is also what the wireframe display draws for any width, see
// draw() for why.
struct PolyVertex {
    Vec3 pos{};
    double bulge{0.0};
    double start_width{0.0};
    double end_width{0.0};
};

// POLYLINE.
//
// Vertices are owned here rather than being separate database entities, which
// is the decision recorded in SF_todo.md: a 20,000-face mesh stored the R12 way
// is 20,000 entities, 20,000 undo clones, and an O(n^2) entnext walk. The DXF
// layer synthesises VERTEX and SEQEND records at the boundary, and R14's
// LWPOLYLINE went the same way.
class Polyline final : public Entity {
public:
    Polyline() : Entity(EntityType::Polyline) {}

    const std::vector<PolyVertex>& vertices() const { return vertices_; }
    std::vector<PolyVertex>& vertices() { return vertices_; }

    void add(const Vec3& p, double bulge = 0.0) { vertices_.push_back({p, bulge, 0.0, 0.0}); }
    void add(const Vec3& p, double bulge, double start_width, double end_width) {
        vertices_.push_back({p, bulge, start_width, end_width});
    }

    // PEDIT's Width: one width for every segment, which is the only width edit
    // R12 offers outside the per-vertex editor.
    void set_uniform_width(double w);

    // True when any segment carries a width. The renderer asks, so that the
    // common zero-width polyline costs nothing.
    bool has_width() const;

    std::size_t size() const { return vertices_.size(); }
    bool empty() const { return vertices_.empty(); }

    // A closed polyline has a segment from the last vertex back to the first.
    bool closed() const { return closed_; }
    void set_closed(bool c) { closed_ = c; }

    // How many segments it draws: one per vertex when closed, one fewer when
    // not. Zero for anything with fewer than two vertices.
    std::size_t segment_count() const;

    // The arc through segment `i`, if it has one. False for a straight segment
    // or an out-of-range index. Angles are in the polyline's own plane.
    bool segment_arc(std::size_t i, Vec3* centre, double* radius, double* start_angle,
                     double* end_angle) const;

    // Total length along the polyline, arcs included.
    double length() const;

    EntityPtr clone() const override;
    void transform(const Mat4& m) override;
    BBox bbox() const override;
    void osnap_points(std::vector<OsnapPoint>& out) const override;
    void grips(std::vector<Grip>& out) const override;
    void stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) override;
    void dxf_write(DxfWriter& w) const override;
    void draw(const DrawContext& ctx, Renderer& r) const override;

private:
    std::vector<PolyVertex> vertices_;
    bool closed_{false};
};

// POINT. A location and nothing else.
//
// Drawn as a small cross sized from the draw context, so it stays the same size
// on screen at any zoom. R12 draws it per PDMODE and PDSIZE, which do not exist
// here yet -- see SF_todo.md.
class PointEntity final : public Entity {
public:
    PointEntity() : Entity(EntityType::Point) {}
    explicit PointEntity(const Vec3& p) : Entity(EntityType::Point), pos_(p) {}

    const Vec3& position() const { return pos_; }
    void set_position(const Vec3& p) { pos_ = p; }

    EntityPtr clone() const override;
    void transform(const Mat4& m) override;
    BBox bbox() const override;
    void osnap_points(std::vector<OsnapPoint>& out) const override;
    void grips(std::vector<Grip>& out) const override;
    void stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) override;
    void dxf_write(DxfWriter& w) const override;
    void draw(const DrawContext& ctx, Renderer& r) const override;

private:
    Vec3 pos_{};
};

// SOLID and 3DFACE: four corners, or three with the fourth repeated.
//
// One class for both, because they differ in what they mean rather than in what
// they hold. SOLID is a filled quadrilateral in its own plane and stores its
// corners in the entity coordinate system; 3DFACE is a face in space, stores
// world coordinates, and carries per-edge visibility. Both draw as outlines
// here, since the display is wireframe.
//
// The corner order is R12's and is famously not the order you would draw them
// in: the third and fourth corners run across the shape, so a rectangle is
// given as two opposite edges rather than as a loop. Getting it wrong yields a
// bowtie, which is the traditional way to discover it.
class Face final : public Entity {
public:
    explicit Face(EntityType type) : Entity(type) {}

    const Vec3& corner(int i) const { return corners_[i]; }
    void set_corner(int i, const Vec3& p) { corners_[i] = p; }

    // Three-cornered when the fourth repeats the third, which is how the format
    // says triangle.
    bool triangular() const { return near_equal(corners_[2], corners_[3], 1e-12); }

    // 3DFACE group 70: bit 1 hides edge 1-2, bit 2 edge 2-3, and so on.
    std::int16_t edge_flags() const { return edge_flags_; }
    void set_edge_flags(std::int16_t f) { edge_flags_ = f; }
    bool edge_visible(int i) const { return (edge_flags_ & (1 << i)) == 0; }

    EntityPtr clone() const override;
    void transform(const Mat4& m) override;
    BBox bbox() const override;
    void osnap_points(std::vector<OsnapPoint>& out) const override;
    void grips(std::vector<Grip>& out) const override;
    void stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) override;
    void dxf_write(DxfWriter& w) const override;
    void draw(const DrawContext& ctx, Renderer& r) const override;

private:
    Vec3 corners_[4]{};
    std::int16_t edge_flags_{0};
};

// TEXT justification. The enumerator values ARE the DXF group values -- 72 for
// horizontal, 73 for vertical -- so the writer needs no mapping table and a
// wrong one cannot drift into existence.
//
// Aligned and Fit are horizontal modes that R12 treats specially: both take two
// points instead of a rotation, Aligned scaling the height to suit and Fit
// squeezing the width factor instead. Stored so a file round-trips; the TEXT
// command implements them as the two-point prompts they are.
enum class TextHAlign : std::uint8_t {
    Left = 0,
    Center = 1,
    Right = 2,
    Aligned = 3,
    Middle = 4,
    Fit = 5,
};

enum class TextVAlign : std::uint8_t {
    Baseline = 0,
    Bottom = 1,
    Middle = 2,
    Top = 3,
};

// TEXT.
//
// The entity exists; the glyphs do not. R12 draws text with SHX vector fonts,
// and that decision is deliberately deferred -- see CLAUDE.md. What is NOT
// deferred is the entity itself, because DXF read must be able to hold a TEXT
// record: a reader that drops text opens a drawing and saves it back emptied of
// its annotation, which is the one failure this project decided it would not
// have.
//
// Until a font arrives, draw() emits a box of the right height and approximate
// width. That is honest about what is known -- position, height, rotation and
// extent -- and wrong about nothing, where a guess at letterforms would be.
class Text final : public Entity {
public:
    Text() : Entity(EntityType::Text) {}
    Text(const Vec3& at, std::string value, double height)
        : Entity(EntityType::Text), pos_(at), value_(std::move(value)), height_(height) {}

    const Vec3& position() const { return pos_; }
    void set_position(const Vec3& p) { pos_ = p; }

    const std::string& value() const { return value_; }
    void set_value(std::string v) { value_ = std::move(v); }

    double height() const { return height_; }
    void set_height(double h) { height_ = h; }

    // Radians, in the entity's own plane, as every other angle here is.
    double rotation() const { return rotation_; }
    void set_rotation(double r) { rotation_ = r; }

    // Width factor (DXF group 41) and oblique angle (group 51), kept so a round
    // trip does not quietly normalise someone's text.
    double width_factor() const { return width_factor_; }
    void set_width_factor(double w) { width_factor_ = w; }
    double oblique() const { return oblique_; }
    void set_oblique(double o) { oblique_ = o; }

    // The width the placeholder assumes. Without a font this is a guess, and it
    // is only used for the box and the bounding box.
    double approximate_width() const;

    TextHAlign h_align() const { return h_align_; }
    TextVAlign v_align() const { return v_align_; }
    void set_align(TextHAlign h, TextVAlign v) {
        h_align_ = h;
        v_align_ = v;
    }

    // DXF group 11. R12 writes the insertion point twice for justified text:
    // group 10 stays where it was and group 11 carries the point the
    // justification is measured from. When the text is left-baseline -- the
    // default and the common case -- there is no second point and group 10 is
    // the whole story.
    const Vec3& align_point() const { return align_point_; }
    void set_align_point(const Vec3& p) { align_point_ = p; }

    bool is_justified() const {
        return h_align_ != TextHAlign::Left || v_align_ != TextVAlign::Baseline;
    }

    // Where the text actually sits: group 11 when justified, group 10 when not.
    // Everything that has to agree about where the text IS -- drawing, the
    // bounding box, the INSERT snap and the grip -- goes through this, so they
    // cannot disagree.
    const Vec3& position_for_drawing() const { return is_justified() ? align_point_ : pos_; }

    // The lower-left corner of the placeholder box, in world space, with the
    // justification applied.
    Vec3 box_origin() const;

    EntityPtr clone() const override;
    void transform(const Mat4& m) override;
    BBox bbox() const override;
    void osnap_points(std::vector<OsnapPoint>& out) const override;
    void grips(std::vector<Grip>& out) const override;
    void stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) override;
    void dxf_write(DxfWriter& w) const override;
    void draw(const DrawContext& ctx, Renderer& r) const override;

private:
    Vec3 pos_{};
    std::string value_;
    double height_{1.0};
    double rotation_{0.0};
    double width_factor_{1.0};
    double oblique_{0.0};
    Vec3 align_point_{};
    TextHAlign h_align_{TextHAlign::Left};
    TextVAlign v_align_{TextVAlign::Baseline};
};

// One DXF group: a code and its value, kept as text.
//
// Text rather than parsed, because a proxy's whole job is to give back exactly
// what it was given. Parsing to double and formatting again would round-trip
// most values and quietly alter some.
struct DxfGroup {
    int code{0};
    std::string value;
};

// An entity read from DXF that this program has no class for.
//
// It draws nothing, has no snaps and no grips, and refuses to transform. What
// it does is survive: opening a drawing and saving it writes these back byte
// for byte, so a file containing TEXT, INSERT, DIMENSION or anything from R13
// is not silently emptied by a round trip. AutoCAD does the same thing under
// the same name.
//
// Each real entity built later takes its type out of proxy status; nothing else
// has to change.
class Proxy final : public Entity {
public:
    Proxy() : Entity(EntityType::Proxy) {}

    const std::string& dxf_name() const { return dxf_name_; }
    void set_dxf_name(std::string name) { dxf_name_ = std::move(name); }

    const std::vector<DxfGroup>& groups() const { return groups_; }
    void add_group(int code, std::string value) { groups_.push_back({code, std::move(value)}); }

    EntityPtr clone() const override;

    // A no-op. A proxy cannot be moved or scaled, because doing so would mean
    // understanding which of its groups are coordinates -- which is exactly
    // what it does not know. R12 and AutoCAD both refuse rather than guess.
    void transform(const Mat4& m) override;

    // Deliberately invalid: with no geometry there is nothing to bound, and an
    // invalid box keeps a proxy out of picking, region selection and extents
    // rather than putting an invisible obstacle at the origin.
    BBox bbox() const override;

    void osnap_points(std::vector<OsnapPoint>& out) const override;
    void grips(std::vector<Grip>& out) const override;
    void stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) override;
    void dxf_write(DxfWriter& w) const override;
    void draw(const DrawContext& ctx, Renderer& r) const override;

private:
    std::string dxf_name_;
    std::vector<DxfGroup> groups_;
};

}  // namespace noto
