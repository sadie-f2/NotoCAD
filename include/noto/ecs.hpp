// Entity Coordinate System support: the Arbitrary Axis Algorithm.
//
// R12 stores CIRCLE, ARC, 2D POLYLINE, TEXT, SOLID and friends as 2D coordinates
// in their own coordinate system, plus an extrusion direction (DXF group 210).
// Converting between world and entity space requires deriving a repeatable basis
// from nothing but that normal -- that is what this algorithm does. Without it,
// any such entity that is not parallel to the world XY plane serialises wrong.
#pragma once

#include "noto/mat4.hpp"
#include "noto/vec3.hpp"

namespace noto {

// The 1/64 threshold is specified by the DXF format itself, not a tolerance we
// chose. It must not be replaced with kEps: the exact value is what makes the
// basis agree with other CAD applications reading the same file.
inline constexpr double kArbitraryAxisThreshold = 1.0 / 64.0;

// An orthonormal right-handed basis. `az` is the entity's extrusion direction.
struct Basis {
    Vec3 ax, ay, az;
};

// The Arbitrary Axis Algorithm. `normal` need not be unit length, but must be
// non-degenerate; a zero normal yields the world basis.
Basis arbitrary_axis(const Vec3& normal);

// World -> ECS, for an entity whose extrusion direction is `normal`.
Mat4 world_to_ecs(const Vec3& normal);

// ECS -> world. The inverse of world_to_ecs; computed directly by transpose
// rather than inversion, since the basis is orthonormal.
Mat4 ecs_to_world(const Vec3& normal);

}  // namespace noto
