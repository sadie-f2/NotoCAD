// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Walking a database into a Renderer.
//
// Separate from render.hpp so that the Renderer interface -- the thing entities
// and backends both depend on -- does not drag the database in behind it.
#pragma once

#include "ncad/entity.hpp"
#include "ncad/render.hpp"

#include <vector>

namespace ncad {

class Database;

// True if the entity's layer is on and thawed. R12 stores "off" as a negative
// layer colour and freezing as its own flag; both mean invisible.
bool entity_visible(const Database& db, const Entity& e);

// Draws every visible entity in drawing order, calling begin_entity() before
// each. Drawing order is R12's own display order and is what makes output
// deterministic.
void draw_database(const Database& db, const DrawContext& ctx, Renderer& r);

// The same, less the entities in `skip`.
//
// In-flight geometry is why it exists: while a MOVE is being dragged, the
// entities it will replace are already being stood in for by ghosts, and
// drawing both would show the selection in two places at once. COPY passes an
// empty list, which is the whole difference between the two commands as far as
// the viewport is concerned.
//
// `skip` is a selection rather than a drawing -- tens of handles, not
// thousands -- so it is scanned linearly rather than hashed. If that ever stops
// being true, the spatial index in phase 14 arrives first.
void draw_database(const Database& db, const DrawContext& ctx, Renderer& r,
                   const std::vector<Handle>& skip);

// Draws just the named entities, in the order given rather than in drawing
// order. Highlighting a selection is the caller: wrap `r` in a
// HighlightRenderer and pass the selection's handles.
//
// Visibility is still honoured. A frozen layer's entity can be in a selection
// set -- ALL skips them, but a handle held by AutoLISP need not -- and drawing
// it because it happens to be selected would put geometry on screen that the
// drawing says is not there.
void draw_handles(const Database& db, const DrawContext& ctx, Renderer& r,
                  const std::vector<Handle>& handles);

// Draws entities that are not in the database at all: the modified clones a
// command is holding and has not committed. Layer visibility cannot be checked
// against anything here, so the caller is trusted -- these came from entities
// that were visible when they were cloned.
void draw_entities(const std::vector<EntityPtr>& entities, const DrawContext& ctx, Renderer& r);

}  // namespace ncad
