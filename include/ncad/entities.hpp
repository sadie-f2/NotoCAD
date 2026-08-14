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

#include "ncad/blocks.hpp"
#include "ncad/entity.hpp"

#include <string>
#include <vector>

namespace ncad {

// A whole turn in radians. Named rather than spelled out because it appears as
// a default argument, where a literal would be unreadable.
inline constexpr double kFullTurn = 6.283185307179586476925286766559;


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
    double length() const { return ncad::length(direction()); }

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
// ELLIPSE -- and the first deliberate divergence from R12's entity set.
//
// AC1009 has no ELLIPSE: R12 drew one as a polyline approximation and stored it
// as such, so a drawing could not later be asked what the ellipse actually was.
// This holds it exactly -- centre, axes, ratio -- and degrades to a polyline on
// DXF write, which keeps interchange honest while osnaps, intersections and
// transforms work on the real curve. CLAUDE.md sets that rule out.
//
// The parameterisation is DXF R13's, and it is the ellipse's OWN parameter
// rather than a true angle:
//
//     p(t) = centre + major*cos(t) + minor*sin(t)
//
// which keeps the arithmetic linear and matches groups 41 and 42 for the day a
// later DXF version is written. A true angle would need an arctangent at every
// evaluation and would not match anything.
class Ellipse final : public Entity {
public:
    Ellipse() : Entity(EntityType::Ellipse) {}

    // `major` runs from the centre to the end of the major axis -- half the
    // long diameter, DXF group 11. `ratio` is minor over major, in (0, 1].
    Ellipse(const Vec3& centre, const Vec3& major, double ratio, double start_param = 0.0,
            double end_param = kFullTurn, const Vec3& normal = kWorldZ)
        : Entity(EntityType::Ellipse),
          center_(centre),
          major_(major),
          ratio_(ratio),
          start_param_(start_param),
          end_param_(end_param) {
        props().normal = normalize(normal);
    }

    const Vec3& center() const { return center_; }
    const Vec3& major_axis() const { return major_; }
    double ratio() const { return ratio_; }
    double start_param() const { return start_param_; }
    double end_param() const { return end_param_; }

    void set_center(const Vec3& c) { center_ = c; }
    void set_major_axis(const Vec3& v) { major_ = v; }
    void set_ratio(double r) { ratio_ = r; }
    void set_params(double s, double e) {
        start_param_ = s;
        end_param_ = e;
    }

    // Perpendicular to the major axis, in the entity's plane, already scaled.
    Vec3 minor_axis() const;

    double major_length() const { return length(major_); }
    double minor_length() const { return length(major_) * ratio_; }

    // Parameter span, in (0, 2*pi]. A zero span means a full ellipse, the same
    // convention Arc::sweep() uses for a zero sweep.
    double sweep() const;
    bool is_full() const;

    Vec3 point_at(double param) const;
    Vec3 start_point() const { return point_at(start_param_); }
    Vec3 end_point() const { return point_at(end_param_); }

    // The four axis endpoints, in the order +major, +minor, -major, -minor.
    void axis_points(Vec3 out[4]) const;

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
    Vec3 major_{1.0, 0.0, 0.0};
    double ratio_{1.0};
    double start_param_{0.0};
    double end_param_{kFullTurn};
};

// SPLINE -- a NURBS curve, and the largest divergence from R12 so far.
//
// R12's "spline" was not a curve. PEDIT fitted a quadratic or cubic B-spline
// through a polyline's vertices and stored the RESULT as more polyline
// vertices, so the drawing never knew what the spline had been and Decurve
// worked only by keeping the originals around. AC1009 has no SPLINE entity to
// say it in.
//
// This is the real thing: degree, control points, a knot vector, and optional
// weights. It degrades to a polyline on DXF write exactly as Ellipse does, and
// for the same reason -- CLAUDE.md's rule is that richer geometry is welcome in
// the database provided it leaves honestly.
//
// FIT POINTS are kept when the curve was built by interpolation, which is the
// usual case: a designer picks points they want the curve to PASS THROUGH, and
// control points are an implementation detail they never asked about. Holding
// both means a grip can move a fit point and the curve be re-solved, rather
// than the user being handed control points and told they are the same thing.
// DXF R13 carries both for the same reason (groups 10 and 11).
// The highest degree a spline may have.
//
// Not a limit of the mathematics -- it is what lets the evaluator keep its
// basis-function scratch on the stack, which matters because that runs once per
// evaluated point of every spline in every frame. Fifteen is far above anything
// a drawing contains; cubic is the overwhelming case and R12 had no splines at
// all. A file naming a higher degree fails valid() and is kept as a Proxy
// rather than silently drawn wrong.
inline constexpr int kMaxSplineDegree = 15;

class Spline final : public Entity {
public:
    Spline() : Entity(EntityType::Spline) {}

    // The general form. `knots` must hold control_points.size() + degree + 1
    // entries; `weights` is either empty (non-rational, the common case) or the
    // same length as the control points.
    Spline(int degree, std::vector<Vec3> control_points, std::vector<double> knots,
           std::vector<double> weights = {}, const Vec3& normal = kWorldZ);

    // A curve through the given points, degree 3 unless there are too few.
    // This is what the SPLINE command makes and what most callers want.
    static EntityPtr interpolating(const std::vector<Vec3>& through, int degree = 3,
                                   const Vec3& normal = kWorldZ);

    int degree() const { return degree_; }
    const std::vector<Vec3>& control_points() const { return control_; }
    const std::vector<double>& knots() const { return knots_; }
    const std::vector<double>& weights() const { return weights_; }
    const std::vector<Vec3>& fit_points() const { return fit_; }

    bool is_rational() const { return !weights_.empty(); }
    bool has_fit_points() const { return !fit_.empty(); }

    // Whether the curve is valid enough to evaluate. A spline read from a file
    // or built by entmake can be none of those things, and every method below
    // returns something harmless rather than reading past an array when it is
    // not -- these run in the render path, where an exception is not an option.
    bool valid() const;

    // The usable parameter range: knots[degree] to knots[n], outside which the
    // basis functions do not sum to one.
    double domain_min() const;
    double domain_max() const;

    // De Boor. Clamped to the domain rather than extrapolating, which a NURBS
    // curve cannot meaningfully do.
    Vec3 point_at(double u) const;

    // Unit tangent, or a zero vector at a degenerate point. Needed by osnaps
    // and by anything that continues from the end of a curve.
    Vec3 tangent_at(double u) const;

    Vec3 start_point() const { return point_at(domain_min()); }
    Vec3 end_point() const { return point_at(domain_max()); }

    // True when the first and last control points coincide. Not periodicity --
    // a closed knot vector is a different thing and is not built here.
    bool is_closed() const;

    // Re-solves the control points from the fit points, after one has moved.
    // Does nothing when there are no fit points, since then the control points
    // are the authored data and there is nothing to solve from.
    void refit();

    EntityPtr clone() const override;
    void transform(const Mat4& m) override;
    BBox bbox() const override;
    void osnap_points(std::vector<OsnapPoint>& out) const override;
    void grips(std::vector<Grip>& out) const override;
    void stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) override;
    void dxf_write(DxfWriter& w) const override;
    void draw(const DrawContext& ctx, Renderer& r) const override;

    // How finely to flatten, for a given chord tolerance. Exposed because the
    // DXF degrade and the renderer must agree about what the curve looks like.
    int segment_count(double chord_tolerance) const;

    // Public because DXF READ needs it: a SPLINE record carries its fit points
    // (group 11) alongside its control points, and they are the user's own
    // input rather than something derivable from the curve. Setting them does
    // not re-solve anything -- see interpolating() for the direction that does.
    void set_fit_points(std::vector<Vec3> pts) { fit_ = std::move(pts); }

private:

    int degree_{3};
    std::vector<Vec3> control_;
    std::vector<double> knots_;
    std::vector<double> weights_;
    std::vector<Vec3> fit_;
};

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
// Where an MTEXT's insertion point sits relative to the block of text. DXF
// group 71, and the numbering is the format's own.
enum class MTextAttach : std::uint8_t {
    TopLeft = 1,
    TopCenter = 2,
    TopRight = 3,
    MiddleLeft = 4,
    MiddleCenter = 5,
    MiddleRight = 6,
    BottomLeft = 7,
    BottomCenter = 8,
    BottomRight = 9,
};

// MTEXT -- a paragraph, and the third deliberate divergence from R12's entity
// set after ELLIPSE and SPLINE.
//
// AC1009 has TEXT only: one line per entity, no wrapping, no inline formatting.
// Modern drawings put nearly all of their annotation in MTEXT, so before this
// existed a 2018 file's notes and callouts arrived as Proxy and drew as
// nothing. Held exactly here and degraded to a run of TEXT records on the way
// out, which is the same bargain the other two take.
//
// WHAT IS HELD EXACTLY is the raw string, inline codes and all. Stripping them
// at read time would be the cheap option and it destroys the entity: a file
// opened and saved would lose its formatting permanently, which is the round
// trip Sadie was bitten by on ellipses. Formatting is discarded at LAYOUT time
// instead, where it costs nothing and is recomputed on demand.
//
// The wrap needs exact advance widths, so this could not have been built before
// the Hershey font landed -- StrokeFont::width() is what decides where a line
// breaks.
class MText final : public Entity {
public:
    MText() : Entity(EntityType::MText) {}
    MText(const Vec3& at, std::string text, double height, double width = 0.0)
        : Entity(EntityType::MText),
          pos_(at),
          text_(std::move(text)),
          height_(height),
          width_(width) {}

    const Vec3& position() const { return pos_; }
    void set_position(const Vec3& p) { pos_ = p; }

    // The raw contents, inline codes included. See layout() for the readable
    // form; this is what survives a round trip.
    const std::string& text() const { return text_; }
    void set_text(std::string t) { text_ = std::move(t); }

    // Cap height of one line, DXF group 40 -- the same meaning Text::height has.
    double height() const { return height_; }
    void set_height(double h) { height_ = h; }

    // Group 41: the width the text wraps inside. Zero means no wrapping, and
    // then only explicit \P breaks divide the lines.
    double reference_width() const { return width_; }
    void set_reference_width(double w) { width_ = w; }

    double rotation() const { return rotation_; }
    void set_rotation(double r) { rotation_ = r; }

    MTextAttach attach() const { return attach_; }
    void set_attach(MTextAttach a) { attach_ = a; }

    // Group 44, a multiplier on the default spacing. R12's TEXT has no
    // equivalent, so the degrade computes positions from it rather than
    // preserving it.
    double line_spacing() const { return line_spacing_; }
    void set_line_spacing(double f) { line_spacing_ = f; }

    // Distance between successive baselines, in world units.
    double line_height() const;

    // The readable form: formatting removed, \P honoured, and wrapped to the
    // reference width. draw(), bbox() and the DXF degrade ALL go through this,
    // so what is shown, what is picked and what is written cannot disagree.
    void layout(std::vector<std::string>& out) const;

    // Width of the widest laid-out line, in world units.
    double laid_out_width() const;

    // World position of the first line's baseline start, with the attachment
    // applied. Successive lines step down by line_height().
    Vec3 baseline_origin() const;

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
    std::string text_;
    double height_{1.0};
    double width_{0.0};
    double rotation_{0.0};
    double line_spacing_{1.0};
    MTextAttach attach_{MTextAttach::TopLeft};
};

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

    // The advance width of the string in world units, summed from the bundled
    // stroke font. Exact, not a nominal -- which is what lets justification, the
    // bounding box and TEXT's Aligned and Fit modes all rely on it.
    double text_width() const;

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

    // Where the baseline starts, in world space, with the justification
    // applied: the pen's origin for the first glyph. Everything that has to
    // agree about where the text sits on the page goes through this.
    Vec3 baseline_origin() const;

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

// INSERT, and MINSERT, which is the same entity with a row and column count.
//
// PLACEMENT IS A FULL MATRIX. R12 stores an insertion point, three scale
// factors, one rotation angle and an extrusion, which between them describe
// translate * rotate * scale and nothing else. This holds the whole transform
// instead and reduces it to those fields at DXF write time.
//
// That is the same convention every other entity here follows -- geometry in
// world space, entity coordinates synthesised at serialisation -- and it is
// what lets ROTATE3D act on a block reference without a special case. The cost
// is that a transform R12 cannot express (a shear, from non-uniform scale
// composed with rotation) is approximated on the way out, exactly as a
// non-uniform scale of a CIRCLE already is. The alternative was refusing the
// transform, which would make ROTATE3D fail on one entity type out of nine.
//
// The definition is held by pointer, not by name or by value. Nothing in the
// vtable is handed a database -- `osnap_points()` takes only an output vector --
// so an insert that had to look its block up could not draw or snap at all.
// Block definitions live at stable addresses for exactly this reason, and
// pointing at the definition rather than copying it is also what makes R12's
// redefinition behaviour fall out: rewriting a block updates every insertion.
class Insert final : public Entity {
public:
    Insert() : Entity(EntityType::Insert) {}
    Insert(const BlockDef* def, const Mat4& placement)
        : Entity(EntityType::Insert), def_(def), placement_(placement) {}

    const BlockDef* definition() const { return def_; }
    void set_definition(const BlockDef* def) { def_ = def; }

    // Maps the definition's own coordinates into the drawing.
    const Mat4& placement() const { return placement_; }
    void set_placement(const Mat4& m) { placement_ = m; }

    // Where the block's base point landed. This is the INSERT's group 10, the
    // point it was placed by, and its INS snap.
    Vec3 insertion_point() const;

    // MINSERT: a rectangular array of copies, without the array being separate
    // entities. Spacings are in the insert's own rotated frame, as R12 has them.
    std::int16_t rows() const { return rows_; }
    std::int16_t columns() const { return columns_; }
    double row_spacing() const { return row_spacing_; }
    double column_spacing() const { return column_spacing_; }
    void set_array(std::int16_t rows, std::int16_t columns, double row_spacing,
                   double column_spacing);

    bool is_array() const { return rows_ > 1 || columns_ > 1; }

    // The transform placing copy (row, col). Equal to placement() for a plain
    // INSERT, which is what keeps the array a detail rather than a second path.
    Mat4 placement_for(std::int16_t row, std::int16_t column) const;

    EntityPtr clone() const override;
    void transform(const Mat4& m) override;
    BBox bbox() const override;
    void osnap_points(std::vector<OsnapPoint>& out) const override;
    void grips(std::vector<Grip>& out) const override;
    void stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) override;
    void dxf_write(DxfWriter& w) const override;
    void draw(const DrawContext& ctx, Renderer& r) const override;

private:
    const BlockDef* def_{nullptr};
    Mat4 placement_{Mat4::identity()};
    std::int16_t rows_{1};
    std::int16_t columns_{1};
    double row_spacing_{0.0};
    double column_spacing_{0.0};
};

// The scale and rotation R12 would record for a placement, recovered from the
// matrix. Exact for any translate/rotate/scale; approximate for a shear, which
// R12's fields cannot express at all -- see the Insert comment.
struct InsertPlacement {
    Vec3 insertion{};
    Vec3 scale{1, 1, 1};
    double rotation{0.0};  // radians, in the plane of `normal`
    Vec3 normal{0, 0, 1};
};

InsertPlacement decompose_placement(const Mat4& m, const Vec3& base);

// Everything a block reference draws, as ordinary world-space entities.
//
// Transformed COPIES, which is the point: the derived object snaps and the
// intersection kernel both dispatch on entity type and solve in world space,
// and neither can be taught about blocks without teaching every solver about
// them. Flattening once at the boundary means NEA, PER, TAN and INT work inside
// a block without a single line of block awareness in any of them.
//
// It allocates, and it is reached from the cursor path. That is affordable
// because the callers run their broad phase first, so only the reference
// actually under the cursor is ever flattened -- and it is the same linear-scan
// bargain the rest of the pick path already makes. See SF_todo.md on the
// spatial index.
//
// Nested references are flattened too, depth-guarded. MINSERT yields one copy
// per array element.
void flatten_insert(const Insert& ins, std::vector<EntityPtr>& out);

// The inverse: the matrix R12's fields describe. Round-trips with the above for
// everything R12 can represent, which is what the DXF tests pin.
Mat4 compose_placement(const InsertPlacement& p, const Vec3& base);

// Which kind of measurement a DIMENSION states. The values are DXF group 70's
// dimension-type field, so the enum IS the serialised form -- the same trick
// TextHAlign plays with group 72.
enum class DimKind : std::uint8_t {
    Linear = 0,    // rotated, and horizontal/vertical are rotations of it
    Aligned = 1,   // parallel to the two points it measures
    Diameter = 3,
    Radius = 4,
};

// DIMENSION: a measurement the drawing carries.
//
// NON-ASSOCIATIVE, as R12's are. The dimension holds the points it was given
// and nothing watches the geometry those points were taken from; moving a line
// does not move the dimension that measured it. That is a real limitation and
// it is also what makes dimensions cheap enough to have -- associativity needs
// a dependency graph the database does not have and does not want yet.
//
// IT GENERATES ITS OWN GEOMETRY. `regenerate()` produces the lines, arrowheads
// and text, and BOTH `draw()` and `dxf_write()` call it. That is the rule the
// rest of this codebase states for preview versus commit: derive the same thing
// with the same code, differing only in what is done with the result. A second
// generator for the DXF block is exactly how the two would drift.
//
// THE STYLE IS BAKED IN. Text height, arrow size and the extension offsets are
// captured from the DIM system variables when the dimension is made, not read
// back at draw time. Three reasons, and the first is decisive: `draw()` is
// handed no database and could not read them. It is also R12's behaviour --
// dimensions are drawn into a block at creation and do not follow later
// variable changes until UPDATE re-applies them -- and it means a drawing
// read from DXF looks like what the file said rather than like the reader's
// current settings.
class Dimension final : public Entity {
public:
    Dimension() : Entity(EntityType::Dimension) {}

    DimKind kind() const { return kind_; }
    void set_kind(DimKind k) { kind_ = k; }

    bool radial() const { return kind_ == DimKind::Radius || kind_ == DimKind::Diameter; }

    // DXF group 10. Where the dimension line runs for a linear dimension; the
    // centre of the curve for a radial one.
    const Vec3& definition() const { return definition_; }
    void set_definition(const Vec3& p) { definition_ = p; }

    // Groups 13 and 14 for a linear dimension: the two points being measured.
    // For a radial one `first` is group 15, the point where the leader meets
    // the curve, and `second` is unused.
    const Vec3& first() const { return first_; }
    const Vec3& second() const { return second_; }
    void set_points(const Vec3& a, const Vec3& b) {
        first_ = a;
        second_ = b;
    }

    // Group 50: which way a Linear dimension measures, in its own plane. Zero
    // is horizontal and a quarter turn is vertical, which is all R12's HOR and
    // VER ever were.
    double rotation() const { return rotation_; }
    void set_rotation(double radians) { rotation_ = radians; }

    // Group 1. Empty means "use the measurement", which is the usual case;
    // anything else replaces the whole label, as R12 has it.
    const std::string& text_override() const { return text_; }
    void set_text_override(std::string s) { text_ = std::move(s); }

    // The baked style. `apply_style` is what the commands call, having read the
    // DIM variables once.
    void apply_style(double text_height, double arrow_size, double ext_offset,
                     double ext_beyond);
    double text_height() const { return text_height_; }
    double arrow_size() const { return arrow_size_; }
    double ext_offset() const { return ext_offset_; }
    double ext_beyond() const { return ext_beyond_; }

    // What this dimension states, in drawing units. Group 42 on the way out,
    // and recomputed rather than stored so it cannot disagree with the points.
    double measurement() const;

    // The label as drawn: the override, or the measurement formatted with the
    // prefix its kind wants.
    std::string label() const;

    // The drawn form: lines, SOLID arrowheads and one TEXT. Appended to `out`.
    //
    // The single source for the viewport and for the DXF block, which is the
    // whole design. Callers own the result.
    void regenerate(std::vector<EntityPtr>& out) const;

    EntityPtr clone() const override;
    void transform(const Mat4& m) override;
    BBox bbox() const override;
    void osnap_points(std::vector<OsnapPoint>& out) const override;
    void grips(std::vector<Grip>& out) const override;
    void stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) override;
    void dxf_write(DxfWriter& w) const override;
    void draw(const DrawContext& ctx, Renderer& r) const override;

private:
    DimKind kind_{DimKind::Linear};
    Vec3 definition_{};
    Vec3 first_{};
    Vec3 second_{};
    double rotation_{0.0};
    std::string text_;

    // Captured from DIMTXT, DIMASZ, DIMEXO and DIMEXE, already multiplied by
    // DIMSCALE. Defaults are R12's own, so a dimension built without a style
    // still looks like one.
    double text_height_{2.5};
    double arrow_size_{2.5};
    double ext_offset_{0.625};
    double ext_beyond_{1.25};
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

}  // namespace ncad
