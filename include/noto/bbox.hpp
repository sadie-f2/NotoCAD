// Axis-aligned bounding box in world coordinates.
#pragma once

#include "noto/vec3.hpp"

#include <algorithm>
#include <limits>

namespace noto {

struct BBox {
    static constexpr double kInf = std::numeric_limits<double>::infinity();

    Vec3 min{kInf, kInf, kInf};
    Vec3 max{-kInf, -kInf, -kInf};

    // Default-constructed boxes are empty; expanding one is always safe.
    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

    void expand(const Vec3& p) {
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
        max.z = std::max(max.z, p.z);
    }

    void expand(const BBox& b) {
        if (!b.valid()) return;
        expand(b.min);
        expand(b.max);
    }

    Vec3 center() const { return (min + max) * 0.5; }
    Vec3 size() const { return valid() ? max - min : Vec3{}; }
};

}  // namespace noto
