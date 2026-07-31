// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "geom_subrs.hpp"

#include "ncad/database.hpp"
#include "ncad/intersect.hpp"
#include "ncad/osnap.hpp"
#include "ncad/osnap_derived.hpp"

#include <cmath>
#include <numbers>
#include <string>
#include <vector>

namespace ncad::lisp {
namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

// A point is a list of two or three numbers, as everywhere else in this layer.
// Two is legal and means Z = 0, which is what R12 does.
bool arg_point(Interp& in, const char* who, const Value& v, Vec3& out) {
    double c[3] = {0.0, 0.0, 0.0};
    std::size_t i = 0;
    for (Value cur = v; is_cons(cur); cur = cdr(cur)) {
        if (i >= 3) {
            return in.fail(EvalStatus::BadArgumentType,
                           std::string(who) + ": point has more than three coordinates");
        }
        const Value n = car(cur);
        if (!is_number(n)) {
            return in.fail(EvalStatus::BadArgumentType,
                           std::string(who) + ": coordinate is not a number: " + prin1(n));
        }
        c[i++] = as_double(n);
    }
    if (i < 2) {
        return in.fail(EvalStatus::BadArgumentType,
                       std::string(who) + ": not a point: " + prin1(v));
    }
    out = Vec3{c[0], c[1], c[2]};
    return true;
}

bool arg_real(Interp& in, const char* who, const Value& v, double& out) {
    if (!is_number(v)) {
        return in.fail(EvalStatus::BadArgumentType,
                       std::string(who) + ": not a number: " + prin1(v));
    }
    out = as_double(v);
    return true;
}

Value point_value(Context& ctx, const Vec3& p) {
    const Value coords[3] = {make_real(p.x), make_real(p.y), make_real(p.z)};
    return ctx.list(coords, 3);
}

}  // namespace

bool subr_polar(Interp& in, const Value* args, std::size_t, Value& out) {
    Vec3 base{};
    double ang = 0.0;
    double dist = 0.0;
    if (!arg_point(in, "polar", args[0], base)) return false;
    if (!arg_real(in, "polar", args[1], ang)) return false;
    if (!arg_real(in, "polar", args[2], dist)) return false;

    // Z is carried through rather than zeroed: a script working at a height
    // stays at it, which is what every use of this in a 3D drawing assumes.
    const Vec3 p{base.x + std::cos(ang) * dist, base.y + std::sin(ang) * dist, base.z};
    out = point_value(in.ctx(), p);
    return true;
}

bool subr_distance(Interp& in, const Value* args, std::size_t, Value& out) {
    Vec3 a{};
    Vec3 b{};
    if (!arg_point(in, "distance", args[0], a)) return false;
    if (!arg_point(in, "distance", args[1], b)) return false;

    // Three dimensions, always. R12 flattened this when FLATLAND was set, and
    // FLATLAND was a compatibility switch for drawings older than this program
    // is pretending to be.
    out = make_real(length(b - a));
    return true;
}

bool subr_angle(Interp& in, const Value* args, std::size_t, Value& out) {
    Vec3 a{};
    Vec3 b{};
    if (!arg_point(in, "angle", args[0], a)) return false;
    if (!arg_point(in, "angle", args[1], b)) return false;

    // Projected onto the world XY plane, which is what makes this the inverse
    // of polar. Normalised to [0, 2*pi) because a script comparing angles
    // should not have to know that atan2 returns negatives.
    double t = std::atan2(b.y - a.y, b.x - a.x);
    if (t < 0.0) t += kTwoPi;
    out = make_real(t);
    return true;
}

bool subr_inters(Interp& in, const Value* args, std::size_t n, Value& out) {
    Vec3 p[4];
    for (std::size_t i = 0; i < 4; ++i) {
        if (!arg_point(in, "inters", args[i], p[i])) return false;
    }

    // The convention people get backwards: a FIFTH argument of nil means the
    // lines are infinite. Omitted, or anything non-nil, means the crossing must
    // lie on both segments.
    const bool on_segments = (n < 5) || !is_nil(args[4]);

    IntersectionList hits;
    intersect_line_line(p[0], p[1], p[2], p[3], hits);

    for (const Intersection& h : hits) {
        if (on_segments) {
            // The kernel reports the parameter along each line, so bounding is a
            // range test rather than a second intersection.
            if (h.t0 < -kIntersectTol || h.t0 > 1.0 + kIntersectTol) continue;
            if (h.t1 < -kIntersectTol || h.t1 > 1.0 + kIntersectTol) continue;
        }
        out = point_value(in.ctx(), h.point);
        return true;
    }

    // Parallel, skew, or crossing outside the segments. nil rather than an
    // error: "do these meet?" is a question a script asks expecting either
    // answer.
    out = make_nil();
    return true;
}

bool subr_osnap(Interp& in, const Value* args, std::size_t, Value& out) {
    Database* db = in.database();
    if (db == nullptr) {
        return in.fail(EvalStatus::BadArgumentType, "osnap: no drawing is attached");
    }

    Vec3 ref{};
    if (!arg_point(in, "osnap", args[0], ref)) return false;
    if (args[1].type != Type::Str) {
        return in.fail(EvalStatus::BadArgumentType, "osnap: modes are not a string: " +
                                                        prin1(args[1]));
    }

    const std::string modes(args[1].str->view());
    OsnapMask mask = kOsnapNone;
    if (!parse_osnap_mask(modes, &mask) || mask == kOsnapNone) {
        // An unparseable mode string is a bug in the script; NONE is not, and
        // means exactly what it says.
        out = make_nil();
        return true;
    }

    bool found = false;
    double best = 0.0;
    Vec3 answer{};
    auto consider = [&](const Vec3& p) {
        const double d = length_sq(p - ref);
        if (!found || d < best) {
            best = d;
            answer = p;
            found = true;
        }
    };

    std::vector<OsnapPoint> pts;
    for (const Handle h : db->order()) {
        const Entity* e = db->get(h);
        if (e == nullptr) continue;

        // The stored snaps, filtered to what was asked for.
        pts.clear();
        e->osnap_points(pts);
        for (const OsnapPoint& p : pts) {
            if (osnap_enabled(mask, p.type)) consider(p.pos);
        }

        // And the derived ones, which take the reference point as their own --
        // NEAREST to it, PERPENDICULAR from it, TANGENT through it.
        Vec3 d{};
        if (osnap_enabled(mask, OsnapType::Nearest) && nearest_point(*e, ref, &d)) consider(d);
        if (osnap_enabled(mask, OsnapType::Perpendicular) && perpendicular_point(*e, ref, &d)) {
            consider(d);
        }
        if (osnap_enabled(mask, OsnapType::Tangent)) {
            Vec3 tan[kMaxTangents];
            const int count = tangent_points(*e, ref, tan);
            for (int i = 0; i < count; ++i) consider(tan[i]);
        }
    }

    out = found ? point_value(in.ctx(), answer) : make_nil();
    return true;
}

}  // namespace ncad::lisp
