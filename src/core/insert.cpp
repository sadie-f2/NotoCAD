// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// INSERT: the entity, and the arithmetic that reduces a placement matrix back
// to the four fields R12 records.
//
// Everything recursive here -- drawing, bounds, snaps -- descends through the
// definition under an accumulated transform and is depth-guarded, because a DXF
// is data from elsewhere and may claim a cycle that cannot occur in a drawing
// this program built.
#include "ncad/entities.hpp"

#include "ncad/dxf.hpp"
#include "ncad/ecs.hpp"
#include "ncad/render.hpp"

#include <cmath>
#include <numbers>

namespace ncad {

BlockDef BlockDef::clone() const {
    BlockDef copy;
    copy.name = name;
    copy.base = base;
    copy.flags = flags;
    copy.entities.reserve(entities.size());
    for (const EntityPtr& e : entities) {
        if (e) copy.entities.push_back(e->clone());
    }
    return copy;
}

namespace {

// Forwards drawing through a transform.
//
// The same shape as DashRenderer, and for the same reason: an entity's draw()
// emits world-space points and knows nothing of being inside a block, so the
// transform is applied to what it emits rather than by asking it to cooperate.
// A block's contents therefore need no awareness of blocks at all.
class TransformRenderer final : public Renderer {
public:
    TransformRenderer(Renderer& inner, const Mat4& m) : inner_(inner), m_(m) {}

    void begin_entity(const EntityProps& props) override { inner_.begin_entity(props); }

    void polyline(const Vec3* pts, std::size_t count, bool closed) override {
        buffer_.clear();
        buffer_.reserve(count);
        for (std::size_t i = 0; i < count; ++i) buffer_.push_back(m_.transform_point(pts[i]));
        inner_.polyline(buffer_.data(), buffer_.size(), closed);
    }

private:
    Renderer& inner_;
    Mat4 m_;
    std::vector<Vec3> buffer_;
};

void draw_definition(const BlockDef& def, const Mat4& m, const DrawContext& ctx, Renderer& r,
                     int depth) {
    if (depth >= kMaxBlockDepth) return;

    TransformRenderer through(r, m);
    for (const EntityPtr& e : def.entities) {
        if (!e) continue;
        if (e->type() == EntityType::Insert) {
            // Recursed explicitly rather than through the wrapper, so the
            // depth guard is counted once per level instead of once per
            // wrapper in a chain of them.
            const Insert& nested = static_cast<const Insert&>(*e);
            if (const BlockDef* inner = nested.definition()) {
                for (std::int16_t row = 0; row < nested.rows(); ++row) {
                    for (std::int16_t col = 0; col < nested.columns(); ++col) {
                        draw_definition(*inner, m * nested.placement_for(row, col), ctx, r,
                                        depth + 1);
                    }
                }
            }
            continue;
        }
        r.begin_entity(e->props());
        e->draw(ctx, through);
    }
}

// Appends a definition's snap points, mapped through `m`.
void collect_snaps(const BlockDef& def, const Mat4& m, std::vector<OsnapPoint>& out, int depth) {
    if (depth >= kMaxBlockDepth) return;

    for (const EntityPtr& e : def.entities) {
        if (!e) continue;
        if (e->type() == EntityType::Insert) {
            const Insert& nested = static_cast<const Insert&>(*e);
            if (const BlockDef* inner = nested.definition()) {
                for (std::int16_t row = 0; row < nested.rows(); ++row) {
                    for (std::int16_t col = 0; col < nested.columns(); ++col) {
                        collect_snaps(*inner, m * nested.placement_for(row, col), out, depth + 1);
                    }
                }
            }
            continue;
        }

        const std::size_t first = out.size();
        e->osnap_points(out);
        for (std::size_t i = first; i < out.size(); ++i) {
            out[i].pos = m.transform_point(out[i].pos);
        }
    }
}

void accumulate_bbox(const BlockDef& def, const Mat4& m, BBox& box, int depth) {
    if (depth >= kMaxBlockDepth) return;

    for (const EntityPtr& e : def.entities) {
        if (!e) continue;
        if (e->type() == EntityType::Insert) {
            const Insert& nested = static_cast<const Insert&>(*e);
            if (const BlockDef* inner = nested.definition()) {
                for (std::int16_t row = 0; row < nested.rows(); ++row) {
                    for (std::int16_t col = 0; col < nested.columns(); ++col) {
                        accumulate_bbox(*inner, m * nested.placement_for(row, col), box,
                                        depth + 1);
                    }
                }
            }
            continue;
        }

        const BBox child = e->bbox();
        if (!child.valid()) continue;
        // The eight corners, because a rotated box's extent is not the
        // transform of its extremes.
        for (int i = 0; i < 8; ++i) {
            const Vec3 corner{(i & 1) ? child.max.x : child.min.x,
                              (i & 2) ? child.max.y : child.min.y,
                              (i & 4) ? child.max.z : child.min.z};
            box.expand(m.transform_point(corner));
        }
    }
}

void flatten_definition(const BlockDef& def, const Mat4& m, std::vector<EntityPtr>& out,
                        int depth) {
    if (depth >= kMaxBlockDepth) return;

    for (const EntityPtr& e : def.entities) {
        if (!e) continue;
        if (e->type() == EntityType::Insert) {
            const Insert& nested = static_cast<const Insert&>(*e);
            if (const BlockDef* inner = nested.definition()) {
                for (std::int16_t row = 0; row < nested.rows(); ++row) {
                    for (std::int16_t col = 0; col < nested.columns(); ++col) {
                        flatten_definition(*inner, m * nested.placement_for(row, col), out,
                                           depth + 1);
                    }
                }
            }
            continue;
        }
        EntityPtr copy = e->clone();
        copy->transform(m);
        out.push_back(std::move(copy));
    }
}

}  // namespace

void flatten_insert(const Insert& ins, std::vector<EntityPtr>& out) {
    const BlockDef* def = ins.definition();
    if (!def) return;
    for (std::int16_t row = 0; row < ins.rows(); ++row) {
        for (std::int16_t col = 0; col < ins.columns(); ++col) {
            flatten_definition(*def, ins.placement_for(row, col), out, 0);
        }
    }
}

void Insert::set_array(std::int16_t rows, std::int16_t columns, double row_spacing,
                       double column_spacing) {
    rows_ = rows > 0 ? rows : 1;
    columns_ = columns > 0 ? columns : 1;
    row_spacing_ = row_spacing;
    column_spacing_ = column_spacing;
}

Vec3 Insert::insertion_point() const {
    return placement_.transform_point(def_ ? def_->base : Vec3{});
}

Mat4 Insert::placement_for(std::int16_t row, std::int16_t column) const {
    if (row == 0 && column == 0) return placement_;

    // R12 spaces MINSERT copies along the insert's OWN axes, not the world's,
    // so a rotated MINSERT arrays along its rotation rather than along X and Y.
    // Composing the offset on the left of the placement in its own frame is
    // what expresses that.
    const Vec3 offset{column_spacing_ * static_cast<double>(column),
                      row_spacing_ * static_cast<double>(row), 0.0};
    return placement_ * Mat4::translation(offset);
}

EntityPtr Insert::clone() const {
    auto copy = std::make_unique<Insert>(def_, placement_);
    copy->rows_ = rows_;
    copy->columns_ = columns_;
    copy->row_spacing_ = row_spacing_;
    copy->column_spacing_ = column_spacing_;
    copy_common_to(*copy);
    return copy;
}

void Insert::transform(const Mat4& m) {
    // The whole point of storing a matrix: every transform is a composition,
    // and none of them needs to be representable as R12's fields until the
    // drawing is written.
    placement_ = m * placement_;
}

BBox Insert::bbox() const {
    BBox box;
    if (!def_) return box;
    for (std::int16_t row = 0; row < rows_; ++row) {
        for (std::int16_t col = 0; col < columns_; ++col) {
            accumulate_bbox(*def_, placement_for(row, col), box, 0);
        }
    }
    return box;
}

void Insert::osnap_points(std::vector<OsnapPoint>& out) const {
    // The insertion point itself, which is what R12's INS snap means and the
    // one point on a block reference that is exactly defined.
    out.push_back({insertion_point(), OsnapType::Insert});

    if (!def_) return;
    for (std::int16_t row = 0; row < rows_; ++row) {
        for (std::int16_t col = 0; col < columns_; ++col) {
            collect_snaps(*def_, placement_for(row, col), out, 0);
        }
    }
}

void Insert::grips(std::vector<Grip>& out) const {
    // One grip, at the insertion point. R12 gives a block reference exactly
    // one: its contents are not editable in place, so there is nothing else on
    // it that dragging could mean.
    out.push_back(Grip{insertion_point(), GripKind::Stretch, 0});
}

void Insert::stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (indices[i] == 0) placement_ = Mat4::translation(delta) * placement_;
    }
}

void Insert::draw(const DrawContext& ctx, Renderer& r) const {
    if (!def_) return;
    for (std::int16_t row = 0; row < rows_; ++row) {
        for (std::int16_t col = 0; col < columns_; ++col) {
            draw_definition(*def_, placement_for(row, col), ctx, r, 0);
        }
    }
}

void Insert::dxf_write(DxfWriter& w) const {
    if (!def_) return;  // an insert with no definition is not a drawable thing

    // This is where the matrix becomes R12's four fields. Anything R12 can
    // express survives exactly; a shear does not and is approximated, which is
    // the trade the header explains.
    const InsertPlacement p = decompose_placement(placement_, def_->base);

    w.write_common(*this);
    w.code(2, def_->name);
    // The insertion point is in the entity's own coordinate system, like every
    // other ECS entity here.
    w.point(10, world_to_ecs(p.normal).transform_point(p.insertion));

    // Omitted when unity, which is what R12 writes and keeps a plain insert
    // byte-for-byte minimal.
    if (p.scale.x != 1.0) w.code(41, p.scale.x);
    if (p.scale.y != 1.0) w.code(42, p.scale.y);
    if (p.scale.z != 1.0) w.code(43, p.scale.z);
    if (p.rotation != 0.0) w.code(50, p.rotation * 180.0 / std::numbers::pi);

    // The extrusion belongs to AcDbBlockReference, so on a MINSERT it has to be
    // written BEFORE the second marker -- the same rule that puts an ARC's 210
    // before AcDbArc. A plain INSERT has only the one class and no such
    // ordering to get wrong.
    if (is_array() && dxf_requires_handles(w.version())) w.write_extrusion(p.normal);

    if (is_array()) {
        // A MINSERT is a block reference that then declares itself an
        // AcDbMInsertBlock; the row and column counts belong to that part of
        // the class, not to the reference. A plain INSERT stops at
        // AcDbBlockReference and never reaches here.
        w.subclass("AcDbMInsertBlock");
        w.code(70, static_cast<int>(columns_));
        w.code(71, static_cast<int>(rows_));
        if (column_spacing_ != 0.0) w.code(44, column_spacing_);
        if (row_spacing_ != 0.0) w.code(45, row_spacing_);
    }

    if (!is_array() || !dxf_requires_handles(w.version())) w.write_extrusion(p.normal);
}

// --- placement decomposition ------------------------------------------------

InsertPlacement decompose_placement(const Mat4& m, const Vec3& base) {
    InsertPlacement out;

    // The linear part's columns are the transformed unit axes: their lengths
    // are the scale factors and their directions are the rotation.
    const Vec3 ax = m.transform_vector({1, 0, 0});
    const Vec3 ay = m.transform_vector({0, 1, 0});
    const Vec3 az = m.transform_vector({0, 0, 1});

    out.scale = {length(ax), length(ay), length(az)};
    out.insertion = m.transform_point(base);

    Vec3 normal = normalize(az);
    if (is_zero(normal)) normal = kWorldZ;

    // A mirror shows up as a left-handed frame. R12 records that as a negative
    // scale factor rather than as a flipped extrusion, so that the block's own
    // sense of "up" survives.
    if (dot(cross(ax, ay), az) < 0.0) out.scale.x = -out.scale.x;

    out.normal = normal;

    // The rotation is where the transformed X axis points, measured in the
    // plane the extrusion defines.
    const Basis b = arbitrary_axis(normal);
    const Vec3 flat = ax - normal * dot(ax, normal);
    out.rotation = is_zero(flat) ? 0.0 : std::atan2(dot(flat, b.ay), dot(flat, b.ax));
    return out;
}

Mat4 compose_placement(const InsertPlacement& p, const Vec3& base) {
    const Basis b = arbitrary_axis(p.normal);

    // Rotate within the extrusion's plane, then lift into it: the block's own
    // axes map to the rotated basis vectors.
    const Vec3 ax = (b.ax * std::cos(p.rotation) + b.ay * std::sin(p.rotation)) * p.scale.x;
    const Vec3 ay = (b.ay * std::cos(p.rotation) - b.ax * std::sin(p.rotation)) * p.scale.y;
    const Vec3 az = b.az * p.scale.z;

    // Built column by column rather than through from_basis(), which builds the
    // world-TO-basis matrix -- axes in the rows, origin projected out. What is
    // wanted here is the other direction: the definition's own axes mapped into
    // the world, which is the same vectors as columns.
    Mat4 linear = Mat4::identity();
    linear.m[0][0] = ax.x; linear.m[1][0] = ax.y; linear.m[2][0] = ax.z;
    linear.m[0][1] = ay.x; linear.m[1][1] = ay.y; linear.m[2][1] = ay.z;
    linear.m[0][2] = az.x; linear.m[1][2] = az.y; linear.m[2][2] = az.z;

    // The base point has to land on the insertion point, which fixes the
    // translation once the linear part is known.
    return Mat4::translation(p.insertion - linear.transform_point(base)) * linear;
}

}  // namespace ncad
