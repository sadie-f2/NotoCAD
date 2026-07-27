// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The drawing database: entity storage plus the symbol tables.
//
// Handles are stable for the lifetime of an entity and are never reused. This is
// load-bearing, not incidental: AutoLISP holds entity names (`ename`) across
// arbitrary amounts of user activity, and R12 DXF writes handles to disk.
#pragma once

#include "noto/entity.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace noto {

inline constexpr LayerId kInvalidLayer = 0xFFFF;
inline constexpr LinetypeId kInvalidLinetype = 0xFFFF;

struct Layer {
    std::string name;
    std::int16_t color{7};  // negative means the layer is off, per R12
    LinetypeId linetype{kLinetypeContinuous};
    bool frozen{false};
    bool locked{false};
};

struct Linetype {
    std::string name;
    std::string description;
    std::vector<double> pattern;  // positive = dash, negative = gap, 0 = dot
};

class Database {
public:
    // A new database always contains layer "0" and linetype CONTINUOUS, which
    // R12 requires to exist.
    Database();

    // --- entities -----------------------------------------------------------

    // Takes ownership and assigns a fresh handle, which it returns.
    Handle add(EntityPtr entity);

    Entity* get(Handle h);
    const Entity* get(Handle h) const;

    bool erase(Handle h);
    void clear();

    std::size_t size() const { return entities_.size(); }
    bool empty() const { return entities_.empty(); }

    // Handles in insertion order. Iterating this rather than the hash map is
    // what makes DXF output byte-for-byte deterministic.
    const std::vector<Handle>& order() const { return order_; }

    BBox extents() const;

    // --- symbol tables ------------------------------------------------------

    LayerId add_layer(const std::string& name, std::int16_t color = 7,
                      LinetypeId linetype = kLinetypeContinuous);
    LayerId find_layer(const std::string& name) const;
    const Layer& layer(LayerId id) const { return layers_[id]; }
    Layer& layer(LayerId id) { return layers_[id]; }
    const std::vector<Layer>& layers() const { return layers_; }

    LinetypeId add_linetype(const std::string& name, const std::string& description,
                            std::vector<double> pattern);
    LinetypeId find_linetype(const std::string& name) const;
    const Linetype& linetype(LinetypeId id) const { return linetypes_[id]; }
    const std::vector<Linetype>& linetypes() const { return linetypes_; }

    Handle peek_next_handle() const { return next_handle_; }

private:
    std::unordered_map<Handle, EntityPtr> entities_;
    std::vector<Handle> order_;
    Handle next_handle_{1};

    std::vector<Layer> layers_;
    std::vector<Linetype> linetypes_;
};

}  // namespace noto
