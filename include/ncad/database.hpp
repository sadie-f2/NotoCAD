// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The drawing database: entity storage plus the symbol tables.
//
// Handles are stable for the lifetime of an entity and are never reused. This is
// load-bearing, not incidental: AutoLISP holds entity names (`ename`) across
// arbitrary amounts of user activity, and R12 DXF writes handles to disk.
#pragma once

#include "ncad/blocks.hpp"
#include "ncad/entity.hpp"
#include "ncad/sysvar.hpp"
#include "ncad/tables.hpp"
#include "ncad/undo.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ncad {

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

    // --- coordinate systems -------------------------------------------------

    // Named UCSs, the table half. DXF keeps these in the TABLES section beside
    // LAYER and LTYPE, and they get the same treatment here for the same
    // reasons: journalled, and reached through the database commands already
    // hold.
    UcsId add_ucs(const std::string& name, const Ucs& value);
    UcsId find_ucs(const std::string& name) const;
    const UcsDef& ucs(UcsId id) const { return ucs_table_[id]; }
    const std::vector<UcsDef>& ucs_table() const { return ucs_table_; }
    bool set_ucs(UcsId id, const UcsDef& value);
    bool erase_ucs(UcsId id);
    void pop_ucs();
    void restore_ucs(UcsId id, const UcsDef& value);

    // The CURRENT UCS, the header half. Carried in system variables because
    // that is what it is -- DXF stores it as $UCSORG/$UCSXDIR/$UCSYDIR, exactly
    // as it stores the current layer as $CLAYER -- so it inherits journalling,
    // getvar, and header round-tripping from machinery that already exists.
    //
    // Always returned orthonormalised: the variables are reachable from LISP,
    // and a frame whose axes are not perpendicular would put geometry somewhere
    // no transform could undo.
    Ucs current_ucs() const;

    // Sets it, and the name and $WORLDUCS with it. An unnamed system -- one
    // built by UCS Origin or 3point rather than restored from the table --
    // takes an empty name, which is what R12 reports as "*NO NAME*".
    void set_current_ucs(const Ucs& value, const std::string& name = {});

    // The construction plane's normal: the current UCS's Z axis.
    //
    // The single seam CLAUDE.md named. Every command that needed a plane went
    // through a free function returning world Z; they go through here now, and
    // nothing else had to change.
    Vec3 construction_normal() const { return current_ucs().zdir(); }

    // --- blocks -------------------------------------------------------------

    // Takes ownership of the definition and returns its id. A name that already
    // exists is REDEFINED rather than duplicated, which is R12's behaviour and
    // is why every insertion updates: they hold the definition's address, and
    // the address does not change.
    BlockId add_block(BlockDef def);

    BlockId find_block(const std::string& name) const;

    // Stable for the lifetime of the database. Null for an invalid id.
    const BlockDef* block(BlockId id) const;

    std::size_t block_count() const { return blocks_.size(); }
    const std::vector<std::unique_ptr<BlockDef>>& blocks() const { return blocks_; }

    // Undo's way back, matching pop_layer and restore_layer above.
    void pop_block();
    void restore_block(BlockId id, BlockDef value);

    // Whether any entity in the drawing, or in another block, inserts this one.
    // BLOCK refuses to redefine a block in terms of itself, and a future PURGE
    // needs the same question answered.
    bool block_is_referenced(BlockId id) const;

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

    // --- current entity properties ------------------------------------------

    // The layer CLAYER names, creating nothing: an unknown name gives layer 0
    // rather than quietly adding a layer as a side effect of drawing.
    LayerId current_layer() const;

    // Everything a newly drawn entity takes from the current settings. Commands
    // call this rather than each remembering which sysvars apply, so a new
    // command cannot forget one.
    EntityProps current_props() const;

    Handle peek_next_handle() const { return next_handle_; }

private:
    std::unordered_map<Handle, EntityPtr> entities_;
    std::vector<Handle> order_;
    Handle next_handle_{1};

    std::vector<Layer> layers_;
    std::vector<Linetype> linetypes_;
    std::vector<UcsDef> ucs_table_;
    // unique_ptr rather than by value: an Insert holds the definition's
    // address, so growing this vector must not move what it points at.
    std::vector<std::unique_ptr<BlockDef>> blocks_;

    // Definitions popped by undo, kept alive rather than freed.
    //
    // An Insert holds a raw `const BlockDef*`, and the undo stack holds Insert
    // CLONES that still point at a definition undo has removed -- so freeing it
    // is a use-after-free that redo then walks straight into. Popping moves the
    // allocation here instead, and restoring takes it back, which is what makes
    // a redone insert point at a live object at the SAME address it had before.
    //
    // Bounded by the number of block definitions undone in one session, and
    // dropped when the drawing is cleared. Paying a stale BlockDef's worth of
    // memory beats holding the invariant with a comment alone.
    std::vector<std::unique_ptr<BlockDef>> retired_blocks_;
    UndoJournal journal_;
    Sysvars sysvars_;
};

}  // namespace ncad
