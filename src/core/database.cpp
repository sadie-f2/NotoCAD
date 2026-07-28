// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/database.hpp"

#include <algorithm>

namespace noto {

Database::Database() {
    sysvars_.set_journal(&journal_);
    add_linetype("CONTINUOUS", "Solid line", {});
    add_layer("0", 7, kLinetypeContinuous);

    // The tables and defaults above are what a new drawing *is*, not something
    // done to it. Undoing back past them is not meaningful.
    journal_.clear();
}

Handle Database::add(EntityPtr entity) {
    if (!entity) return kNullHandle;

    const Handle h = next_handle_++;
    entity->handle_ = h;
    entities_.emplace(h, std::move(entity));
    order_.push_back(h);
    journal_.record_add(*entities_[h], order_.size() - 1);
    return h;
}

bool Database::restore(Handle h, EntityPtr entity, std::size_t order_index) {
    if (!entity || h == kNullHandle) return false;
    if (entities_.find(h) != entities_.end()) return false;

    entity->handle_ = h;
    entities_.emplace(h, std::move(entity));

    // Clamped rather than rejected: the order may legitimately be shorter now
    // than when the entity was removed, if things after it went too.
    const std::size_t at = order_index < order_.size() ? order_index : order_.size();
    order_.insert(order_.begin() + static_cast<std::ptrdiff_t>(at), h);

    // A handle handed back out must never be reissued to something else.
    if (h >= next_handle_) next_handle_ = h + 1;
    return true;
}

Entity* Database::get(Handle h) {
    const auto it = entities_.find(h);
    return (it == entities_.end()) ? nullptr : it->second.get();
}

const Entity* Database::get(Handle h) const {
    const auto it = entities_.find(h);
    return (it == entities_.end()) ? nullptr : it->second.get();
}

bool Database::replace(Handle h, EntityPtr entity) {
    if (!entity) return false;
    const auto it = entities_.find(h);
    if (it == entities_.end()) return false;

    // The handle carries over, so order_ needs no update and any ename held by
    // AutoLISP stays valid.
    entity->handle_ = h;
    journal_.record_modify(*it->second, *entity);
    it->second = std::move(entity);
    return true;
}

Handle Database::next(Handle h) const {
    // Linear, matching erase(). Callers walking the whole drawing should iterate
    // order() directly rather than calling this in a loop.
    for (std::size_t i = 0; i < order_.size(); ++i) {
        if (order_[i] == h) {
            return (i + 1 < order_.size()) ? order_[i + 1] : kNullHandle;
        }
    }
    return kNullHandle;
}

bool Database::erase(Handle h) {
    const auto it = entities_.find(h);
    if (it == entities_.end()) return false;

    const auto pos = std::find(order_.begin(), order_.end(), h);
    const std::size_t index = static_cast<std::size_t>(pos - order_.begin());
    journal_.record_erase(*it->second, index);

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
