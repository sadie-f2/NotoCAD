// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The drawing database: entity storage plus the symbol tables.
//
// Handles are stable for the lifetime of an entity and are never reused. This is
// load-bearing, not incidental: AutoLISP holds entity names (`ename`) across
// arbitrary amounts of user activity, and R12 DXF writes handles to disk.
#pragma once

#include "noto/entity.hpp"
#include "noto/sysvar.hpp"
#include "noto/tables.hpp"
#include "noto/undo.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace noto {

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

    // Swaps in a new entity under an existing handle, keeping its position in
    // the drawing order. AutoLISP's entmod needs this: the ename it holds must
    // still refer to the same entity after the modification.
    bool replace(Handle h, EntityPtr entity);

    bool erase(Handle h);
    void clear();

    // Re-inserts an entity under a handle it previously had, at the drawing
    // order position it previously occupied. This exists for undo and for
    // nothing else: ordinary insertion is add(), which allocates a fresh handle.
    // Restoring to the end of the order instead would silently change what
    // draws on top of what, and change the DXF byte for byte.
    bool restore(Handle h, EntityPtr entity, std::size_t order_index);

    // Undo's way back. Not for general use: adds append, so removing one is
    // only ever valid as the reversal of the add that put it there.
    void pop_layer();
    void pop_linetype();
    void restore_layer(LayerId id, const Layer& value);
    void restore_linetype(LinetypeId id, const Linetype& value);

    // Drawing-order traversal, for AutoLISP's entnext and entlast.
    Handle first() const { return order_.empty() ? kNullHandle : order_.front(); }
    Handle last() const { return order_.empty() ? kNullHandle : order_.back(); }

    // kNullHandle when h is the last entity or is not in the database.
    Handle next(Handle h) const;

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

    // Writes go through here rather than through a mutable reference, so that
    // every change is journalled. Handing out a Layer& would route edits past
    // undo, which is exactly how undo grows holes -- the same reasoning that
    // put the journal inside Sysvars.
    bool set_layer(LayerId id, const Layer& value);
    bool set_layer_color(LayerId id, std::int16_t color);
    bool set_layer_frozen(LayerId id, bool frozen);
    bool set_layer_locked(LayerId id, bool locked);
    bool set_layer_linetype(LayerId id, LinetypeId linetype);
    const std::vector<Layer>& layers() const { return layers_; }

    LinetypeId add_linetype(const std::string& name, const std::string& description,
                            std::vector<double> pattern);
    LinetypeId find_linetype(const std::string& name) const;
    const Linetype& linetype(LinetypeId id) const { return linetypes_[id]; }
    bool set_linetype(LinetypeId id, const Linetype& value);
    const std::vector<Linetype>& linetypes() const { return linetypes_; }

    // --- system variables ---------------------------------------------------

    // They live here so commands reach them through the context they already
    // have, and CommandContext stays {Database&}. Not everything in the table is
    // drawing state -- PICKBOX and APERTURE follow the installation -- which is
    // what SysvarDef::save_in_drawing and reset_drawing_vars() are for.
    Sysvars& sysvars() { return sysvars_; }
    const Sysvars& sysvars() const { return sysvars_; }

    // --- undo ---------------------------------------------------------------

    // Every mutation above is journalled here. It lives on the database for the
    // same reason the sysvars do: commands reach it through the context they
    // already hold, and nothing can change the drawing behind its back.
    UndoJournal& journal() { return journal_; }
    const UndoJournal& journal() const { return journal_; }

    Handle peek_next_handle() const { return next_handle_; }

private:
    std::unordered_map<Handle, EntityPtr> entities_;
    std::vector<Handle> order_;
    Handle next_handle_{1};

    std::vector<Layer> layers_;
    std::vector<Linetype> linetypes_;
    UndoJournal journal_;
    Sysvars sysvars_;
};

}  // namespace noto
