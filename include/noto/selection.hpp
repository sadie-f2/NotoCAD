// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Selection sets: what "Select objects:" collects, and what the editing
// commands act on.
//
// A set of handles, and -- crucially -- not only a set of handles. STRETCH needs
// the crossing region as well, because the question it asks is "which defining
// points fell inside the window", and that is not answerable from a list of
// entities. Everything in the set is being stretched; only some of each
// entity's points move.
//
// This is also why STRETCH is crossing-only. With a plain Window, or with
// objects picked one at a time, every defining point of every selected entity
// is inside the selection, so "move the points that are inside" moves all of
// them and STRETCH silently degenerates into MOVE. No error, no warning. R12
// behaves that way and it is the classic reason the command looks broken.
#pragma once

#include "noto/entity.hpp"
#include "noto/vec3.hpp"

#include <cstddef>
#include <vector>

namespace noto {

// A screen-aligned rectangle, frozen into world space at the moment it was
// dragged: an origin corner and the two in-plane axes it was drawn against.
//
// Stored this way rather than as a screen rectangle so that nothing downstream
// needs a Viewport to ask about it -- STRETCH runs in the kernel and the
// selection may outlive the view that made it. Depth is ignored, because a
// crossing window is a screen-space question: it catches whatever lies under
// it, however far away.
struct SelectionRegion {
    Vec3 origin{};
    Vec3 ax{1, 0, 0};  // unit; screen right at selection time
    Vec3 ay{0, 1, 0};  // unit; screen up
    double width{0.0};
    double height{0.0};

    bool contains(const Vec3& p) const;
};

class SelectionSet {
public:
    // Adding an entity already present is a no-op rather than a duplicate:
    // picking the same line twice must not erase it twice or move it double.
    bool add(Handle h);
    bool remove(Handle h);
    bool contains(Handle h) const;

    void clear();

    std::size_t size() const { return handles_.size(); }
    bool empty() const { return handles_.empty(); }

    // In the order selected, which is what "N found" counts and what commands
    // iterate. Not drawing order.
    const std::vector<Handle>& handles() const { return handles_; }

    // The crossing region STRETCH will ask about. Only the most recent is kept:
    // AutoCAD uses the last crossing window to decide which points move, even
    // after several selections, so a list would be modelling something that
    // does not exist.
    //
    // NOTE: that "last one wins" rule is taken from usage and is flagged in
    // SF_todo.md as wanting confirmation against the R12 documentation.
    void set_region(const SelectionRegion& r);
    bool has_region() const { return has_region_; }
    const SelectionRegion& region() const { return region_; }
    void clear_region() { has_region_ = false; }

private:
    std::vector<Handle> handles_;
    SelectionRegion region_{};
    bool has_region_{false};
};

}  // namespace noto
