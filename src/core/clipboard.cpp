// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/clipboard.hpp"

#include "ncad/blocks.hpp"
#include "ncad/database.hpp"
#include "ncad/dxf.hpp"
#include "ncad/entities.hpp"

#include <cstdlib>
#include <string>
#include <unordered_map>

namespace ncad {
namespace {

// Carries a selection into a scratch database, closing over everything the
// entities name: an id in EntityProps indexes THIS database's tables, so a
// clone crossing databases must have its layer and linetype re-resolved by
// name, and an INSERT must be re-pointed at a copy of its definition -- the
// BlockDef* it holds is an address in the source database. Definitions copy
// recursively, so a nested assembly travels whole.
struct FragmentCopier {
    const Database& src;
    Database& out;

    std::unordered_map<LayerId, LayerId> layer_map;
    std::unordered_map<LinetypeId, LinetypeId> linetype_map;
    std::unordered_map<const BlockDef*, BlockId> block_map;

    LinetypeId map_linetype(LinetypeId id) {
        if (id >= src.linetypes().size()) return kLinetypeContinuous;
        const auto it = linetype_map.find(id);
        if (it != linetype_map.end()) return it->second;

        const Linetype& lt = src.linetype(id);
        const LinetypeId mapped = out.add_linetype(lt.name, lt.description, lt.pattern);
        linetype_map.emplace(id, mapped);
        return mapped;
    }

    LayerId map_layer(LayerId id) {
        if (id >= src.layers().size()) return kLayerZero;
        const auto it = layer_map.find(id);
        if (it != layer_map.end()) return it->second;

        const Layer& l = src.layer(id);
        const LinetypeId lt = map_linetype(l.linetype);
        const bool fresh = out.find_layer(l.name) == kInvalidLayer;
        const LayerId mapped = out.add_layer(l.name, l.color, lt);
        // add_layer returns the existing entry for a name already present --
        // layer 0 always is -- and that entry keeps its own settings, the same
        // name-collision rule the merge on the paste side applies. Only an
        // entry this copy created carries the source's full state across.
        if (fresh) out.set_layer(mapped, Layer{l.name, l.color, lt, l.frozen, l.locked});
        layer_map.emplace(id, mapped);
        return mapped;
    }

    BlockId map_block(const BlockDef* def, int depth) {
        if (def == nullptr || depth > kMaxBlockDepth) return kInvalidBlock;
        const auto it = block_map.find(def);
        if (it != block_map.end()) return it->second;

        BlockDef copy;
        copy.name = def->name;
        copy.base = def->base;
        copy.flags = def->flags;
        for (const EntityPtr& e : def->entities) {
            if (!e) continue;
            EntityPtr c = e->clone();
            remap(*c, depth + 1);
            copy.entities.push_back(std::move(c));
        }

        const BlockId mapped = out.add_block(std::move(copy));
        block_map.emplace(def, mapped);
        return mapped;
    }

    void remap(Entity& e, int depth) {
        e.props().layer = map_layer(e.props().layer);
        e.props().linetype = map_linetype(e.props().linetype);

        if (e.type() == EntityType::Insert) {
            auto& ins = static_cast<Insert&>(e);
            const BlockId mapped = map_block(ins.definition(), depth);
            ins.set_definition(mapped == kInvalidBlock ? nullptr : out.block(mapped));
        }
    }
};

// One code/value pair of a DXF text stream. Lines may end \n or \r\n -- a
// clipboard is exactly where CRLF text from another program turns up.
struct PairScanner {
    const std::string& text;
    std::size_t pos{0};

    bool next_line(std::string& out) {
        if (pos >= text.size()) return false;
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        out.assign(text, pos, end - pos);
        pos = end + 1;
        while (!out.empty() && out.back() == '\r') out.pop_back();
        return true;
    }

    bool next_pair(int& code, std::string& value) {
        std::string code_line;
        if (!next_line(code_line) || !next_line(value)) return false;

        const char* s = code_line.c_str();
        char* parsed_to = nullptr;
        const long c = std::strtol(s, &parsed_to, 10);
        if (parsed_to == s) return false;
        code = static_cast<int>(c);
        return true;
    }
};

}  // namespace

std::string clip_fragment(const Database& db, const std::vector<Handle>& handles,
                          const Vec3& base) {
    Database out;
    FragmentCopier copier{db, out, {}, {}, {}};

    for (const Handle h : handles) {
        const Entity* e = db.get(h);
        if (!e) continue;
        EntityPtr c = e->clone();
        copier.remap(*c, 0);
        out.add(std::move(c));
    }

    out.sysvars().set("INSBASE", SysvarValue::of_point(base));
    return write_dxf_text(out, DxfVersion::R2000);
}

Vec3 clip_base(const std::string& text) {
    PairScanner scan{text};
    int code = 0;
    std::string value;

    // Find $INSBASE in the header, then collect its point. Stopping at the
    // next group 9 (or 0, which ends the header) keeps a 10/20/30 belonging to
    // some later variable from being taken for the base.
    while (scan.next_pair(code, value)) {
        if (code != 9 || value != "$INSBASE") continue;

        Vec3 base{};
        while (scan.next_pair(code, value)) {
            if (code == 9 || code == 0) break;
            if (code == 10) base.x = std::strtod(value.c_str(), nullptr);
            if (code == 20) base.y = std::strtod(value.c_str(), nullptr);
            if (code == 30) base.z = std::strtod(value.c_str(), nullptr);
        }
        return base;
    }
    return Vec3{};
}

bool clip_looks_like_dxf(const std::string& text) {
    PairScanner scan{text};
    int code = 0;
    std::string value;

    // The first pair that is not a 999 comment must be 0/SECTION. That is how
    // every DXF document opens, and nothing that is not one starts that way.
    while (scan.next_pair(code, value)) {
        if (code == 999) continue;
        return code == 0 && value == "SECTION";
    }
    return false;
}

}  // namespace ncad
