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
    void dxf_write(DxfWriter& w) const override;

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
    void dxf_write(DxfWriter& w) const override;

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
    void dxf_write(DxfWriter& w) const override;

private:
    Vec3 center_{};
    double radius_{0.0};
    double start_angle_{0.0};
    double end_angle_{0.0};
};

}  // namespace noto
