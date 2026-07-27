// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/scene.hpp"

#include "noto/database.hpp"
#include "noto/entity.hpp"

namespace noto {

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
        r.begin_entity(e->props());
        e->draw(ctx, r);
    }
}

}  // namespace noto
