// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// entmake / entget / entmod / entdel / entlast / entnext -- the boundary where
// AutoLISP association lists become entities and back again.
//
// This is the project's declared hot path: "alist of dotted pairs -> entity
// struct", at tens of thousands of faces.
//
// Two conventions here are easy to get wrong and expensive to discover late:
//
// 1. COORDINATES. Group 10 arrives in the *entity coordinate system* for CIRCLE
//    and ARC, exactly as DXF stores them, and must be carried into world space
//    through the group-210 extrusion vector. LINE is the exception: R12 keeps
//    both its endpoints in world coordinates. This mirrors entities_dxf.cpp,
//    because AutoLISP's entity lists and the DXF file agree on this by design.
//
// 2. ANGLES. AutoLISP hands arc angles to LISP in RADIANS, even though the DXF
//    file on disk stores degrees. So there is no conversion here, and there is
//    one in the DXF writer. That asymmetry is real, not an oversight.
//
// Error policy: an entity type we do not implement yet returns nil, which is a
// condition AutoLISP code tests for. Malformed data -- a missing group 10, a
// radius that is a string -- raises an error instead. Silently returning nil at
// face twenty thousand of a mesh build is not a diagnosis.
#include "entity_subrs.hpp"

#include "ncad/database.hpp"
#include "ncad/ecs.hpp"
#include "ncad/entities.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace ncad::lisp {
namespace {

// --- alist reading ----------------------------------------------------------

// Returns the cdr of the (code . value) pair, or nil with found=false.
Value alist_get(const Value& alist, std::int32_t code, bool& found) {
    for (Value cur = alist; is_cons(cur); cur = cdr(cur)) {
        const Value pair = car(cur);
        if (is_cons(pair) && car(pair).type == Type::Int && car(pair).i == code) {
            found = true;
            return cdr(pair);
        }
    }
    found = false;
    return make_nil();
}

// Every value carried under `code`, in the order written.
//
// alist_get returns the FIRST match, which is right for every entity that came
// before: a circle has one group 40 and asking for a second would be a mistake.
// A spline has one group 40 per KNOT and one group 10 per control point, so the
// distinction is real and this is the accessor for it.
std::vector<Value> alist_all(const Value& alist, std::int32_t code) {
    std::vector<Value> out;
    for (Value cur = alist; is_cons(cur); cur = cdr(cur)) {
        const Value pair = car(cur);
        if (is_cons(pair) && car(pair).type == Type::Int && car(pair).i == code) {
            out.push_back(cdr(pair));
        }
    }
    return out;
}

std::string upcase(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

bool group_error(Interp& in, std::int32_t code, const char* what) {
    return in.fail(EvalStatus::BadArgumentType,
                   "group " + std::to_string(code) + ": " + what);
}

// A coordinate group's value is a bare list of 2 or 3 reals: (10 1.0 2.0 0.0).
bool parse_point(Interp& in, std::int32_t code, const Value& v, Vec3& out) {
    double c[3] = {0.0, 0.0, 0.0};
    std::size_t i = 0;
    for (Value cur = v; is_cons(cur); cur = cdr(cur)) {
        if (i >= 3) return group_error(in, code, "more than three coordinates");
        const Value n = car(cur);
        if (!is_number(n)) return group_error(in, code, "coordinate is not a number");
        c[i++] = as_double(n);
    }
    // Two coordinates is legal; R12 treats the point as lying at Z = 0.
    if (i < 2) return group_error(in, code, "needs at least two coordinates");
    out = Vec3{c[0], c[1], c[2]};
    return true;
}

bool parse_real(Interp& in, std::int32_t code, const Value& v, double& out) {
    if (!is_number(v)) return group_error(in, code, "not a number");
    out = as_double(v);
    return true;
}

// --- alist -> entity --------------------------------------------------------

// Applies the properties every R12 entity shares. The layer is created if it
// does not exist, which diverges from AutoCAD (where entmake fails); for a tool
// whose purpose is generating geometry from analysis data, having to declare
// layers up front is friction with no safety benefit.
bool apply_common(Interp& in, Database& db, const Value& alist, Entity& ent,
                  const Vec3& normal) {
    bool found = false;

    const Value layer = alist_get(alist, 8, found);
    if (found) {
        if (layer.type != Type::Str) return group_error(in, 8, "layer name is not a string");
        ent.props().layer = db.add_layer(upcase(layer.str->view()));
    }

    const Value ltype = alist_get(alist, 6, found);
    if (found) {
        if (ltype.type != Type::Str) return group_error(in, 6, "linetype name is not a string");
        const std::string name = upcase(ltype.str->view());
        const LinetypeId id = db.find_linetype(name);
        // A linetype cannot be invented: it needs a dash pattern.
        if (id == kInvalidLinetype) {
            return in.fail(EvalStatus::BadArgumentType, "group 6: no such linetype: " + name);
        }
        ent.props().linetype = id;
    }

    const Value color = alist_get(alist, 62, found);
    if (found) {
        if (color.type != Type::Int) return group_error(in, 62, "colour is not an integer");
        ent.props().color = static_cast<std::int16_t>(color.i);
    }

    const Value thickness = alist_get(alist, 39, found);
    if (found) {
        double t = 0.0;
        if (!parse_real(in, 39, thickness, t)) return false;
        ent.props().thickness = t;
    }

    ent.props().normal = normal;
    return true;
}

// Builds an entity from an association list. `unsupported` distinguishes "this
// entity kind is not implemented" from "the data was wrong".
bool build_entity(Interp& in, Database& db, const Value& alist, EntityPtr& out,
                  bool& unsupported) {
    unsupported = false;
    if (!is_cons(alist)) {
        return in.fail(EvalStatus::BadArgumentType, "entity data is not a list");
    }

    bool found = false;
    const Value type_v = alist_get(alist, 0, found);
    if (!found) return in.fail(EvalStatus::BadArgumentType, "missing group 0 (entity type)");
    if (type_v.type != Type::Str) return group_error(in, 0, "entity type is not a string");
    const std::string type = upcase(type_v.str->view());

    // The extrusion vector, which defines the entity coordinate system.
    Vec3 normal = kWorldZ;
    const Value ext = alist_get(alist, 210, found);
    if (found) {
        if (!parse_point(in, 210, ext, normal)) return false;
        if (is_zero(normal)) return group_error(in, 210, "extrusion vector is zero");
        normal = normalize(normal);
    }

    const Value p10 = alist_get(alist, 10, found);
    const bool has_p10 = found;
    Vec3 point10{};
    if (has_p10 && !parse_point(in, 10, p10, point10)) return false;

    if (type == "LINE") {
        if (!has_p10) return in.fail(EvalStatus::BadArgumentType, "LINE: missing group 10");
        const Value p11 = alist_get(alist, 11, found);
        if (!found) return in.fail(EvalStatus::BadArgumentType, "LINE: missing group 11");
        Vec3 point11{};
        if (!parse_point(in, 11, p11, point11)) return false;
        // Both endpoints are already world coordinates; LINE is the exception.
        out = std::make_unique<Line>(point10, point11);

    } else if (type == "SPLINE") {
        // Two ways to describe one, and R13's group codes carry both:
        // control points with knots (10 and 40), or fit points to interpolate
        // through (11). Fit points win when present, because they are what the
        // caller actually meant and solving from them cannot disagree with
        // itself the way a hand-written knot vector can.
        const std::vector<Value> fit_raw = alist_all(alist, 11);
        const std::vector<Value> ctrl_raw = alist_all(alist, 10);

        const Value v71 = alist_get(alist, 71, found);
        double degree_real = 3.0;
        if (found && !parse_real(in, 71, v71, degree_real)) return false;
        int degree = static_cast<int>(degree_real);
        if (degree < 1) return group_error(in, 71, "degree must be at least 1");
        // Reported rather than clamped: a script asking for degree 30 and
        // silently receiving 15 gets a different curve than it wrote, which is
        // worse than being told no. `interpolating` clamps as well, because
        // the bound protects its stack scratch and must hold for every caller
        // rather than only this one.
        if (degree > kMaxSplineDegree) {
            const std::string what = "degree is at most " + std::to_string(kMaxSplineDegree);
            return group_error(in, 71, what.c_str());
        }

        const Mat4 to_world = ecs_to_world(normal);

        if (!fit_raw.empty()) {
            std::vector<Vec3> fit;
            fit.reserve(fit_raw.size());
            for (const Value& v : fit_raw) {
                Vec3 p{};
                if (!parse_point(in, 11, v, p)) return false;
                fit.push_back(to_world.transform_point(p));
            }
            if (fit.size() < 2) return group_error(in, 11, "a spline needs at least two points");

            out = Spline::interpolating(fit, degree, normal);
            if (!out) return group_error(in, 11, "those points do not describe a curve");

        } else {
            if (ctrl_raw.size() < 2) {
                return in.fail(EvalStatus::BadArgumentType,
                               "SPLINE: needs group 11 fit points or group 10 control points");
            }

            std::vector<Vec3> control;
            control.reserve(ctrl_raw.size());
            for (const Value& v : ctrl_raw) {
                Vec3 p{};
                if (!parse_point(in, 10, v, p)) return false;
                control.push_back(to_world.transform_point(p));
            }

            if (static_cast<std::size_t>(degree) + 1 > control.size()) {
                degree = static_cast<int>(control.size()) - 1;
            }

            std::vector<double> knots;
            for (const Value& v : alist_all(alist, 40)) {
                double k = 0.0;
                if (!parse_real(in, 40, v, k)) return false;
                knots.push_back(k);
            }

            // An omitted knot vector is filled in as a clamped uniform one,
            // which is the only knot vector most callers would have wanted and
            // is far easier to get right here than in LISP.
            if (knots.empty()) {
                const std::size_t n = control.size();
                const std::size_t p = static_cast<std::size_t>(degree);
                const std::size_t interior = n - p - 1;
                for (std::size_t i = 0; i <= p; ++i) knots.push_back(0.0);
                for (std::size_t i = 1; i <= interior; ++i) {
                    knots.push_back(static_cast<double>(i) / static_cast<double>(interior + 1));
                }
                for (std::size_t i = 0; i <= p; ++i) knots.push_back(1.0);
            }

            std::vector<double> weights;
            for (const Value& v : alist_all(alist, 41)) {
                double wt = 0.0;
                if (!parse_real(in, 41, v, wt)) return false;
                weights.push_back(wt);
            }
            if (!weights.empty() && weights.size() != control.size()) {
                return group_error(in, 41, "one weight per control point, or none");
            }

            auto sp = std::make_unique<Spline>(degree, std::move(control), std::move(knots),
                                               std::move(weights), normal);
            if (!sp->valid()) {
                return group_error(in, 40, "the knot count does not match the control points");
            }
            out = std::move(sp);
        }

    } else if (type == "ELLIPSE") {
        if (!has_p10) return in.fail(EvalStatus::BadArgumentType, "ELLIPSE: missing group 10");

        const Value v11 = alist_get(alist, 11, found);
        if (!found) return in.fail(EvalStatus::BadArgumentType, "ELLIPSE: missing group 11");
        Vec3 major{};
        if (!parse_point(in, 11, v11, major)) return false;

        const Value v40 = alist_get(alist, 40, found);
        if (!found) return in.fail(EvalStatus::BadArgumentType, "ELLIPSE: missing group 40");
        double ratio = 0.0;
        if (!parse_real(in, 40, v40, ratio)) return false;
        if (ratio <= 0.0 || ratio > 1.0) {
            return group_error(in, 40, "axis ratio must be in (0, 1]");
        }

        // Parameters are optional: leaving them out asks for a whole ellipse,
        // which is what most callers want and what R12 could only draw.
        double start = 0.0;
        double end = kFullTurn;
        const Value v41 = alist_get(alist, 41, found);
        if (found && !parse_real(in, 41, v41, start)) return false;
        const Value v42 = alist_get(alist, 42, found);
        if (found && !parse_real(in, 42, v42, end)) return false;

        const Mat4 to_world = ecs_to_world(normal);
        const Vec3 centre = to_world.transform_point(point10);
        const Vec3 axis = to_world.transform_vector(major);
        if (is_zero(axis)) return group_error(in, 11, "the major axis has no length");

        out = std::make_unique<Ellipse>(centre, axis, ratio, start, end, normal);

    } else if (type == "CIRCLE" || type == "ARC") {
        if (!has_p10) return in.fail(EvalStatus::BadArgumentType, type + ": missing group 10");
        const Value r = alist_get(alist, 40, found);
        if (!found) return in.fail(EvalStatus::BadArgumentType, type + ": missing group 40");
        double radius = 0.0;
        if (!parse_real(in, 40, r, radius)) return false;
        if (radius <= 0.0) return group_error(in, 40, "radius must be positive");

        // Group 10 is in the entity coordinate system; the kernel wants world.
        const Vec3 center = ecs_to_world(normal).transform_point(point10);

        if (type == "CIRCLE") {
            out = std::make_unique<Circle>(center, radius, normal);
        } else {
            const Value a50 = alist_get(alist, 50, found);
            const bool has50 = found;
            const Value a51 = alist_get(alist, 51, found);
            if (!has50 || !found) {
                return in.fail(EvalStatus::BadArgumentType, "ARC: missing group 50 or 51");
            }
            double start = 0.0;
            double end = 0.0;
            if (!parse_real(in, 50, a50, start)) return false;
            if (!parse_real(in, 51, a51, end)) return false;
            // Radians: AutoLISP's convention, not the DXF file's.
            out = std::make_unique<Arc>(center, radius, start, end, normal);
        }

    } else {
        // A kind we have not built yet. nil is the honest answer, and it is what
        // AutoLISP code tests for.
        unsupported = true;
        return true;
    }

    return apply_common(in, db, alist, *out, normal);
}

// --- entity -> alist --------------------------------------------------------

Value pair_int(Context& ctx, std::int32_t code, std::int32_t v) {
    return ctx.cons(make_int(code), make_int(v));
}

Value pair_real(Context& ctx, std::int32_t code, double v) {
    return ctx.cons(make_int(code), make_real(v));
}

Value pair_str(Context& ctx, std::int32_t code, std::string_view v) {
    return ctx.cons(make_int(code), make_str(ctx.new_string(v)));
}

Value pair_point(Context& ctx, std::int32_t code, const Vec3& p) {
    const Value coords[3] = {make_real(p.x), make_real(p.y), make_real(p.z)};
    return ctx.cons(make_int(code), ctx.list(coords, 3));
}

Value build_entity_alist(Context& ctx, const Database& db, const Entity& ent) {
    std::vector<Value> items;
    const EntityProps& props = ent.props();

    Value ename;
    ename.type = Type::Ename;
    ename.ename = ent.handle();
    items.push_back(ctx.cons(make_int(-1), ename));
    items.push_back(pair_str(ctx, 0, ent.type_name()));
    items.push_back(pair_str(ctx, 8, db.layer(props.layer).name));

    if (props.linetype != kLinetypeContinuous) {
        items.push_back(pair_str(ctx, 6, db.linetype(props.linetype).name));
    }
    if (props.color != kColorByLayer) {
        items.push_back(pair_int(ctx, 62, props.color));
    }
    if (props.thickness != 0.0) {
        items.push_back(pair_real(ctx, 39, props.thickness));
    }

    switch (ent.type()) {
        case EntityType::Line: {
            const Line& line = static_cast<const Line&>(ent);
            items.push_back(pair_point(ctx, 10, line.start()));
            items.push_back(pair_point(ctx, 11, line.end()));
            break;
        }
        case EntityType::Circle: {
            const Circle& circle = static_cast<const Circle&>(ent);
            const Mat4 to_ecs = world_to_ecs(props.normal);
            items.push_back(pair_point(ctx, 10, to_ecs.transform_point(circle.center())));
            items.push_back(pair_real(ctx, 40, circle.radius()));
            break;
        }
        case EntityType::Spline: {
            // R13's codes, and both descriptions when both exist. A caller that
            // made the curve from fit points gets those back and can hand them
            // straight to entmake; one reading a curve it did not author gets
            // the control points, which is the only complete description.
            const Spline& sp = static_cast<const Spline&>(ent);
            const Mat4 to_ecs = world_to_ecs(props.normal);

            items.push_back(pair_int(ctx, 71, sp.degree()));
            items.push_back(pair_int(ctx, 72, static_cast<std::int32_t>(sp.knots().size())));
            items.push_back(
                pair_int(ctx, 73, static_cast<std::int32_t>(sp.control_points().size())));
            items.push_back(
                pair_int(ctx, 74, static_cast<std::int32_t>(sp.fit_points().size())));

            for (double k : sp.knots()) items.push_back(pair_real(ctx, 40, k));
            for (double wt : sp.weights()) items.push_back(pair_real(ctx, 41, wt));
            for (const Vec3& c : sp.control_points()) {
                items.push_back(pair_point(ctx, 10, to_ecs.transform_point(c)));
            }
            for (const Vec3& f : sp.fit_points()) {
                items.push_back(pair_point(ctx, 11, to_ecs.transform_point(f)));
            }
            break;
        }
        case EntityType::MText: {
            // R13's codes, even though this writes to DXF as a run of TEXT
            // records. AutoLISP is not DXF: an MTEXT in the database is an
            // MTEXT, and handing LISP the degraded lines instead would make it
            // impossible to read back what was made -- the same reasoning the
            // ellipse case gives, and the same trap that was missed there.
            const MText& mt = static_cast<const MText&>(ent);
            const Mat4 to_ecs = world_to_ecs(props.normal);
            items.push_back(pair_point(ctx, 10, to_ecs.transform_point(mt.position())));
            items.push_back(pair_real(ctx, 40, mt.height()));
            items.push_back(pair_real(ctx, 41, mt.reference_width()));
            items.push_back(pair_int(ctx, 71, static_cast<std::int32_t>(mt.attach())));
            items.push_back(pair_real(ctx, 44, mt.line_spacing()));
            if (mt.rotation() != 0.0) items.push_back(pair_real(ctx, 50, mt.rotation()));
            // The RAW string, codes and all -- what the entity holds. Handing
            // over the laid-out lines would lose the formatting a caller may be
            // about to hand straight back to entmake.
            items.push_back(pair_str(ctx, 1, mt.text()));
            break;
        }
        case EntityType::Ellipse: {
            // R13's group codes, even though this writes to DXF as a polyline.
            // AutoLISP is not DXF: an ellipse in the database is an ellipse, and
            // handing LISP the approximation instead would make it impossible to
            // read back what was made -- which for a tool whose purpose is
            // LISP-driven geometry would be the wrong loss to take.
            const Ellipse& el = static_cast<const Ellipse&>(ent);
            const Mat4 to_ecs = world_to_ecs(props.normal);
            items.push_back(pair_point(ctx, 10, to_ecs.transform_point(el.center())));
            // Group 11 is a VECTOR from the centre, so it rotates without
            // translating -- transform_vector, not transform_point.
            items.push_back(pair_point(ctx, 11, to_ecs.transform_vector(el.major_axis())));
            items.push_back(pair_real(ctx, 40, el.ratio()));
            items.push_back(pair_real(ctx, 41, el.start_param()));
            items.push_back(pair_real(ctx, 42, el.end_param()));
            break;
        }
        case EntityType::Arc: {
            const Arc& arc = static_cast<const Arc&>(ent);
            const Mat4 to_ecs = world_to_ecs(props.normal);
            items.push_back(pair_point(ctx, 10, to_ecs.transform_point(arc.center())));
            items.push_back(pair_real(ctx, 40, arc.radius()));
            items.push_back(pair_real(ctx, 50, arc.start_angle()));
            items.push_back(pair_real(ctx, 51, arc.end_angle()));
            break;
        }
        case EntityType::Dimension: {
            // The site the impact list exists for: this switch has a default,
            // so a missing case compiles and fails silently -- which is exactly
            // how ELLIPSE was missed. What LISP is handed is what the entity
            // MEANS, not the line work it draws.
            const Dimension& dim = static_cast<const Dimension&>(ent);
            items.push_back(pair_point(ctx, 10, dim.definition()));
            items.push_back(pair_point(ctx, 13, dim.first()));
            if (!dim.radial()) items.push_back(pair_point(ctx, 14, dim.second()));
            items.push_back(pair_int(ctx, 70, static_cast<int>(dim.kind())));
            items.push_back(pair_real(ctx, 42, dim.measurement()));
            if (!dim.text_override().empty()) {
                items.push_back(pair_str(ctx, 1, dim.text_override()));
            }
            if (!dim.radial()) items.push_back(pair_real(ctx, 50, dim.rotation()));
            break;
        }
        default:
            break;
    }

    // Omitted when it is world Z, matching both the DXF writer and AutoCAD.
    if (!near_equal(props.normal, kWorldZ)) {
        items.push_back(pair_point(ctx, 210, props.normal));
    }
    return ctx.list(items.data(), items.size());
}

// --- shared preamble --------------------------------------------------------

Database* require_db(Interp& in, const char* who) {
    Database* db = in.database();
    if (!db) {
        in.fail(EvalStatus::BadArgumentType, std::string(who) + ": no drawing is attached");
    }
    return db;
}

bool require_ename(Interp& in, const char* who, const Value& v, Handle& out) {
    if (v.type != Type::Ename) {
        in.fail(EvalStatus::BadArgumentType,
                std::string(who) + ": not an entity name: " + prin1(v));
        return false;
    }
    out = v.ename;
    return true;
}

}  // namespace

// Public because ssget's filters test against exactly what entget returns --
// see entity_subrs.hpp. A thin forward rather than moving the builder out of
// the anonymous namespace, which would have meant relocating it past every
// helper it calls.
Value entity_to_alist(Context& ctx, const Database& db, const Entity& ent) {
    return build_entity_alist(ctx, db, ent);
}

// --- the builtins -----------------------------------------------------------

bool subr_entmake(Interp& in, const Value* a, std::size_t, Value& out) {
    Database* db = require_db(in, "entmake");
    if (!db) return false;

    EntityPtr ent;
    bool unsupported = false;
    if (!build_entity(in, *db, a[0], ent, unsupported)) return false;
    if (unsupported) {
        out = make_nil();
        return true;
    }

    db->add(std::move(ent));
    out = a[0];  // AutoLISP returns the entity list it was given
    return true;
}

bool subr_entget(Interp& in, const Value* a, std::size_t, Value& out) {
    Database* db = require_db(in, "entget");
    if (!db) return false;

    Handle h = kNullHandle;
    if (!require_ename(in, "entget", a[0], h)) return false;

    const Entity* ent = db->get(h);
    if (!ent) {
        out = make_nil();  // a deleted entity, which is a condition not an error
        return true;
    }
    out = entity_to_alist(in.ctx(), *db, *ent);
    return true;
}

bool subr_entmod(Interp& in, const Value* a, std::size_t, Value& out) {
    Database* db = require_db(in, "entmod");
    if (!db) return false;

    bool found = false;
    const Value ename = alist_get(a[0], -1, found);
    if (!found) return in.fail(EvalStatus::BadArgumentType, "entmod: missing group -1 (ename)");

    Handle h = kNullHandle;
    if (!require_ename(in, "entmod", ename, h)) return false;
    if (!db->get(h)) {
        out = make_nil();
        return true;
    }

    EntityPtr ent;
    bool unsupported = false;
    if (!build_entity(in, *db, a[0], ent, unsupported)) return false;
    if (unsupported) {
        out = make_nil();
        return true;
    }

    // Replacing under the same handle keeps every ename already handed to LISP
    // valid, which is the whole point of entmod over delete-and-remake.
    if (!db->replace(h, std::move(ent))) {
        out = make_nil();
        return true;
    }
    out = a[0];
    return true;
}

bool subr_entdel(Interp& in, const Value* a, std::size_t, Value& out) {
    Database* db = require_db(in, "entdel");
    if (!db) return false;

    Handle h = kNullHandle;
    if (!require_ename(in, "entdel", a[0], h)) return false;

    // R12's entdel toggles: calling it again within the same command undeletes.
    // That needs an undo stack, which does not exist yet, so deletion is final.
    out = db->erase(h) ? a[0] : make_nil();
    return true;
}

bool subr_entlast(Interp& in, const Value*, std::size_t, Value& out) {
    Database* db = require_db(in, "entlast");
    if (!db) return false;

    const Handle h = db->last();
    out = (h == kNullHandle) ? make_nil() : make_ename(h);
    return true;
}

bool subr_entnext(Interp& in, const Value* a, std::size_t argc, Value& out) {
    Database* db = require_db(in, "entnext");
    if (!db) return false;

    Handle h = kNullHandle;
    if (argc == 0 || is_nil(a[0])) {
        h = db->first();
    } else {
        Handle from = kNullHandle;
        if (!require_ename(in, "entnext", a[0], from)) return false;
        h = db->next(from);
    }
    out = (h == kNullHandle) ? make_nil() : make_ename(h);
    return true;
}

}  // namespace ncad::lisp
