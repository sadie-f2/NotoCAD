// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Drawing a subset of entities in a different colour, as a Renderer that wraps
// another one -- the same shape as DashRenderer, and for the same reason.
//
// It turned out to need no change to the Renderer interface at all. That
// interface carries no colour of its own: begin_entity() takes EntityProps, and
// the backend infers the pen from it, resolving BYLAYER as it goes. So a
// wrapper that copies the props, overwrites `color` with a concrete ACI and
// forwards is the whole mechanism. Selected entities and in-flight ghosts both
// want exactly this.
//
// Colour and not linetype: forcing a dashed pattern would mean naming a
// LinetypeId that exists in this drawing, which a renderer has no business
// knowing. Chain this outside a DashRenderer when both are wanted -- the
// override happens first and the dashes are cut from the recoloured run.
#pragma once

#include "ncad/entity.hpp"
#include "ncad/render.hpp"
#include "ncad/vec3.hpp"

#include <cstdint>

namespace ncad {

class HighlightRenderer final : public Renderer {
public:
    HighlightRenderer(Renderer& target, std::int16_t color) : target_(target), color_(color) {}

    void begin_entity(const EntityProps& props) override {
        EntityProps forced = props;
        forced.color = color_;
        target_.begin_entity(forced);
    }

    void polyline(const Vec3* pts, std::size_t count, bool closed) override {
        target_.polyline(pts, count, closed);
    }

private:
    Renderer& target_;
    std::int16_t color_;
};

}  // namespace ncad
