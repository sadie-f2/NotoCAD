// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/dxf_read.hpp"

#include "ncad/database.hpp"
#include "ncad/ecs.hpp"
#include "ncad/entities.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <cstdlib>
#include <fstream>
#include <numbers>
#include <sstream>
#include <vector>

namespace ncad {
namespace {

constexpr double kDegToRad = std::numbers::pi / 180.0;

// A DXF file is pairs of lines: a group code, then its value. Whitespace around
// the code is ignored; the value is taken whole, because layer names and text
// strings may contain spaces and leading ones are meaningful.
class GroupStream {
public:
    explicit GroupStream(std::string text) : text_(std::move(text)) {}

    bool next(int& code, std::string& value) {
        if (has_pending_) {
            code = pending_code_;
            value = pending_value_;
            has_pending_ = false;
            return true;
        }

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

    // One slot of pushback, for a reader that has to LOOK at the next group to
    // know whether it wants it. Enough for that: nothing here needs two.
    void unget(int code, const std::string& value) {
        pending_code_ = code;
        pending_value_ = value;
        has_pending_ = true;
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

    int pending_code_{0};
    std::string pending_value_;
    bool has_pending_{false};
};

// A DXF is untrusted input, and strtod happily parses "nan", "-inf" and
// "1e999". Nothing downstream rejected them: a NaN coordinate propagates
// through BBox::expand into an invalid box, so the entity becomes unpickable
// and invisible to ZOOM EXTENTS, and normalize() hands back the zero vector for
// a NaN normal so the arbitrary axis algorithm quietly uses the world basis.
// Worse, it round-tripped -- `nan` went in and `nan` came back out of DXFOUT,
// which would hand AutoCAD a file this project's whole interchange guarantee
// says it should not.
//
// Zero is the honest substitute: it is what an absent group would have given,
// and it is a value the rest of the kernel is built to survive.
double to_double(const std::string& s) {
    const double v = std::strtod(s.c_str(), nullptr);
    return std::isfinite(v) ? v : 0.0;
}
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
    Reader(Database& db, DxfReadMode mode) : db_(db), mode_(mode) {}

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
    // The raw pointer is valid only for as long as SOMETHING OWNS THE ENTITY,
    // and in the BLOCKS section that is not a given: an entity outside a block
    // is dropped, and a block abandoned without ENDBLK -- by EOF, by ENDSEC, or
    // by a BLOCK record with no name -- takes its entities with it. This
    // comment used to claim the pointers simply stay valid; four malformed
    // files proved otherwise, each a heap-use-after-free in resolve_inserts.
    //
    // So every path that destroys an entity calls forget_pending first.
    struct PendingInsert {
        Insert* entity{nullptr};
        std::string block_name;
        InsertPlacement placement;
        // The block this insert is INSIDE, empty for model space. Recorded so
        // that a cycle can be recognised: an edge from `owner` to `block_name`
        // that closes a loop is one this drawing must not hold. See
        // break_block_cycles.
        std::string owner;
    };

    void resolve_inserts();

    // Drop any pending registration pointing at `e`, or at anything `def`
    // owns, because it is about to be destroyed. Cheap in practice: a file has
    // far fewer inserts than the loop below would suggest, and correctness here
    // is worth more than the scan.
    void forget_pending(const Entity* e);
    void forget_pending(const BlockDef& def);

    // Refuses any INSERT whose definition would close a loop. A DXF is data
    // from elsewhere and may claim a cycle; the kernel's depth guard bounds how
    // DEEP a traversal goes but not how much WORK it does, so a block holding
    // two insertions of itself is 2^32 traversals at depth 32 rather than a
    // stack overflow. Cheaper and more honest to refuse it at the door.
    void break_block_cycles();

    // The block currently being collected, so a nested INSERT knows its owner.
    std::string collecting_block_;

    // Name -> id, case-folded, for the two lookups the reader does PER RECORD.
    // Database::find_layer and find_block are linear scans -- fine for a
    // command, quadratic for a file: 500k entities against 10k layers is ~5e9
    // string compares at load, with no progress and no cancel.
    //
    // Cached here rather than indexed in Database on purpose. The database's
    // tables are edited by commands and rewound by undo, and an index inside it
    // would be a second thing to keep true across all of that. A reader lives
    // for one file and then dies, so its cache cannot go stale.
    std::unordered_map<std::string, LayerId> layer_cache_;
    std::unordered_map<std::string, BlockId> block_cache_;

    static std::string folded(const std::string& name) {
        std::string out(name);
        for (char& c : out) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        }
        return out;
    }

    LayerId layer_for(const std::string& name);
    BlockId block_for(const std::string& name);

    Database& db_;
    DxfReadMode mode_{DxfReadMode::Replace};
    DxfReadResult result_;
    std::vector<PendingInsert> pending_inserts_;

    // The current UCS from the HEADER, applied once the whole file is read.
    Ucs header_ucs_;
    std::string header_ucs_name_;
    bool have_header_ucs_{false};
};

void Reader::apply_common(Entity& e, const EntityGroups& g) {
    const LayerId id = layer_for(g.text(8, "0"));
    e.props().layer = id;

    const std::string ltype = g.text(6);
    if (!ltype.empty() && ltype != "BYLAYER") {
        const LinetypeId lt = db_.find_linetype(ltype);
        if (lt != kInvalidLinetype) e.props().linetype = lt;
    }
    if (g.has(62)) {
        // Clamped to the range AutoCAD actually uses: 0 is BYBLOCK, 256 is
        // BYLAYER, 1..255 are the palette, and a negative value is the same
        // colour on a layer that is switched off. Anything else is a corrupt
        // file, and 32768 in particular is a weapon -- negating it in an
        // int16_t gives back -32768, which walks past the front of the palette
        // table in the renderer. Guarded there too; this is the door it came in
        // through.
        const int raw = to_int(g.text(62));
        e.props().color = static_cast<std::int16_t>(std::clamp(raw, -256, 256));
    }
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

        pending_inserts_.push_back({e.get(), g.text(2), p, collecting_block_});
        return e;
    }

    if (g.name == "DIMENSION") {
        auto e = std::make_unique<Dimension>();
        apply_common(*e, g);

        // Group 70's low bits are the kind; the rest are flags about how the
        // record was built, and none of them change what is measured.
        const int flags = to_int(g.text(70, "0"));
        switch (flags & 7) {
            case 1: e->set_kind(DimKind::Aligned); break;
            case 3: e->set_kind(DimKind::Diameter); break;
            case 4: e->set_kind(DimKind::Radius); break;
            // Type 2 is the two-line form and 5 the three-point one. Both are
            // read into the three-point representation, which can express
            // either -- a two-line angular's vertex is where its arms cross.
            case 2:
            case 5: e->set_kind(DimKind::Angular); break;
            default: e->set_kind(DimKind::Linear); break;
        }

        e->set_definition(g.point(10));
        if (e->angular()) {
            e->set_points(g.point(13), g.point(14));
            e->set_vertex(g.point(15));
        } else if (e->radial()) {
            // Group 15 is where the leader meets the curve.
            e->set_points(g.point(15), Vec3{});
        } else {
            e->set_points(g.point(13), g.point(14));
            e->set_rotation(g.real(50) * kDegToRad);
        }
        if (!g.text(1).empty()) e->set_text_override(g.text(1));

        // The style comes from the header, which the HEADER pass has already
        // read into the sysvars -- the entity carries only what it measures.
        const Sysvars& sv = db_.sysvars();
        const double s = sv.get_real(Sysvar::DimScale);
        e->apply_style(sv.get_real(Sysvar::DimTxt) * s, sv.get_real(Sysvar::DimAsz) * s,
                       sv.get_real(Sysvar::DimExo) * s, sv.get_real(Sysvar::DimExe) * s);
        e->set_text_horizontal(sv.get_int(Sysvar::DimTih) != 0);

        // THE BLOCK IS IGNORED, deliberately. A dimension generates its own
        // geometry here, so the `*D<n>` block the file carries is redundant and
        // keeping it would mean the drawing held the measurement twice, able to
        // disagree. AutoCAD regenerates for the same reason. What this costs is
        // another program's styling, which we could not have reproduced anyway.
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
            return;
        }
        // Outside a block, and so about to be destroyed unowned. An INSERT here
        // has already registered itself in pending_inserts_.
        forget_pending(e.get());
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
            collecting_block_ = def.name;
            def.base = current.point(10);
            def.flags = static_cast<std::int16_t>(to_int(current.text(70, "0")));
        }

        if (value == "BLOCK") {
            // A previous BLOCK that never reached ENDBLK is abandoned here.
            forget_pending(def);
            def = BlockDef{};
            collecting_block_.clear();
            in_block = true;
            current = EntityGroups{};
            current.name = value;
            in_header = true;
            continue;
        }
        if (value == "ENDBLK") {
            if (in_block && !def.name.empty()) {
                // Redefinition is R12's behaviour and add_block implements it
                // by rewriting the existing definition IN PLACE -- which
                // destroys the entities that definition held. Any INSERT among
                // them is registered here, so it has to be forgotten first.
                // A file with two same-named blocks is not exotic; xref
                // flattening emits them.
                if (const BlockDef* previous = db_.block(db_.find_block(def.name))) {
                    forget_pending(*previous);
                }
                db_.add_block(std::move(def));
                ++result_.blocks;
                // The cache records MISSES too, and a name that missed before
                // this block was defined must not go on missing. Today every
                // block_for call happens after the BLOCKS section is finished,
                // so this cannot bite -- it is here so that it still cannot if
                // that stops being true.
                block_cache_.clear();
            } else {
                // A BLOCK with no group 2 has nowhere to go, and its entities
                // die with `def` on the next line.
                forget_pending(def);
            }
            def = BlockDef{};
            in_block = false;
            collecting_block_.clear();
            continue;
        }
        if (value == "ENDSEC") {
            // Inside a block still: `def` is destroyed on the way out.
            forget_pending(def);
            return;
        }

        // Anything else inside a BLOCK is one of its entities.
        current = EntityGroups{};
        current.name = value;
        have_entity = true;
    }

    // A file that ends mid-section still yields the definitions it completed --
    // and whatever was half-collected when the input ran out is destroyed with
    // `def`, so nothing may still be pointing into it.
    forget_pending(def);
}

void Reader::forget_pending(const Entity* e) {
    if (e == nullptr) return;
    for (PendingInsert& pending : pending_inserts_) {
        if (pending.entity == e) pending.entity = nullptr;
    }
}

void Reader::forget_pending(const BlockDef& def) {
    for (const EntityPtr& owned : def.entities) forget_pending(owned.get());
}

LayerId Reader::layer_for(const std::string& name) {
    const std::string key = folded(name);
    const auto it = layer_cache_.find(key);
    if (it != layer_cache_.end()) return it->second;

    LayerId id = db_.find_layer(name);
    // A file may name a layer its own table forgot to define. Creating it is
    // better than dropping the entity onto layer 0 and losing the name.
    if (id == kInvalidLayer) id = db_.add_layer(name);
    layer_cache_.emplace(key, id);
    return id;
}

BlockId Reader::block_for(const std::string& name) {
    const std::string key = folded(name);
    const auto it = block_cache_.find(key);
    if (it != block_cache_.end()) return it->second;

    const BlockId id = db_.find_block(name);
    // A miss is cached too. An INSERT naming a block the file never defines is
    // often repeated many times, and re-scanning the whole table for each of
    // them is the case this exists to avoid.
    block_cache_.emplace(key, id);
    return id;
}

void Reader::break_block_cycles() {
    // The edges are exactly the pending inserts that live inside a block:
    // owner -> target. A model-space insert cannot be part of a cycle, because
    // nothing inserts model space.
    //
    // Keyed on BlockId rather than on the name, so the database's own
    // case-insensitive lookup decides what "the same block" means and this does
    // not grow a second opinion about it.
    struct Edge {
        BlockId from{kInvalidBlock};
        BlockId to{kInvalidBlock};
        Insert* entity{nullptr};
    };

    std::vector<Edge> edges;
    for (const PendingInsert& pending : pending_inserts_) {
        if (pending.entity == nullptr || pending.owner.empty()) continue;
        if (pending.entity->definition() == nullptr) continue;
        const BlockId from = block_for(pending.owner);
        const BlockId to = block_for(pending.block_name);
        if (from == kInvalidBlock || to == kInvalidBlock) continue;
        edges.push_back({from, to, pending.entity});
    }

    // For each edge, can the target reach the owner again? If so this insert is
    // what closes the loop, and cutting it costs a drawing one insertion rather
    // than a viewport that never comes back.
    for (Edge& edge : edges) {
        std::vector<BlockId> stack{edge.to};
        std::vector<BlockId> seen;
        bool loops = false;

        while (!stack.empty()) {
            const BlockId here = stack.back();
            stack.pop_back();
            if (here == edge.from) {
                loops = true;
                break;
            }
            if (std::find(seen.begin(), seen.end(), here) != seen.end()) continue;
            seen.push_back(here);
            for (const Edge& next : edges) {
                if (next.from == here && next.entity->definition() != nullptr) {
                    stack.push_back(next.to);
                }
            }
        }

        if (loops) {
            edge.entity->set_definition(nullptr);
            ++result_.cyclic_inserts;
        }
    }
}

void Reader::resolve_inserts() {
    for (const PendingInsert& pending : pending_inserts_) {
        if (!pending.entity) continue;

        const BlockId id = block_for(pending.block_name);
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
    break_block_cycles();
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

        // The dimension style. A dimension records only what it measures, so
        // without these the sizes it is redrawn at would come from this
        // reader's defaults rather than from the drawing.
        if (code == 9 && value.size() > 4 && value.compare(0, 4, "$DIM") == 0) {
            const std::string which = value.substr(1);
            if (in.next(code, value)) {
                const SysvarDef* def = find_sysvar(which);
                if (def != nullptr) {
                    if (def->type == SysvarType::Real) {
                        db_.sysvars().set_real(def->id, to_double(value));
                    } else if (def->type == SysvarType::Int) {
                        db_.sysvars().set_int(def->id, to_int(value));
                    }
                }
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
            // Up to three coordinate groups follow -- and only as many as are
            // actually there. Taking three unconditionally meant a short
            // $UCSORG swallowed whatever came after it, and what usually comes
            // after a header variable is the 0/SECTION pair that opens
            // ENTITIES. The whole section was then skipped and the file loaded
            // clean and empty: silent data loss, no error, from a file only
            // slightly malformed.
            for (int i = 0; i < 3; ++i) {
                if (!in.next(code, value)) break;
                if (code != 10 && code != 20 && code != 30) {
                    // Not ours. Hand it back to the loop that knows what it is.
                    in.unget(code, value);
                    break;
                }
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
    //
    // Not on a merge: the current UCS belongs to the drawing being imported
    // INTO. Letting an imported file reset it would move the construction plane
    // out from under whatever the user was doing, which is the sort of thing
    // DXFIN has no business doing to a drawing already in progress.
    if (have_header_ucs_ && mode_ == DxfReadMode::Replace) {
        db_.set_current_ucs(header_ucs_, header_ucs_name_);
    }
    result_.ok = true;
    return result_;
}

}  // namespace

DxfReadResult read_dxf_text(Database& db, const std::string& text, DxfReadMode mode) {
    if (mode == DxfReadMode::Replace) {
        db.clear();
        db.journal().clear();
    }

    Reader r(db, mode);

    // Suppressed for a Replace, because loading a drawing is not an edit and
    // the journal is about to be cleared anyway. Recording it meant a full
    // clone of every entity in the file, allocated and then thrown away -- see
    // UndoJournal::SuppressRecording.
    //
    // A merge records normally: DXFIN IS an edit, made to a drawing whose
    // history is worth keeping, and the caller's command group makes the whole
    // import one undoable step.
    DxfReadResult result;
    if (mode == DxfReadMode::Replace) {
        UndoJournal::SuppressRecording quiet(db.journal());
        result = r.run(text);
    } else {
        result = r.run(text);
    }

    // A freshly opened drawing has no history: undoing past the load is not
    // meaningful, and the load itself is not an edit.
    //
    // A merge is the opposite on both counts. It IS an edit, made to a drawing
    // with a history worth keeping, so the journal is left alone and the
    // caller's command group makes the whole import one undoable step.
    if (mode == DxfReadMode::Replace) db.journal().clear();
    return result;
}

DxfReadResult read_dxf_file(Database& db, const std::string& path, DxfReadMode mode) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        DxfReadResult r;
        r.error = "cannot open " + path;
        return r;
    }
    // Read straight into ONE buffer. The obvious version -- an ostringstream
    // fed from rdbuf, then `.str()` -- costs the file's size TWICE over, since
    // `.str()` returns a copy, plus whatever slack the stream's doubling left.
    // Measured on a 2.1 GB drawing that was most of 7.8 GB resident before a
    // single entity existed, which is enough to put a machine into swap.
    //
    // Binary rather than text: identical on the platforms this builds for, and
    // it stops a Windows build reading fewer bytes than it sized the buffer
    // for. `gcount` then trims whatever was actually delivered.
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string text;
    if (size > 0) {
        text.resize(static_cast<std::size_t>(size));
        file.read(text.data(), size);
        text.resize(static_cast<std::size_t>(file.gcount()));
    }
    return read_dxf_text(db, text, mode);
}

}  // namespace ncad
