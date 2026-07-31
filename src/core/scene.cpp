// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/scene.hpp"

#include "ncad/database.hpp"
#include "ncad/entity.hpp"

#include <algorithm>

namespace ncad {

bool entity_visible(const Database& db, const Entity& e) {
    const LayerId id = e.props().layer;
    if (id >= db.layers().size()) return false;  // dangling layer reference
    const Layer& l = db.layer(id);
    return !l.frozen && l.color >= 0;
}

void draw_database(const Database& db, const DrawContext& ctx, Renderer& r) {
    for (const Handle h : db.order()) {
        const Entity* e = db.get(h);
        if (!e || !entity_visible(db, *e)) continue;
        // Off screen: not flattened, not projected, not handed to the backend.
        // QPainter would have discarded the pixels anyway, but only after all
        // of that work had been done.
        if (!ctx.visible(e->bbox())) continue;
        r.begin_entity(e->props());
        e->draw(ctx, r);
    }
}

void draw_database(const Database& db, const DrawContext& ctx, Renderer& r,
                   const std::vector<Handle>& skip) {
    // Sorted once rather than searched linearly per entity. `skip` is the set
    // hidden behind ghosts, so it is non-empty exactly while a selection is
    // being dragged -- and a linear find made that O(drawing x selection),
    // which is a billion comparisons a frame for a thousand entities moving in
    // a million. The one case where responsiveness matters most was the one
    // that scaled worst.
    std::vector<Handle> sorted(skip);
    std::sort(sorted.begin(), sorted.end());

    for (const Handle h : db.order()) {
        if (std::binary_search(sorted.begin(), sorted.end(), h)) continue;
        const Entity* e = db.get(h);
        if (!e || !entity_visible(db, *e)) continue;
        if (!ctx.visible(e->bbox())) continue;
        r.begin_entity(e->props());
        e->draw(ctx, r);
    }
}

void draw_handles(const Database& db, const DrawContext& ctx, Renderer& r,
                  const std::vector<Handle>& handles) {
    for (const Handle h : handles) {
        const Entity* e = db.get(h);
        if (!e || !entity_visible(db, *e)) continue;
        r.begin_entity(e->props());
        e->draw(ctx, r);
    }
}

void draw_entities(const std::vector<EntityPtr>& entities, const DrawContext& ctx, Renderer& r) {
    for (const EntityPtr& e : entities) {
        if (!e) continue;
        r.begin_entity(e->props());
        e->draw(ctx, r);
    }
}

}  // namespace ncad
