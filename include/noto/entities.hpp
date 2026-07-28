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
struct PolyVertex {
    Vec3 pos{};
    double bulge{0.0};
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

    void add(const Vec3& p, double bulge = 0.0) { vertices_.push_back({p, bulge}); }

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

}  // namespace noto
