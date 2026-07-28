// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/undo.hpp"

#include "noto/database.hpp"

#include <utility>

namespace noto {
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

const std::string& UndoJournal::undo_name() const {
    return undo_.empty() ? empty_name() : undo_.back().name;
}

const std::string& UndoJournal::redo_name() const {
    return redo_.empty() ? empty_name() : redo_.back().name;
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
                    db.sysvars().set(sysvar_def(c.sysvar).name, c.sysvar_before);
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
                    db.sysvars().set(sysvar_def(c.sysvar).name, c.sysvar_after);
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
}

}  // namespace noto
