// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Walking a database into a Renderer.
//
// Separate from render.hpp so that the Renderer interface -- the thing entities
// and backends both depend on -- does not drag the database in behind it.
#pragma once

#include "noto/render.hpp"

namespace noto {

class Database;
class Entity;

// True if the entity's layer is on and thawed. R12 stores "off" as a negative
// layer colour and freezing as its own flag; both mean invisible.
bool entity_visible(const Database& db, const Entity& e);

// Draws every visible entity in drawing order, calling begin_entity() before
// each. Drawing order is R12's own display order and is what makes output
// deterministic.
void draw_database(const Database& db, const DrawContext& ctx, Renderer& r);

}  // namespace noto
