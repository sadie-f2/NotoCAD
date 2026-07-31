// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Geometry a command has changed but not committed.
//
// Not "preview", which sounds like a display effect. This is state that exists
// between a command starting and committing, and the viewport is only its most
// visible consumer: it is what MOVE would do if you clicked now.
//
// THE ABSTRACTION IS NOT A TRANSFORM. The tempting version is a pending Mat4
// against the selection, and it fails immediately -- STRETCH cannot be written
// as one and neither can a dragged grip. So this sits one level up: the command
// is asked what the affected entities WOULD look like, and answers however it
// likes. MOVE answers with a matrix, STRETCH with stretch(delta, indices),
// ARRAY would answer with N matrices, and nothing out here knows the
// difference.
//
// Two invariants, both load-bearing:
//
//   It never touches the database or the undo journal. A mouse-move that
//   reached the journal would make every pixel of cursor travel an undo step.
//   The construct is as much defined by what it must not touch as by what it
//   holds.
//
//   It is derived, never stored and invalidated. Rebuilt whenever the tentative
//   value changes. Cloning a selection is cheap; tracking when a cached copy
//   went stale is how you end up showing a ghost of the wrong thing after an
//   undo.
#pragma once

#include "ncad/entity.hpp"

#include <vector>

namespace ncad {

struct InFlight {
    // The modified clones, to be drawn in the highlight colour. They have no
    // handles and no place in drawing order, because they are not in the
    // drawing.
    std::vector<EntityPtr> ghosts;

    // Committed entities to hide while the ghosts stand in for them.
    //
    // This is the whole of what "mark the selection" was reaching for, and why
    // carrying the database with a flagged subset would buy nothing: the
    // viewport already walks the database, and the one thing it cannot work out
    // for itself is which committed entities the ghosts replace. MOVE suppresses
    // its originals, COPY suppresses nothing, and the viewport stays ignorant of
    // the difference between them.
    std::vector<Handle> suppressed;

    bool empty() const { return ghosts.empty(); }

    void clear() {
        ghosts.clear();
        suppressed.clear();
    }
};

}  // namespace ncad
