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
    void read_blocks(GroupStream& in);

    // An INSERT may name a block defined later in the file, and a block may
    // insert another block. So inserts are built unresolved and fixed up once
    // every definition has been read -- resolving inline would work only for
    // files that happen to be in dependency order, which nothing guarantees.
    //
    // The raw pointers stay valid because entities are heap-allocated and only
    // the owning unique_ptr moves.
    struct PendingInsert {
        Insert* entity{nullptr};
        std::string block_name;
        InsertPlacement placement;
    };

    void resolve_inserts();

    Database& db_;
    DxfReadResult result_;
    std::vector<PendingInsert> pending_inserts_;

    // The current UCS from the HEADER, applied once the whole file is read.
    Ucs header_ucs_;
    std::string header_ucs_name_;
    bool have_header_ucs_{false};
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

    // The header's groups 40 and 41 are the default widths for every vertex
    // that does not carry its own. Reading them here rather than ignoring them
    // is what stops a uniformly wide polyline coming back as a hairline.
    const double default_start_width = g.real(40, 0.0);
    const double default_end_width = g.real(41, 0.0);

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
                poly->add(to_world.transform_point(current.point(10)), current.real(42, 0.0),
                          current.real(40, default_start_width),
                          current.real(41, default_end_width));
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
    if (in_vertex) poly->add(to_world.transform_point(current.point(10)), current.real(42, 0.0),
                          current.real(40, default_start_width),
                          current.real(41, default_end_width));
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

    // --- Entities newer than AC1009 -----------------------------------------
    //
    // None of these can appear in an R12 file, so reading them is purely about
    // IMPORT: modern AutoCAD writes LWPOLYLINE where R12 wrote POLYLINE, and
    // the database has held ELLIPSE and SPLINE natively for a while without
    // being able to read one back. Writing still degrades to R12 -- that
    // asymmetry is deliberate, and AC1009 remains the interchange guarantee.

    if (g.name == "ELLIPSE") {
        // Centre and major axis are in WORLD coordinates here, not ECS -- the
        // major axis is a vector from the centre rather than a point. That is
        // unlike CIRCLE and ARC in the same file, and getting it wrong puts a
        // tilted ellipse in the wrong place while leaving a flat one perfect.
        const Vec3 centre = g.point(10);
        const Vec3 major = g.point(11);
        const double ratio = g.real(40, 1.0);
        const double start = g.real(41, 0.0);
        const double end = g.real(42, kFullTurn);

        auto e = std::make_unique<Ellipse>(centre, major, ratio, start, end, g.normal());
        apply_common(*e, g);
        e->set_center(centre);
        e->set_major_axis(major);
        return e;
    }

    if (g.name == "MTEXT") {
        // Text over 250 characters is SPLIT across repeated group 3 chunks with
        // the tail in group 1. Reading only group 1 silently truncates every
        // long paragraph, which is the kind of loss that looks like the file
        // was wrong rather than the reader.
        std::string text;
        for (const DxfGroup& x : g.groups) {
            if (x.code == 3) text += x.value;
        }
        text += g.text(1);

        auto e = std::make_unique<MText>(g.point(10), std::move(text), g.real(40, 1.0),
                                         g.real(41, 0.0));
        apply_common(*e, g);
        e->set_position(ecs_to_world(e->props().normal).transform_point(g.point(10)));

        const int attach = to_int(g.text(71, "1"));
        if (attach >= 1 && attach <= 9) e->set_attach(static_cast<MTextAttach>(attach));
        if (g.has(44)) e->set_line_spacing(g.real(44, 1.0));

        // Rotation is group 50, or an X-axis direction vector in group 11 --
        // the same angle said two ways, and a file may use either.
        if (g.has(50)) {
            e->set_rotation(g.real(50) * kDegToRad);
        } else if (g.has(11)) {
            const Vec3 dir = g.point(11);
            if (!is_zero(dir)) {
                const Basis b = arbitrary_axis(e->props().normal);
                e->set_rotation(std::atan2(dot(dir, b.ay), dot(dir, b.ax)));
            }
        }
        return e;
    }

    if (g.name == "LWPOLYLINE") {
        auto e = std::make_unique<Polyline>();
        apply_common(*e, g);
        e->set_closed((static_cast<int>(g.real(70, 0.0)) & 1) != 0);

        // Vertices arrive as REPEATED group codes, so this walks the groups in
        // order rather than asking for one by number: every 10 opens a vertex
        // and the 20, 40, 41 and 42 that follow belong to it.
        const double elevation = g.real(38, 0.0);
        const double constant_width = g.real(43, 0.0);
        const Mat4 to_world = ecs_to_world(e->props().normal);

        bool open = false;
        Vec3 pos{};
        double bulge = 0.0;
        double w0 = constant_width;
        double w1 = constant_width;
        auto flush = [&]() {
            if (!open) return;
            e->add(to_world.transform_point(Vec3{pos.x, pos.y, elevation}), bulge, w0, w1);
            bulge = 0.0;
            w0 = constant_width;
            w1 = constant_width;
        };

        for (const DxfGroup& x : g.groups) {
            switch (x.code) {
                case 10:
                    flush();
                    open = true;
                    pos = Vec3{to_double(x.value), 0.0, 0.0};
                    break;
                case 20: pos.y = to_double(x.value); break;
                case 40: w0 = to_double(x.value); break;
                case 41: w1 = to_double(x.value); break;
                case 42: bulge = to_double(x.value); break;
                default: break;
            }
        }
        flush();
        return e;
    }

    if (g.name == "SPLINE") {
        const int degree = static_cast<int>(g.real(71, 3.0));

        std::vector<Vec3> control;
        std::vector<Vec3> fit;
        std::vector<double> knots;
        std::vector<double> weights;

        // Control points, fit points, knots and weights are all repeated codes,
        // and a control point's three coordinates arrive as separate groups.
        Vec3 c{};
        Vec3 f{};
        bool has_c = false;
        bool has_f = false;
        for (const DxfGroup& x : g.groups) {
            switch (x.code) {
                case 10:
                    if (has_c) control.push_back(c);
                    c = Vec3{to_double(x.value), 0.0, 0.0};
                    has_c = true;
                    break;
                case 20: c.y = to_double(x.value); break;
                case 30: c.z = to_double(x.value); break;
                case 11:
                    if (has_f) fit.push_back(f);
                    f = Vec3{to_double(x.value), 0.0, 0.0};
                    has_f = true;
                    break;
                case 21: f.y = to_double(x.value); break;
                case 31: f.z = to_double(x.value); break;
                case 40: knots.push_back(to_double(x.value)); break;
                case 41: weights.push_back(to_double(x.value)); break;
                default: break;
            }
        }
        if (has_c) control.push_back(c);
        if (has_f) fit.push_back(f);

        // A rational spline carries one weight per control point; anything else
        // is a file saying it is rational without saying how, and a partial
        // weight list would silently distort the curve. Dropping them makes it
        // uniform, which is at least a curve the control points describe.
        if (!weights.empty() && weights.size() != control.size()) weights.clear();

        auto e = std::make_unique<Spline>(degree, std::move(control), std::move(knots),
                                          std::move(weights), g.normal());
        apply_common(*e, g);
        e->set_fit_points(std::move(fit));

        // An unusable spline becomes a Proxy rather than a broken entity: the
        // groups survive untouched and the drawing is not quietly damaged.
        if (!e->valid()) {
            auto p = std::make_unique<Proxy>();
            p->set_dxf_name(g.name);
            for (const DxfGroup& x : g.groups) p->add_group(x.code, x.value);
            ++result_.proxies;
            return p;
        }
        return e;
    }
    if (g.name == "POINT") {
        auto e = std::make_unique<PointEntity>();
        apply_common(*e, g);
        e->set_position(ecs_to_world(e->props().normal).transform_point(g.point(10)));
        return e;
    }
    if (g.name == "SOLID" || g.name == "TRACE") {
        // TRACE is a SOLID with width semantics R12 draws differently and this
        // program does not; holding it as a SOLID keeps the geometry rather
        // than proxying a shape it could otherwise edit.
        auto e = std::make_unique<Face>(EntityType::Solid);
        apply_common(*e, g);
        const Mat4 to_world = ecs_to_world(e->props().normal);
        for (int i = 0; i < 4; ++i) e->set_corner(i, to_world.transform_point(g.point(10 + i)));
        return e;
    }
    if (g.name == "3DFACE") {
        auto e = std::make_unique<Face>(EntityType::Face3d);
        apply_common(*e, g);
        // World coordinates throughout, unlike SOLID.
        for (int i = 0; i < 4; ++i) e->set_corner(i, g.point(10 + i));
        e->set_edge_flags(static_cast<std::int16_t>(to_int(g.text(70, "0"))));
        return e;
    }
    if (g.name == "TEXT") {
        auto e = std::make_unique<Text>();
        apply_common(*e, g);
        e->set_position(ecs_to_world(e->props().normal).transform_point(g.point(10)));
        e->set_value(g.text(1));
        e->set_height(g.real(40, 1.0));
        e->set_rotation(g.real(50) * kDegToRad);
        e->set_width_factor(g.real(41, 1.0));
        e->set_oblique(g.real(51) * kDegToRad);

        // Justification. The group values are the enumerator values, but the
        // file is not trusted to stay in range -- anything else falls back to
        // the default rather than becoming an enumerator that does not exist.
        const int h = to_int(g.text(72, "0"));
        const int v = to_int(g.text(73, "0"));
        e->set_align(h >= 0 && h <= 5 ? static_cast<TextHAlign>(h) : TextHAlign::Left,
                     v >= 0 && v <= 3 ? static_cast<TextVAlign>(v) : TextVAlign::Baseline);
        if (e->is_justified()) {
            e->set_align_point(ecs_to_world(e->props().normal).transform_point(g.point(11)));
        }
        return e;
    }

    if (g.name == "INSERT") {
        auto e = std::make_unique<Insert>();
        apply_common(*e, g);

        // The definition is not resolved here: the block may be defined later
        // in the file. Everything needed to finish the job is recorded and the
        // placement is composed once the base point is known -- which is the
        // definition's, so it cannot be computed before resolution either.
        InsertPlacement p;
        p.normal = e->props().normal;
        p.insertion = ecs_to_world(p.normal).transform_point(g.point(10));
        p.scale = {g.real(41, 1.0), g.real(42, 1.0), g.real(43, 1.0)};
        p.rotation = g.real(50) * kDegToRad;

        // MINSERT: the same record with counts on it.
        const int columns = to_int(g.text(70, "1"));
        const int rows = to_int(g.text(71, "1"));
        if (rows > 1 || columns > 1) {
            e->set_array(static_cast<std::int16_t>(rows), static_cast<std::int16_t>(columns),
                         g.real(45, 0.0), g.real(44, 0.0));
        }

        pending_inserts_.push_back({e.get(), g.text(2), p});
        return e;
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
        } else if (table == "UCS") {
            Ucs u;
            u.origin = current.point(10);
            u.xdir = current.point(11);
            u.ydir = current.point(12);
            db_.add_ucs(name, u);
            ++result_.coordinate_systems;
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
            in_entry = (value == "LAYER" || value == "LTYPE" || value == "UCS");
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

void Reader::read_blocks(GroupStream& in) {
    int code = 0;
    std::string value;
    bool has_pending = false;
    int pending_code = 0;
    std::string pending_value;

    // The block being built, and the entity record being collected inside it.
    BlockDef def;
    bool in_block = false;

    EntityGroups current;
    bool have_entity = false;
    bool in_header = false;  // collecting the BLOCK record's own groups

    auto finish_entity = [&](GroupStream& stream) {
        have_entity = false;
        EntityPtr e = build(current, stream, pending_code, pending_value, has_pending);
        if (!e) return;
        if (in_block) {
            // Into the definition, not the drawing: a block's contents are not
            // entities of the drawing and must not get handles or an order.
            def.entities.push_back(std::move(e));
        }
    };

    for (;;) {
        if (has_pending) {
            code = pending_code;
            value = pending_value;
            has_pending = false;
        } else if (!in.next(code, value)) {
            break;
        }

        if (code != 0) {
            if (have_entity) current.groups.push_back({code, value});
            else if (in_header) current.groups.push_back({code, value});
            continue;
        }

        // A zero group ends whatever was being collected.
        if (have_entity) {
            pending_code = code;
            pending_value = value;
            has_pending = true;
            finish_entity(in);
            continue;
        }
        if (in_header) {
            in_header = false;
            def.name = current.text(2);
            def.base = current.point(10);
            def.flags = static_cast<std::int16_t>(to_int(current.text(70, "0")));
        }

        if (value == "BLOCK") {
            def = BlockDef{};
            in_block = true;
            current = EntityGroups{};
            current.name = value;
            in_header = true;
            continue;
        }
        if (value == "ENDBLK") {
            if (in_block && !def.name.empty()) {
                db_.add_block(std::move(def));
                ++result_.blocks;
            }
            def = BlockDef{};
            in_block = false;
            continue;
        }
        if (value == "ENDSEC") return;

        // Anything else inside a BLOCK is one of its entities.
        current = EntityGroups{};
        current.name = value;
        have_entity = true;
    }

    // A file that ends mid-section still yields the definitions it completed.
}

void Reader::resolve_inserts() {
    for (const PendingInsert& pending : pending_inserts_) {
        if (!pending.entity) continue;

        const BlockId id = db_.find_block(pending.block_name);
        const BlockDef* def = db_.block(id);
        if (!def) {
            // An INSERT naming a block the file never defined. Left with no
            // definition, which draws and snaps as nothing rather than
            // pretending -- and counted, so OPEN can say so instead of the
            // drawing quietly coming up short.
            ++result_.unresolved_inserts;
            continue;
        }

        pending.entity->set_definition(def);
        // Composed now, because the base point it is measured from belongs to
        // the definition and was not available when the record was read.
        pending.entity->set_placement(compose_placement(pending.placement, def->base));
    }
    pending_inserts_.clear();
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
            } else if (section == "BLOCKS") {
                read_blocks(in);
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

        // The current UCS. The rest of the HEADER section is still ignored --
        // see SF_todo.md -- but a drawing saved in a tilted construction plane
        // that reopens in world XY is a change to what typing a coordinate
        // means, which is too surprising to leave.
        if (code == 9 && (value == "$UCSORG" || value == "$UCSXDIR" || value == "$UCSYDIR")) {
            const std::string which = value;
            Vec3 p{};
            // Three coordinate groups follow, in order.
            for (int i = 0; i < 3; ++i) {
                if (!in.next(code, value)) break;
                const double d = to_double(value);
                if (code == 10) p.x = d;
                else if (code == 20) p.y = d;
                else if (code == 30) p.z = d;
            }
            if (which == "$UCSORG") header_ucs_.origin = p;
            else if (which == "$UCSXDIR") header_ucs_.xdir = p;
            else header_ucs_.ydir = p;
            have_header_ucs_ = true;
            continue;
        }
        if (code == 9 && value == "$UCSNAME") {
            if (in.next(code, value) && code == 2) header_ucs_name_ = value;
            continue;
        }
    }

    resolve_inserts();
    // Applied after the tables, so that a named current UCS is set from the
    // header rather than from whichever table entry happened to be read last.
    if (have_header_ucs_) db_.set_current_ucs(header_ucs_, header_ucs_name_);
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
