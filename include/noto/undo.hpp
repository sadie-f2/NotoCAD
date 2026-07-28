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

#include "noto/entity.hpp"
#include "noto/sysvar.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace noto {

class Database;

enum class ChangeKind : std::uint8_t {
    AddEntity,      // `after` holds a copy of what was added
    EraseEntity,    // `before` holds what was removed, and where it sat
    ModifyEntity,   // both, same handle
    SetSysvar,
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
};

struct UndoGroup {
    std::string name;  // the command that made it, for "UNDO LINE" style echo
    std::vector<Change> changes;
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

    bool replaying() const { return replaying_; }

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
};

}  // namespace noto
