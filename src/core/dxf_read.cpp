// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/dxf_read.hpp"

#include "noto/database.hpp"
#include "noto/ecs.hpp"
#include "noto/entities.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <numbers>
#include <sstream>
#include <vector>

namespace noto {
namespace {

constexpr double kDegToRad = std::numbers::pi / 180.0;

// A DXF file is pairs of lines: a group code, then its value. Whitespace around
// the code is ignored; the value is taken whole, because layer names and text
// strings may contain spaces and leading ones are meaningful.
class GroupStream {
public:
    explicit GroupStream(std::string text) : text_(std::move(text)) {}

    bool next(int& code, std::string& value) {
        std::string code_line;
        if (!read_line(code_line)) return false;
        if (!read_line(value)) return false;

        // strtol rather than stoi: a malformed code should end the file, not
        // throw, since exceptions are not control flow here.
        char* end = nullptr;
        const long parsed = std::strtol(code_line.c_str(), &end, 10);
        if (end == code_line.c_str()) return false;
        code = static_cast<int>(parsed);
        return true;
    }

private:
    bool read_line(std::string& out) {
        if (pos_ >= text_.size()) return false;
        const std::size_t eol = text_.find('\n', pos_);
        if (eol == std::string::npos) {
            out = text_.substr(pos_);
            pos_ = text_.size();
        } else {
            out = text_.substr(pos_, eol - pos_);
            pos_ = eol + 1;
        }
        // DXF written on Windows carries a trailing carriage return.
        if (!out.empty() && out.back() == '\r') out.pop_back();
        // Codes are often written right-aligned in a three-character field.
        std::size_t begin = out.find_first_not_of(" \t");
        if (begin == std::string::npos) {
            out.clear();
        } else if (begin != 0) {
            out.erase(0, begin);
        }
        return true;
    }

    std::string text_;
    std::size_t pos_{0};
};

double to_double(const std::string& s) { return std::strtod(s.c_str(), nullptr); }
int to_int(const std::string& s) { return static_cast<int>(std::strtol(s.c_str(), nullptr, 10)); }

// Everything gathered for one entity before it is turned into geometry.
struct EntityGroups {
    std::string name;
    std::vector<DxfGroup> groups;

    bool has(int code) const {
        for (const DxfGroup& g : groups) {
            if (g.code == code) return true;
        }
        return false;
    }
    std::string text(int code, const std::string& fallback = {}) const {
        for (const DxfGroup& g : groups) {
            if (g.code == code) return g.value;
        }
        return fallback;
    }
    double real(int code, double fallback = 0.0) const {
        for (const DxfGroup& g : groups) {
            if (g.code == code) return to_double(g.value);
        }
        return fallback;
    }
    Vec3 point(int base) const {
        return Vec3{real(base), real(base + 10), real(base + 20)};
    }
    Vec3 normal() const {
        const Vec3 n{real(210, 0.0), real(220, 0.0), real(230, 1.0)};
        return is_zero(n) ? kWorldZ : normalize(n);
    }
};

class Reader {
public:
    explicit Reader(Database& db) : db_(db) {}

    DxfReadResult run(std::string text);

private:
    void apply_common(Entity& e, const EntityGroups& g);
    EntityPtr build(const EntityGroups& g, GroupStream& in, int& pending_code,
                    std::string& pending_value, bool& has_pending);
    EntityPtr build_polyline(const EntityGroups& g, GroupStream& in, int& pending_code,
                             std::string& pending_value, bool& has_pending);
    void read_tables(GroupStream& in);
    void read_entities(GroupStream& in);

    Database& db_;
    DxfReadResult result_;
};

void Reader::apply_common(Entity& e, const EntityGroups& g) {
    const std::string layer = g.text(8, "0");
    LayerId id = db_.find_layer(layer);
    // A file may name a layer its own table forgot to define. Creating it is
    // better than dropping the entity onto layer 0 and losing the name.
    if (id == kInvalidLayer) id = db_.add_layer(layer);
    e.props().layer = id;

    const std::string ltype = g.text(6);
    if (!ltype.empty() && ltype != "BYLAYER") {
        const LinetypeId lt = db_.find_linetype(ltype);
        if (lt != kInvalidLinetype) e.props().linetype = lt;
    }
    if (g.has(62)) e.props().color = static_cast<std::int16_t>(to_int(g.text(62)));
    if (g.has(39)) e.props().thickness = g.real(39);
    e.props().normal = g.normal();
}

EntityPtr Reader::build_polyline(const EntityGroups& g, GroupStream& in, int& pending_code,
                                 std::string& pending_value, bool& has_pending) {
    auto poly = std::make_unique<Polyline>();
    apply_common(*poly, g);
    // Group 70 bit 1 is closed.
    if ((to_int(g.text(70, "0")) & 1) != 0) poly->set_closed(true);

    const Mat4 to_world = ecs_to_world(poly->props().normal);

    // VERTEX records follow until SEQEND. They are consumed here rather than
    // becoming entities, which is the storage decision: a mesh cannot afford
    // one database entity per vertex.
    EntityGroups current;
    bool in_vertex = false;

    int code = 0;
    std::string value;
    for (;;) {
        if (has_pending) {
            code = pending_code;
            value = pending_value;
            has_pending = false;
        } else if (!in.next(code, value)) {
            break;
        }

        if (code == 0) {
            if (in_vertex) {
                poly->add(to_world.transform_point(current.point(10)), current.real(42, 0.0));
                in_vertex = false;
            }
            if (value == "VERTEX") {
                current = EntityGroups{};
                in_vertex = true;
                continue;
            }
            if (value == "SEQEND") {
                // Consume SEQEND's own groups, which end at the next 0.
                for (;;) {
                    if (!in.next(code, value)) return poly;
                    if (code == 0) {
                        pending_code = code;
                        pending_value = value;
                        has_pending = true;
                        return poly;
                    }
                }
            }
            // Anything else ends the polyline; hand it back to the caller.
            pending_code = code;
            pending_value = value;
            has_pending = true;
            return poly;
        }
        if (in_vertex) current.groups.push_back({code, value});
    }
    if (in_vertex) poly->add(to_world.transform_point(current.point(10)), current.real(42, 0.0));
    return poly;
}

EntityPtr Reader::build(const EntityGroups& g, GroupStream& in, int& pending_code,
                        std::string& pending_value, bool& has_pending) {
    if (g.name == "LINE") {
        // R12 keeps both endpoints of a LINE in world coordinates, unlike every
        // other entity here. entities_dxf.cpp says the same on the way out.
        auto e = std::make_unique<Line>(g.point(10), g.point(11));
        apply_common(*e, g);
        return e;
    }
    if (g.name == "CIRCLE") {
        auto e = std::make_unique<Circle>(Vec3{}, g.real(40, 1.0));
        apply_common(*e, g);
        e->set_center(ecs_to_world(e->props().normal).transform_point(g.point(10)));
        return e;
    }
    if (g.name == "ARC") {
        auto e = std::make_unique<Arc>(Vec3{}, g.real(40, 1.0), g.real(50) * kDegToRad,
                                       g.real(51) * kDegToRad);
        apply_common(*e, g);
        e->set_center(ecs_to_world(e->props().normal).transform_point(g.point(10)));
        return e;
    }
    if (g.name == "POLYLINE") {
        return build_polyline(g, in, pending_code, pending_value, has_pending);
    }

    // Everything else survives as itself.
    auto p = std::make_unique<Proxy>();
    p->set_dxf_name(g.name);
    for (const DxfGroup& x : g.groups) p->add_group(x.code, x.value);
    ++result_.proxies;
    return p;
}

void Reader::read_tables(GroupStream& in) {
    int code = 0;
    std::string value;
    std::string table;
    EntityGroups current;
    bool in_entry = false;

    auto flush = [&] {
        if (!in_entry) return;
        in_entry = false;
        const std::string name = current.text(2);
        if (name.empty()) return;

        if (table == "LAYER") {
            const LinetypeId lt = db_.find_linetype(current.text(6, "CONTINUOUS"));
            const LayerId id =
                db_.add_layer(name, static_cast<std::int16_t>(to_int(current.text(62, "7"))),
                              lt == kInvalidLinetype ? kLinetypeContinuous : lt);
            // Group 70 bit 1 is frozen, bit 4 is locked.
            const int flags = to_int(current.text(70, "0"));
            if ((flags & 1) != 0) db_.set_layer_frozen(id, true);
            if ((flags & 4) != 0) db_.set_layer_locked(id, true);
            ++result_.layers;
        } else if (table == "LTYPE") {
            std::vector<double> pattern;
            for (const DxfGroup& x : current.groups) {
                if (x.code == 49) pattern.push_back(to_double(x.value));
            }
            db_.add_linetype(name, current.text(3), std::move(pattern));
            ++result_.linetypes;
        }
    };

    while (in.next(code, value)) {
        if (code == 0) {
            flush();
            if (value == "ENDSEC") return;
            if (value == "TABLE") {
                table.clear();
                continue;
            }
            if (value == "ENDTAB") {
                table.clear();
                continue;
            }
            current = EntityGroups{};
            current.name = value;
            in_entry = (value == "LAYER" || value == "LTYPE");
            continue;
        }
        if (code == 2 && table.empty() && !in_entry) {
            table = value;  // the name following TABLE
            continue;
        }
        if (in_entry) current.groups.push_back({code, value});
    }
}

void Reader::read_entities(GroupStream& in) {
    int code = 0;
    std::string value;
    bool has_pending = false;
    int pending_code = 0;
    std::string pending_value;

    EntityGroups current;
    bool have = false;

    for (;;) {
        if (has_pending) {
            code = pending_code;
            value = pending_value;
            has_pending = false;
        } else if (!in.next(code, value)) {
            break;
        }

        if (code != 0) {
            if (have) current.groups.push_back({code, value});
            continue;
        }

        // A zero group ends whatever was being collected. The group itself is
        // handed to build() as pending rather than consumed here, because a
        // POLYLINE has to see the VERTEX record that follows it -- reading it
        // away first is an off-by-one that eats the first vertex.
        if (have) {
            have = false;
            pending_code = code;
            pending_value = value;
            has_pending = true;

            EntityPtr e = build(current, in, pending_code, pending_value, has_pending);
            if (e) {
                db_.add(std::move(e));
                ++result_.entities;
            }
            // Whatever build() did not consume comes round again.
            continue;
        }

        if (value == "ENDSEC") return;

        current = EntityGroups{};
        current.name = value;
        have = true;
    }

    // A file that ends without ENDSEC still yields what it had.
    if (have) {
        has_pending = false;
        EntityPtr e = build(current, in, pending_code, pending_value, has_pending);
        if (e) {
            db_.add(std::move(e));
            ++result_.entities;
        }
    }
}

DxfReadResult Reader::run(std::string text) {
    GroupStream in(std::move(text));

    int code = 0;
    std::string value;
    std::string section;

    while (in.next(code, value)) {
        if (code == 0 && value == "SECTION") {
            section.clear();
            continue;
        }
        if (code == 2 && section.empty()) {
            section = value;
            if (section == "TABLES") {
                read_tables(in);
                section.clear();
            } else if (section == "ENTITIES") {
                read_entities(in);
                section.clear();
            }
            continue;
        }
        if (code == 0 && value == "ENDSEC") {
            section.clear();
            continue;
        }
        if (code == 0 && value == "EOF") break;

        // $ACADVER, so a later file can be reported rather than silently
        // half-understood.
        if (code == 9 && value == "$ACADVER") {
            if (in.next(code, value) && code == 1) {
                result_.version = value;
                // AC1009 is R12. Anything higher is newer.
                result_.newer_version = (value > "AC1009");
            }
            continue;
        }
    }

    result_.ok = true;
    return result_;
}

}  // namespace

DxfReadResult read_dxf_text(Database& db, const std::string& text) {
    db.clear();
    db.journal().clear();
    Reader r(db);
    DxfReadResult result = r.run(text);
    // A freshly opened drawing has no history: undoing past the load is not
    // meaningful, and the load itself is not an edit.
    db.journal().clear();
    return result;
}

DxfReadResult read_dxf_file(Database& db, const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        DxfReadResult r;
        r.error = "cannot open " + path;
        return r;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return read_dxf_text(db, buffer.str());
}

}  // namespace noto
