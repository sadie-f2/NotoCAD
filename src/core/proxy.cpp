// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/dxf.hpp"
#include "noto/entities.hpp"

namespace noto {

EntityPtr Proxy::clone() const {
    auto copy = std::make_unique<Proxy>();
    copy->dxf_name_ = dxf_name_;
    copy->groups_ = groups_;
    copy_common_to(*copy);
    return copy;
}

void Proxy::transform(const Mat4&) {
    // Nothing. Moving a proxy would mean knowing which of its groups are
    // coordinates, and not knowing is what makes it a proxy.
}

BBox Proxy::bbox() const {
    return BBox{};  // invalid, so nothing tries to pick or frame it
}

void Proxy::osnap_points(std::vector<OsnapPoint>&) const {}

void Proxy::grips(std::vector<Grip>&) const {}

void Proxy::stretch(const Vec3&, const GripIndex*, std::size_t) {}

void Proxy::draw(const DrawContext&, Renderer&) const {}

void Proxy::dxf_write(DxfWriter& w) const {
    // Straight back out, in the order it came in. write_common() is not used:
    // the groups already contain the entity's own type, handle, layer and
    // everything else, and writing ours over the top would change a file we do
    // not understand.
    w.code(0, dxf_name_);
    for (const DxfGroup& g : groups_) w.code(g.code, g.value);
}

}  // namespace noto
