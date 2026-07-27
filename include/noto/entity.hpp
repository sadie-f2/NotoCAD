// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Entity base class and the dispatch surface every entity kind implements.
//
// Deliberately a single level of inheritance: this base, and concrete entities
// directly under it. No hierarchies, no RTTI -- `type()` returns an enum, which
// DXF needs anyway.
#pragma once

#include "noto/bbox.hpp"
#include "noto/mat4.hpp"
#include "noto/osnap.hpp"
#include "noto/vec3.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace noto {

class DxfWriter;

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
    Polyline,
    Text,
    // R12 kinds still to come: SOLID, POINT, INSERT, 3DFACE, PFACE meshes,
    // and the surface entities.
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

    virtual void dxf_write(DxfWriter& w) const = 0;

    // draw() joins this list when the GUI shell lands; the core stays headless
    // until then.

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

}  // namespace noto
