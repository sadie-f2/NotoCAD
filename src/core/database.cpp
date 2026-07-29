// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/database.hpp"

#include "noto/entities.hpp"

#include <algorithm>
#include <cmath>

namespace noto {

double Linetype::pattern_length() const {
    double total = 0.0;
    for (const double d : pattern) total += std::abs(d);
    return total;
}


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

bool Database::set_layer(LayerId id, const Layer& value) {
    if (id >= layers_.size()) return false;
    const Layer before = layers_[id];
    layers_[id] = value;
    journal_.record_layer_modify(id, before, layers_[id]);
    return true;
}

bool Database::set_layer_color(LayerId id, std::int16_t color) {
    if (id >= layers_.size()) return false;
    Layer next = layers_[id];
    next.color = color;
    return set_layer(id, next);
}

bool Database::set_layer_frozen(LayerId id, bool frozen) {
    if (id >= layers_.size()) return false;
    Layer next = layers_[id];
    next.frozen = frozen;
    return set_layer(id, next);
}

bool Database::set_layer_locked(LayerId id, bool locked) {
    if (id >= layers_.size()) return false;
    Layer next = layers_[id];
    next.locked = locked;
    return set_layer(id, next);
}

bool Database::set_layer_linetype(LayerId id, LinetypeId linetype) {
    if (id >= layers_.size()) return false;
    Layer next = layers_[id];
    next.linetype = linetype;
    return set_layer(id, next);
}

bool Database::set_linetype(LinetypeId id, const Linetype& value) {
    if (id >= linetypes_.size()) return false;
    const Linetype before = linetypes_[id];
    linetypes_[id] = value;
    journal_.record_linetype_modify(id, before, linetypes_[id]);
    return true;
}

void Database::pop_layer() {
    if (layers_.size() > 1) layers_.pop_back();  // layer 0 always exists
}

void Database::pop_linetype() {
    if (linetypes_.size() > 1) linetypes_.pop_back();  // CONTINUOUS always exists
}

void Database::restore_layer(LayerId id, const Layer& value) {
    if (id < layers_.size()) {
        layers_[id] = value;
        return;
    }
    // Redoing an add: it was the last, so putting it back is appending.
    if (id == layers_.size()) layers_.push_back(value);
}

void Database::restore_linetype(LinetypeId id, const Linetype& value) {
    if (id < linetypes_.size()) {
        linetypes_[id] = value;
        return;
    }
    if (id == linetypes_.size()) linetypes_.push_back(value);
}

LayerId Database::current_layer() const {
    const LayerId id = find_layer(sysvars_.get_string(Sysvar::CLayer));
    return id == kInvalidLayer ? kLayerZero : id;
}

EntityProps Database::current_props() const {
    EntityProps p;
    p.layer = current_layer();
    p.color = static_cast<std::int16_t>(sysvars_.get_int(Sysvar::CEColor));

    const std::string& ltype = sysvars_.get_string(Sysvar::CELtype);
    if (ltype != "BYLAYER" && ltype != "BYBLOCK") {
        const LinetypeId id = find_linetype(ltype);
        if (id != kInvalidLinetype) p.linetype = id;
    }
    return p;
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
    const LayerId id = static_cast<LayerId>(layers_.size() - 1);
    journal_.record_layer_add(id, layers_.back());
    return id;
}

LayerId Database::find_layer(const std::string& name) const {
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        if (layers_[i].name == name) return static_cast<LayerId>(i);
    }
    return kInvalidLayer;
}

UcsId Database::add_ucs(const std::string& name, const Ucs& value) {
    const UcsId existing = find_ucs(name);
    if (existing != kInvalidUcs) {
        // UCS Save onto an existing name redefines it, as R12 does.
        UcsDef next{name, value.normalized()};
        set_ucs(existing, next);
        return existing;
    }

    ucs_table_.push_back(UcsDef{name, value.normalized()});
    const UcsId id = static_cast<UcsId>(ucs_table_.size() - 1);
    journal_.record_ucs_add(id, ucs_table_.back());
    return id;
}

UcsId Database::find_ucs(const std::string& name) const {
    for (std::size_t i = 0; i < ucs_table_.size(); ++i) {
        if (ucs_table_[i].name == name) return static_cast<UcsId>(i);
    }
    return kInvalidUcs;
}

bool Database::set_ucs(UcsId id, const UcsDef& value) {
    if (id >= ucs_table_.size()) return false;
    const UcsDef before = ucs_table_[id];
    ucs_table_[id] = value;
    journal_.record_ucs_modify(id, before, ucs_table_[id]);
    return true;
}

bool Database::erase_ucs(UcsId id) {
    if (id >= ucs_table_.size()) return false;
    // Erasing from the middle would shift every later id, and the journal
    // records ids. UCS Delete is rare enough that emptying the name is an
    // honest answer: the entry stops being findable and nothing else moves.
    UcsDef cleared = ucs_table_[id];
    const std::string erased = cleared.name;
    cleared.name.clear();
    if (!set_ucs(id, cleared)) return false;

    // The current UCS is five system variables, not a reference into this
    // table, so deleting the entry correctly leaves you working where you were.
    // But UCSNAME would go on naming a system the table no longer holds, and
    // then everything that reads it lies: UCS ? prints "current: FOO" beside a
    // listing FOO has just vanished from, (getvar "UCSNAME") returns it, and
    // DXFOUT writes $UCSNAME into a file whose UCS table cannot supply it --
    // which is a malformed drawing, not merely a confusing one.
    //
    // Only the NAME is cleared. Where you are working is not a delete's
    // business, and sending the user back to world would lose the frame they
    // are drawing in.
    if (!erased.empty() && sysvars_.get_string(Sysvar::UcsName) == erased) {
        sysvars_.set_owned(Sysvar::UcsName, SysvarValue::of_string(std::string{}));
    }
    return true;
}

void Database::pop_ucs() {
    if (!ucs_table_.empty()) ucs_table_.pop_back();
}

void Database::restore_ucs(UcsId id, const UcsDef& value) {
    if (id < ucs_table_.size()) {
        ucs_table_[id] = value;
        return;
    }
    if (id == ucs_table_.size()) ucs_table_.push_back(value);
}

Ucs Database::current_ucs() const {
    Ucs u;
    u.origin = sysvars_.get_point(Sysvar::UcsOrg);
    u.xdir = sysvars_.get_point(Sysvar::UcsXDir);
    u.ydir = sysvars_.get_point(Sysvar::UcsYDir);
    // Orthonormalised on the way out rather than trusted on the way in: these
    // are system variables, and a frame with non-perpendicular axes would put
    // geometry somewhere no transform could undo.
    return u.normalized();
}

void Database::set_current_ucs(const Ucs& value, const std::string& name) {
    const Ucs n = value.normalized();
    sysvars_.set_owned(Sysvar::UcsOrg, SysvarValue::of_point(n.origin));
    sysvars_.set_owned(Sysvar::UcsXDir, SysvarValue::of_point(n.xdir));
    sysvars_.set_owned(Sysvar::UcsYDir, SysvarValue::of_point(n.ydir));
    sysvars_.set_owned(Sysvar::UcsName, SysvarValue::of_string(name));
    sysvars_.set_owned(Sysvar::WorldUcs, SysvarValue::of_int(n.is_world() ? 1 : 0));
}

BlockId Database::add_block(BlockDef def) {
    const BlockId existing = find_block(def.name);
    if (existing != kInvalidBlock) {
        // Redefinition, R12-style: the definition is rewritten in place so that
        // every insertion of it updates. Replacing the unique_ptr instead would
        // leave every Insert holding a dangling address.
        BlockDef before = blocks_[existing]->clone();
        *blocks_[existing] = std::move(def);
        journal_.record_block_modify(existing, before, *blocks_[existing]);
        return existing;
    }

    blocks_.push_back(std::make_unique<BlockDef>(std::move(def)));
    const BlockId id = static_cast<BlockId>(blocks_.size() - 1);
    journal_.record_block_add(id, *blocks_.back());
    return id;
}

BlockId Database::find_block(const std::string& name) const {
    for (std::size_t i = 0; i < blocks_.size(); ++i) {
        if (blocks_[i]->name == name) return static_cast<BlockId>(i);
    }
    return kInvalidBlock;
}

const BlockDef* Database::block(BlockId id) const {
    return id < blocks_.size() ? blocks_[id].get() : nullptr;
}

void Database::pop_block() {
    if (!blocks_.empty()) blocks_.pop_back();
}

void Database::restore_block(BlockId id, BlockDef value) {
    if (id < blocks_.size()) {
        *blocks_[id] = std::move(value);
        return;
    }
    // Redo of an add: the definition has to come back at the same id, which is
    // the end of the vector because adds append.
    if (id == blocks_.size()) {
        blocks_.push_back(std::make_unique<BlockDef>(std::move(value)));
    }
}

bool Database::block_is_referenced(BlockId id) const {
    const BlockDef* target = block(id);
    if (!target) return false;

    auto references = [&](const std::vector<EntityPtr>& list, auto&& self, int depth) -> bool {
        if (depth >= kMaxBlockDepth) return false;
        for (const EntityPtr& e : list) {
            if (!e || e->type() != EntityType::Insert) continue;
            const Insert& ins = static_cast<const Insert&>(*e);
            if (ins.definition() == target) return true;
            if (ins.definition() && self(ins.definition()->entities, self, depth + 1)) return true;
        }
        return false;
    };

    // The drawing itself.
    for (Handle h : order_) {
        auto it = entities_.find(h);
        if (it == entities_.end() || !it->second) continue;
        if (it->second->type() != EntityType::Insert) continue;
        const Insert& ins = static_cast<const Insert&>(*it->second);
        if (ins.definition() == target) return true;
        if (ins.definition() && references(ins.definition()->entities, references, 1)) return true;
    }

    // And every other definition, since a block may insert another.
    for (const std::unique_ptr<BlockDef>& def : blocks_) {
        if (def.get() == target) continue;
        if (references(def->entities, references, 0)) return true;
    }
    return false;
}

LinetypeId Database::add_linetype(const std::string& name, const std::string& description,
                                  std::vector<double> pattern) {
    const LinetypeId existing = find_linetype(name);
    if (existing != kInvalidLinetype) return existing;

    linetypes_.push_back(Linetype{name, description, std::move(pattern)});
    const LinetypeId id = static_cast<LinetypeId>(linetypes_.size() - 1);
    journal_.record_linetype_add(id, linetypes_.back());
    return id;
}

LinetypeId Database::find_linetype(const std::string& name) const {
    for (std::size_t i = 0; i < linetypes_.size(); ++i) {
        if (linetypes_[i].name == name) return static_cast<LinetypeId>(i);
    }
    return kInvalidLinetype;
}

}  // namespace noto
