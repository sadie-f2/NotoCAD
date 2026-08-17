// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Undo and redo: unlimited, back to the start of the session.
//
// Not a depth-limited ring. R12's UNDO reaches the beginning of the editing
// session, and a limit is the kind of thing discovered only when you need the
// step that is no longer there.
//
// WHAT IS RECORDED. Before and after states, not inverse operations. Every
// entity can clone() and handles are stable and never reused, so a change is
// just "handle H used to be this, and is now that". An inverse-operation
// journal would be more compact and would fail differently: a wrong inverse
// corrupts the drawing silently, where a wrong snapshot merely costs memory.
// Undo and redo are then the same walk in opposite directions, which is most of
// why this stays small.
//
// System variables are journalled too, not only entities. A command that sets
// OSMODE and is then undone must give it back, and anything mutable that is not
// recorded becomes a hole in undo -- holes being the sort of thing found at the
// worst possible moment.
//
// GROUPING. One UNDO reverses one command, not one primitive: LINE with four
// segments is one step, as R12 has it. CommandEngine opens a group when a
// command begins and closes it when it ends. Groups nest by depth, so a LISP
// function calling (command "LINE" ...) three times collapses into one step
// rather than three -- which is also the hook a batch mode will use when
// entmake starts building meshes a face at a time.
//
// A mutation arriving with no group open becomes its own single-change group,
// which is what a bare entmake at the command line should be.
#pragma once

#include "ncad/blocks.hpp"
#include "ncad/entity.hpp"
#include "ncad/sysvar.hpp"
#include "ncad/tables.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace ncad {

class Database;

enum class ChangeKind : std::uint8_t {
    AddEntity,      // `after` holds a copy of what was added
    EraseEntity,    // `before` holds what was removed, and where it sat
    ModifyEntity,   // both, same handle
    SetSysvar,
    AddLayer,
    ModifyLayer,
    AddLinetype,
    ModifyLinetype,
    AddUcs,
    ModifyUcs,
    AddBlock,
    // A block redefined in place. Every insertion of it changes at once, since
    // they all hold the definition's address -- which is exactly why this has
    // to be journalled as a table change and not as a pile of entity edits.
    ModifyBlock,
};

// Table before-and-after states, held behind a pointer rather than inline.
//
// A Layer carries a string and a Linetype carries two plus a vector, so putting
// them in every Change would add a couple of hundred bytes to each of the
// twenty thousand records a mesh build produces. Table changes are rare and
// entity changes are not, so the rare case pays the allocation.
struct TableChange {
    LayerId layer_id{0};
    Layer layer_before;
    Layer layer_after;

    LinetypeId linetype_id{0};
    Linetype linetype_before;
    Linetype linetype_after;

    UcsId ucs_id{0};
    UcsDef ucs_before;
    UcsDef ucs_after;

    BlockId block_id{0};
    BlockDef block_before;
    BlockDef block_after;
};

// Move-only: it owns entity copies.
struct Change {
    ChangeKind kind{ChangeKind::AddEntity};

    Handle entity{kNullHandle};
    EntityPtr before;
    EntityPtr after;

    // Where in the drawing order the entity sat. Restoring an erased entity to
    // the end of the order would silently reshuffle what draws on top of what,
    // and drawing order is also what makes DXF output deterministic.
    std::size_t order_index{0};

    Sysvar sysvar{Sysvar::OsMode};
    SysvarValue sysvar_before;
    SysvarValue sysvar_after;

    // Null except for the table kinds. See TableChange for why.
    std::unique_ptr<TableChange> table;
};

struct UndoGroup {
    std::string name;  // the command that made it, for "UNDO LINE" style echo
    std::vector<Change> changes;

    // Unique and never reused. This is what makes "is the drawing saved?"
    // answerable -- see UndoJournal::dirty(), where the reason a DEPTH cannot
    // do the job is spelled out.
    std::uint64_t serial{0};
};

class UndoJournal {
public:
    UndoJournal() = default;
    UndoJournal(const UndoJournal&) = delete;
    UndoJournal& operator=(const UndoJournal&) = delete;

    // Nested calls raise a depth count; only the outermost close commits the
    // group. The name of the outermost one is the one kept.
    void begin_group(std::string name);
    void end_group();
    bool in_group() const { return depth_ > 0; }

    // Recording. Called by Database, and ignored while replaying so that undo
    // does not journal its own work back onto the stack.
    void record_add(const Entity& added, std::size_t order_index);
    void record_erase(const Entity& removed, std::size_t order_index);
    void record_modify(const Entity& before, const Entity& after);
    void record_sysvar(Sysvar id, const SysvarValue& before, const SysvarValue& after);

    void record_layer_add(LayerId id, const Layer& added);
    void record_layer_modify(LayerId id, const Layer& before, const Layer& after);
    void record_linetype_add(LinetypeId id, const Linetype& added);
    void record_linetype_modify(LinetypeId id, const Linetype& before, const Linetype& after);
    void record_ucs_add(UcsId id, const UcsDef& added);
    void record_ucs_modify(UcsId id, const UcsDef& before, const UcsDef& after);
    void record_block_add(BlockId id, const BlockDef& added);
    void record_block_modify(BlockId id, const BlockDef& before, const BlockDef& after);

    bool replaying() const { return replaying_; }

    // Stops the journal recording for as long as it lives.
    //
    // For loading a drawing, which is not an edit. Without it, read_dxf_text
    // cleared the journal at the START -- resetting depth_ to zero underneath
    // the OPEN command's open group -- so every entity read afterwards became
    // its own UndoGroup holding a full Entity::clone(). All of it freed again
    // by the clear at the end, so not a leak: a transient doubling of the
    // drawing's memory, proportional to file size, paid on every OPEN.
    //
    // Deliberately RAII and deliberately not a plain setter: a suppression left
    // on is a drawing that silently cannot be undone.
    class SuppressRecording {
    public:
        explicit SuppressRecording(UndoJournal& journal)
            : journal_(journal), previous_(journal.replaying_) {
            journal_.replaying_ = true;
        }
        ~SuppressRecording() { journal_.replaying_ = previous_; }
        SuppressRecording(const SuppressRecording&) = delete;
        SuppressRecording& operator=(const SuppressRecording&) = delete;

    private:
        UndoJournal& journal_;
        bool previous_;
    };

    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }

    // The name of the group the next undo or redo would act on. Empty when
    // there is none.
    const std::string& undo_name() const;
    const std::string& redo_name() const;

    // Reverses (or reapplies) one group. False when there is nothing to do.
    bool undo(Database& db);
    bool redo(Database& db);

    std::size_t undo_depth() const { return undo_.size(); }
    std::size_t redo_depth() const { return redo_.size(); }

    // --- The save point ------------------------------------------------------
    //
    // Called when the whole drawing has been written to a file. Remembers WHICH
    // group was on top, not how many there were.
    //
    // A DEPTH cannot answer this, and the failure is not exotic: save at depth
    // five, undo twice, then draw two new things. The redo stack is discarded
    // by the new work, the depth is five again, and a depth comparison reports
    // the drawing as saved when it holds two entirely different entities. A
    // count of steps from the save point has the same hole for the same reason.
    //
    // Serials are unique and never reused, so a rebuilt branch of the same
    // depth carries different ones and stays dirty -- correctly, and until it is
    // saved again. Undoing back to the save point restores the same serial and
    // goes clean; redoing forward past it goes dirty. Every direction is right
    // by construction rather than by case analysis.
    void mark_saved() { saved_serial_ = top_serial(); }

    // True when the drawing differs from what was last written. An empty
    // journal with no save recorded is clean: a drawing nobody has touched has
    // nothing to lose.
    bool dirty() const { return top_serial() != saved_serial_; }

    std::uint64_t top_serial() const { return undo_.empty() ? 0 : undo_.back().serial; }

    // Drops all history. NEW and OPEN do this -- undoing past the load of a
    // drawing is not meaningful.
    void clear();

private:
    void push(Change&& c);

    std::vector<UndoGroup> undo_;
    std::vector<UndoGroup> redo_;
    UndoGroup open_;
    int depth_{0};
    bool replaying_{false};

    // Never reused, and never rewound -- redo() MOVES a group back rather than
    // rebuilding it, so a serial survives any number of undo/redo round trips.
    std::uint64_t next_serial_{1};
    std::uint64_t saved_serial_{0};
};

}  // namespace ncad
