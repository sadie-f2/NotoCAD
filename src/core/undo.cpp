// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/undo.hpp"

#include "ncad/database.hpp"

#include <utility>

namespace ncad {
namespace {

const std::string& empty_name() {
    static const std::string s;
    return s;
}

// While one of these is alive the journal records nothing. Undo and redo both
// drive the database through its ordinary mutators, and without this every undo
// would push its own work back onto the stack.
class Replaying {
public:
    explicit Replaying(bool& flag) : flag_(flag) { flag_ = true; }
    ~Replaying() { flag_ = false; }
    Replaying(const Replaying&) = delete;
    Replaying& operator=(const Replaying&) = delete;

private:
    bool& flag_;
};

}  // namespace

void UndoJournal::begin_group(std::string name) {
    if (depth_ == 0) {
        open_ = UndoGroup{};
        open_.name = std::move(name);
    }
    ++depth_;
}

void UndoJournal::end_group() {
    if (depth_ == 0) return;
    --depth_;
    if (depth_ > 0) return;

    // An empty group is not a step. A command that asked a question and was
    // cancelled should not cost an undo that visibly does nothing.
    if (!open_.changes.empty()) {
        open_.serial = next_serial_++;
        undo_.push_back(std::move(open_));
        redo_.clear();
    }
    open_ = UndoGroup{};
}

void UndoJournal::push(Change&& c) {
    if (replaying_) return;

    if (depth_ > 0) {
        open_.changes.push_back(std::move(c));
        return;
    }

    // No command running: the change is its own step. A bare entmake at the
    // command line is exactly this.
    UndoGroup g;
    g.changes.push_back(std::move(c));
    g.serial = next_serial_++;
    undo_.push_back(std::move(g));
    redo_.clear();
}

void UndoJournal::record_add(const Entity& added, std::size_t order_index) {
    if (replaying_) return;
    Change c;
    c.kind = ChangeKind::AddEntity;
    c.entity = added.handle();
    c.after = added.clone();
    c.order_index = order_index;
    push(std::move(c));
}

void UndoJournal::record_erase(const Entity& removed, std::size_t order_index) {
    if (replaying_) return;
    Change c;
    c.kind = ChangeKind::EraseEntity;
    c.entity = removed.handle();
    c.before = removed.clone();
    c.order_index = order_index;
    push(std::move(c));
}

void UndoJournal::record_modify(const Entity& before, const Entity& after) {
    if (replaying_) return;
    Change c;
    c.kind = ChangeKind::ModifyEntity;
    c.entity = before.handle();
    c.before = before.clone();
    c.after = after.clone();
    push(std::move(c));
}

void UndoJournal::record_sysvar(Sysvar id, const SysvarValue& before, const SysvarValue& after) {
    if (replaying_) return;
    Change c;
    c.kind = ChangeKind::SetSysvar;
    c.sysvar = id;
    c.sysvar_before = before;
    c.sysvar_after = after;
    push(std::move(c));
}

void UndoJournal::record_layer_add(LayerId id, const Layer& added) {
    if (replaying_) return;
    Change c;
    c.kind = ChangeKind::AddLayer;
    c.table = std::make_unique<TableChange>();
    c.table->layer_id = id;
    // Kept so redo can put it back; undo only needs to know it was the last.
    c.table->layer_after = added;
    push(std::move(c));
}

void UndoJournal::record_layer_modify(LayerId id, const Layer& before, const Layer& after) {
    if (replaying_) return;
    Change c;
    c.kind = ChangeKind::ModifyLayer;
    c.table = std::make_unique<TableChange>();
    c.table->layer_id = id;
    c.table->layer_before = before;
    c.table->layer_after = after;
    push(std::move(c));
}

void UndoJournal::record_linetype_add(LinetypeId id, const Linetype& added) {
    if (replaying_) return;
    Change c;
    c.kind = ChangeKind::AddLinetype;
    c.table = std::make_unique<TableChange>();
    c.table->linetype_id = id;
    c.table->linetype_after = added;
    push(std::move(c));
}

void UndoJournal::record_linetype_modify(LinetypeId id, const Linetype& before,
                                         const Linetype& after) {
    if (replaying_) return;
    Change c;
    c.kind = ChangeKind::ModifyLinetype;
    c.table = std::make_unique<TableChange>();
    c.table->linetype_id = id;
    c.table->linetype_before = before;
    c.table->linetype_after = after;
    push(std::move(c));
}

const std::string& UndoJournal::undo_name() const {
    return undo_.empty() ? empty_name() : undo_.back().name;
}

const std::string& UndoJournal::redo_name() const {
    return redo_.empty() ? empty_name() : redo_.back().name;
}

void UndoJournal::record_ucs_add(UcsId id, const UcsDef& added) {
    if (replaying_) return;
    Change c;
    c.kind = ChangeKind::AddUcs;
    c.table = std::make_unique<TableChange>();
    c.table->ucs_id = id;
    c.table->ucs_after = added;
    push(std::move(c));
}

void UndoJournal::record_ucs_modify(UcsId id, const UcsDef& before, const UcsDef& after) {
    if (replaying_) return;
    Change c;
    c.kind = ChangeKind::ModifyUcs;
    c.table = std::make_unique<TableChange>();
    c.table->ucs_id = id;
    c.table->ucs_before = before;
    c.table->ucs_after = after;
    push(std::move(c));
}

void UndoJournal::record_block_add(BlockId id, const BlockDef& added) {
    if (replaying_) return;
    Change c;
    c.kind = ChangeKind::AddBlock;
    c.table = std::make_unique<TableChange>();
    c.table->block_id = id;
    c.table->block_after = added.clone();
    push(std::move(c));
}

void UndoJournal::record_block_modify(BlockId id, const BlockDef& before, const BlockDef& after) {
    if (replaying_) return;
    Change c;
    c.kind = ChangeKind::ModifyBlock;
    c.table = std::make_unique<TableChange>();
    c.table->block_id = id;
    c.table->block_before = before.clone();
    c.table->block_after = after.clone();
    push(std::move(c));
}

bool UndoJournal::undo(Database& db) {
    if (undo_.empty()) return false;

    UndoGroup g = std::move(undo_.back());
    undo_.pop_back();

    {
        Replaying guard(replaying_);
        // Backwards: within a group the changes happened in order, so reversing
        // them has to run the other way or a later change can undo onto state a
        // former one has not restored yet.
        for (std::size_t i = g.changes.size(); i-- > 0;) {
            Change& c = g.changes[i];
            switch (c.kind) {
                case ChangeKind::AddEntity:
                    db.erase(c.entity);
                    break;
                case ChangeKind::EraseEntity:
                    db.restore(c.entity, c.before->clone(), c.order_index);
                    break;
                case ChangeKind::ModifyEntity:
                    db.replace(c.entity, c.before->clone());
                    break;
                case ChangeKind::SetSysvar:
                    // set_owned, not set: replaying is by definition the
                    // owner's business, and the read-only flag exists to stop
                    // the USER writing a variable. Going through the public
                    // door here would make every read-only variable
                    // un-undoable, which is a hole in undo rather than a
                    // safety measure.
                    db.sysvars().set_owned(c.sysvar, c.sysvar_before);
                    break;
                case ChangeKind::AddLayer:
                    // Adds append, so the one being undone is the last, and
                    // anything referring to it was added later and has already
                    // been undone -- undo is strictly last-in-first-out.
                    db.pop_layer();
                    break;
                case ChangeKind::ModifyLayer:
                    db.restore_layer(c.table->layer_id, c.table->layer_before);
                    break;
                case ChangeKind::AddLinetype:
                    db.pop_linetype();
                    break;
                case ChangeKind::ModifyLinetype:
                    db.restore_linetype(c.table->linetype_id, c.table->linetype_before);
                    break;
                case ChangeKind::AddUcs:
                    db.pop_ucs();
                    break;
                case ChangeKind::ModifyUcs:
                    db.restore_ucs(c.table->ucs_id, c.table->ucs_before);
                    break;
                case ChangeKind::AddBlock:
                    db.pop_block();
                    break;
                case ChangeKind::ModifyBlock:
                    db.restore_block(c.table->block_id, c.table->block_before.clone());
                    break;
            }
        }
    }

    redo_.push_back(std::move(g));
    return true;
}

bool UndoJournal::redo(Database& db) {
    if (redo_.empty()) return false;

    UndoGroup g = std::move(redo_.back());
    redo_.pop_back();

    {
        Replaying guard(replaying_);
        // Forwards this time: redo replays the group as it originally ran.
        for (Change& c : g.changes) {
            switch (c.kind) {
                case ChangeKind::AddEntity:
                    db.restore(c.entity, c.after->clone(), c.order_index);
                    break;
                case ChangeKind::EraseEntity:
                    db.erase(c.entity);
                    break;
                case ChangeKind::ModifyEntity:
                    db.replace(c.entity, c.after->clone());
                    break;
                case ChangeKind::SetSysvar:
                    db.sysvars().set_owned(c.sysvar, c.sysvar_after);
                    break;
                case ChangeKind::AddLayer:
                    db.restore_layer(c.table->layer_id, c.table->layer_after);
                    break;
                case ChangeKind::ModifyLayer:
                    db.restore_layer(c.table->layer_id, c.table->layer_after);
                    break;
                case ChangeKind::AddLinetype:
                    db.restore_linetype(c.table->linetype_id, c.table->linetype_after);
                    break;
                case ChangeKind::ModifyLinetype:
                    db.restore_linetype(c.table->linetype_id, c.table->linetype_after);
                    break;
                case ChangeKind::AddUcs:
                case ChangeKind::ModifyUcs:
                    db.restore_ucs(c.table->ucs_id, c.table->ucs_after);
                    break;
                case ChangeKind::AddBlock:
                case ChangeKind::ModifyBlock:
                    db.restore_block(c.table->block_id, c.table->block_after.clone());
                    break;
            }
        }
    }

    undo_.push_back(std::move(g));
    return true;
}

void UndoJournal::clear() {
    undo_.clear();
    redo_.clear();
    open_ = UndoGroup{};
    depth_ = 0;
    // NEW and OPEN land here, and both leave a drawing that is not modified
    // relative to what is on disk. An empty stack has top serial zero, so
    // matching the save point to zero makes that fall out rather than being a
    // special case anywhere else. Serials themselves keep counting up: reusing
    // them is the one thing that would reintroduce the ambiguity.
    saved_serial_ = 0;
}

}  // namespace ncad
