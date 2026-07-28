// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// POINT, SOLID and 3DFACE.
#include "noto/dxf.hpp"
#include "noto/ecs.hpp"
#include "noto/entities.hpp"
#include "noto/render.hpp"

namespace noto {
namespace {

// A point has no size of its own, so its marker is sized from the flattening
// tolerance -- which is half a pixel of sag. Six of those is a cross a few
// pixels across, at any zoom.
constexpr double kPointMarkerTolerances = 6.0;

}  // namespace

// --- POINT ------------------------------------------------------------------

EntityPtr PointEntity::clone() const {
    auto copy = std::make_unique<PointEntity>(pos_);
    copy_common_to(*copy);
    return copy;
}

void PointEntity::transform(const Mat4& m) { pos_ = m.transform_point(pos_); }

BBox PointEntity::bbox() const {
    BBox b;
    b.expand(pos_);
    return b;
}

void PointEntity::osnap_points(std::vector<OsnapPoint>& out) const {
    // NODE is what a point offers, and the only entity that offers it -- which
    // is why the snap exists at all.
    out.push_back({pos_, OsnapType::Node});
}

void PointEntity::grips(std::vector<Grip>& out) const {
    out.push_back(Grip{pos_, GripKind::Stretch, 0});
}

void PointEntity::stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (indices[i] == 0) pos_ = pos_ + delta;
    }
}

void PointEntity::draw(const DrawContext& ctx, Renderer& r) const {
    const double h = ctx.chord_tolerance * kPointMarkerTolerances;
    if (h <= 0.0) {
        // No scale to work from: emit the location itself rather than nothing,
        // so hit-testing can still find it.
        r.polyline(&pos_, 1, false);
        return;
    }
    const Vec3 across[2] = {pos_ - Vec3{h, 0, 0}, pos_ + Vec3{h, 0, 0}};
    const Vec3 down[2] = {pos_ - Vec3{0, h, 0}, pos_ + Vec3{0, h, 0}};
    r.polyline(across, 2, false);
    r.polyline(down, 2, false);
}

void PointEntity::dxf_write(DxfWriter& w) const {
    w.write_common(*this);
    w.point(10, world_to_ecs(props().normal).transform_point(pos_));
    w.write_extrusion(props().normal);
}

// --- SOLID and 3DFACE -------------------------------------------------------

EntityPtr Face::clone() const {
    auto copy = std::make_unique<Face>(type());
    for (int i = 0; i < 4; ++i) copy->corners_[i] = corners_[i];
    copy->edge_flags_ = edge_flags_;
    copy_common_to(*copy);
    return copy;
}

void Face::transform(const Mat4& m) {
    for (Vec3& c : corners_) c = m.transform_point(c);

    // A SOLID lives in a plane and carries an extrusion; rebuild it from the
    // transformed corners so a rotation out of plane is recorded rather than
    // leaving a normal that disagrees with the geometry. A 3DFACE has no plane
    // to speak of and keeps world Z.
    if (type() == EntityType::Solid) {
        const Vec3 n = cross(corners_[1] - corners_[0], corners_[2] - corners_[0]);
        if (!is_zero(n)) props().normal = normalize(n);
    }
}

BBox Face::bbox() const {
    BBox b;
    for (const Vec3& c : corners_) b.expand(c);
    return b;
}

void Face::osnap_points(std::vector<OsnapPoint>& out) const {
    // Corners are endpoints; edge midpoints follow the outline, so they use the
    // drawn order rather than the stored one.
    const int order[4] = {0, 1, 3, 2};
    const int count = triangular() ? 3 : 4;

    for (int i = 0; i < count; ++i) {
        out.push_back({corners_[order[i]], OsnapType::Endpoint});
    }
    for (int i = 0; i < count; ++i) {
        const Vec3& a = corners_[order[i]];
        const Vec3& b = corners_[order[(i + 1) % count]];
        out.push_back({(a + b) * 0.5, OsnapType::Midpoint});
    }
}

void Face::grips(std::vector<Grip>& out) const {
    const int count = triangular() ? 3 : 4;
    for (int i = 0; i < count; ++i) {
        out.push_back(Grip{corners_[i], GripKind::Stretch, static_cast<GripIndex>(i)});
    }
}

void Face::stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) {
    // Asked before anything moves. A triangle is "corner 3 equals corner 4", so
    // moving corner 3 first would make the test false exactly when it needs to
    // be true, and the fourth corner would be left behind -- splitting the
    // shape open along an edge that had no length until then.
    const bool was_triangular = triangular();

    for (std::size_t k = 0; k < count; ++k) {
        const GripIndex i = indices[k];
        if (i < 4) corners_[i] = corners_[i] + delta;
    }
    if (was_triangular) corners_[3] = corners_[2];
}

void Face::draw(const DrawContext&, Renderer& r) const {
    // Drawn order, not stored order: R12 stores the third and fourth corners
    // across the shape, so walking them as given produces a bowtie.
    const int order[4] = {0, 1, 3, 2};
    const int count = triangular() ? 3 : 4;

    if (type() == EntityType::Solid) {
        Vec3 loop[4];
        for (int i = 0; i < count; ++i) loop[i] = corners_[order[i]];
        r.polyline(loop, static_cast<std::size_t>(count), true);
        return;
    }

    // A 3DFACE may hide edges, so each is drawn on its own terms. The flags are
    // numbered by the drawn order, which is what makes them agree with what a
    // mesh looks like.
    for (int i = 0; i < count; ++i) {
        if (!edge_visible(i)) continue;
        const Vec3 seg[2] = {corners_[order[i]], corners_[order[(i + 1) % count]]};
        r.polyline(seg, 2, false);
    }
}

void Face::dxf_write(DxfWriter& w) const {
    w.write_common(*this);

    if (type() == EntityType::Solid) {
        // SOLID stores its corners in the entity coordinate system.
        const Mat4 to_ecs = world_to_ecs(props().normal);
        for (int i = 0; i < 4; ++i) w.point(10 + i, to_ecs.transform_point(corners_[i]));
        w.write_extrusion(props().normal);
        return;
    }

    // 3DFACE is world coordinates throughout, and has no extrusion.
    for (int i = 0; i < 4; ++i) w.point(10 + i, corners_[i]);
    if (edge_flags_ != 0) w.code(70, static_cast<int>(edge_flags_));
}

}  // namespace noto
