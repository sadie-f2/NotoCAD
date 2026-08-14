// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Entity base class and the dispatch surface every entity kind implements.
//
// Deliberately a single level of inheritance: this base, and concrete entities
// directly under it. No hierarchies, no RTTI -- `type()` returns an enum, which
// DXF needs anyway.
#pragma once

#include "ncad/bbox.hpp"
#include "ncad/grip.hpp"
#include "ncad/mat4.hpp"
#include "ncad/osnap.hpp"
#include "ncad/vec3.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace ncad {

class DxfWriter;
class Renderer;
struct DrawContext;

using Handle = std::uint64_t;
inline constexpr Handle kNullHandle = 0;

using LayerId = std::uint16_t;
using LinetypeId = std::uint16_t;

inline constexpr LayerId kLayerZero = 0;
inline constexpr LinetypeId kLinetypeContinuous = 0;

// DXF group 62 conventions.
inline constexpr std::int16_t kColorByBlock = 0;
inline constexpr std::int16_t kColorByLayer = 256;

enum class EntityType : std::uint8_t {
    Line,
    Circle,
    Arc,
    // Not an R12 entity: AC1009 has no ELLIPSE and R12 drew them as polylines.
    // Held exactly here and degraded to a polyline on DXF write, so the
    // database keeps geometry the interchange format cannot name. See
    // CLAUDE.md on divergences.
    Ellipse,
    // Also not an R12 entity, and a larger divergence than Ellipse: AC1009 has
    // no SPLINE, and R12's own "spline" was PEDIT fitting a quadratic or cubic
    // through polyline vertices rather than a curve in its own right. This is a
    // NURBS curve -- degree, control points, knots, optional weights -- and it
    // degrades to a polyline on DXF write like Ellipse does.
    Spline,
    Polyline,
    Text,
    // Also not an R12 entity. AC1009 has TEXT only -- one line per entity, no
    // wrapping, no inline formatting -- so a modern drawing's annotation, which
    // is nearly all MTEXT, had nowhere to go but Proxy and rendered as nothing.
    // Held exactly here and degraded to a run of TEXT records on DXF write, the
    // same bargain Ellipse and Spline take. See entities.hpp.
    MText,
    Point,
    Solid,
    Face3d,
    // A placed copy of a block definition. MINSERT is the same entity with a
    // row and column count, which is how R12 spells it too.
    Insert,
    // A measurement the drawing carries. Non-associative, as R12's are, and it
    // generates its own line work rather than storing any -- see entities.hpp.
    Dimension,
    // Anything read from DXF that has no class here. Holds the groups it was
    // read from and writes them back unchanged, so opening and saving a drawing
    // cannot quietly destroy what this program does not understand.
    Proxy,
    // R12 kinds still to come: PFACE meshes and the surface entities.
};

const char* entity_type_name(EntityType t);

// Properties common to every R12 entity. `normal` is the extrusion direction,
// DXF group 210 -- the axis of the entity coordinate system.
struct EntityProps {
    LayerId layer{kLayerZero};
    LinetypeId linetype{kLinetypeContinuous};
    std::int16_t color{kColorByLayer};
    double thickness{0.0};
    Vec3 normal{0.0, 0.0, 1.0};
};

class Entity {
public:
    explicit Entity(EntityType type) : type_(type) {}
    virtual ~Entity() = default;

    Entity(const Entity&) = default;
    Entity& operator=(const Entity&) = delete;

    EntityType type() const { return type_; }
    const char* type_name() const { return entity_type_name(type_); }

    Handle handle() const { return handle_; }

    EntityProps& props() { return props_; }
    const EntityProps& props() const { return props_; }

    // --- the vtable ---------------------------------------------------------

    virtual std::unique_ptr<Entity> clone() const = 0;

    // The important one. MOVE, COPY, SCALE, MIRROR, ARRAY, ROTATE, ROTATE3D,
    // ALIGN and block insertion all route through this single method.
    virtual void transform(const Mat4& m) = 0;

    virtual BBox bbox() const = 0;

    // Appends this entity's static snap points (END/MID/CEN/QUA/...).
    virtual void osnap_points(std::vector<OsnapPoint>& out) const = 0;

    // Appends this entity's grips: its defining points, each carrying what
    // dragging it means. Deliberately separate from osnap_points() -- the
    // coordinates frequently coincide and the semantics do not.
    virtual void grips(std::vector<Grip>& out) const = 0;

    // Moves the defining points named by `indices` (grip indices, in any order)
    // by `delta`, leaving the rest where they are. This is what transform()
    // cannot express, and it is what both STRETCH and grip dragging are built
    // from.
    //
    // Indices that name no grip are ignored rather than raising: STRETCH hands
    // over whatever fell inside a window, and a caller working from a stale
    // grip list should get nothing rather than an error.
    virtual void stretch(const Vec3& delta, const GripIndex* indices, std::size_t count) = 0;

    virtual void dxf_write(DxfWriter& w) const = 0;

    // Emits this entity as world-space wireframe. Styling is not this method's
    // business -- the scene walker calls Renderer::begin_entity() first. The
    // core stays headless: Renderer is abstract and knows nothing of Qt.
    virtual void draw(const DrawContext& ctx, Renderer& r) const = 0;

protected:
    // Copies props but deliberately not the handle: a clone is a new entity and
    // the database assigns its identity on insertion.
    void copy_common_to(Entity& dst) const { dst.props_ = props_; }

private:
    friend class Database;

    EntityType type_;
    Handle handle_{kNullHandle};
    EntityProps props_{};
};

using EntityPtr = std::unique_ptr<Entity>;

}  // namespace ncad
