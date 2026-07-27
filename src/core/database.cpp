// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/database.hpp"

#include <algorithm>

namespace noto {

Database::Database() {
    add_linetype("CONTINUOUS", "Solid line", {});
    add_layer("0", 7, kLinetypeContinuous);
}

Handle Database::add(EntityPtr entity) {
    if (!entity) return kNullHandle;

    const Handle h = next_handle_++;
    entity->handle_ = h;
    entities_.emplace(h, std::move(entity));
    order_.push_back(h);
    return h;
}

Entity* Database::get(Handle h) {
    const auto it = entities_.find(h);
    return (it == entities_.end()) ? nullptr : it->second.get();
}

const Entity* Database::get(Handle h) const {
    const auto it = entities_.find(h);
    return (it == entities_.end()) ? nullptr : it->second.get();
}

bool Database::erase(Handle h) {
    const auto it = entities_.find(h);
    if (it == entities_.end()) return false;

    entities_.erase(it);
    // Linear, which is fine at present sizes. If bulk deletion ever shows up in
    // a profile, switch to tombstones and compact lazily.
    order_.erase(std::remove(order_.begin(), order_.end(), h), order_.end());
    return true;
}

void Database::clear() {
    entities_.clear();
    order_.clear();
    // next_handle_ deliberately not reset: handles are never reused.
}

BBox Database::extents() const {
    BBox box;
    for (const Handle h : order_) {
        const auto it = entities_.find(h);
        if (it != entities_.end()) box.expand(it->second->bbox());
    }
    return box;
}

LayerId Database::add_layer(const std::string& name, std::int16_t color, LinetypeId linetype) {
    const LayerId existing = find_layer(name);
    if (existing != kInvalidLayer) return existing;

    layers_.push_back(Layer{name, color, linetype, false, false});
    return static_cast<LayerId>(layers_.size() - 1);
}

LayerId Database::find_layer(const std::string& name) const {
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        if (layers_[i].name == name) return static_cast<LayerId>(i);
    }
    return kInvalidLayer;
}

LinetypeId Database::add_linetype(const std::string& name, const std::string& description,
                                  std::vector<double> pattern) {
    const LinetypeId existing = find_linetype(name);
    if (existing != kInvalidLinetype) return existing;

    linetypes_.push_back(Linetype{name, description, std::move(pattern)});
    return static_cast<LinetypeId>(linetypes_.size() - 1);
}

LinetypeId Database::find_linetype(const std::string& name) const {
    for (std::size_t i = 0; i < linetypes_.size(); ++i) {
        if (linetypes_[i].name == name) return static_cast<LinetypeId>(i);
    }
    return kInvalidLinetype;
}

}  // namespace noto
