// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/osnap_search.hpp"

#include "noto/database.hpp"
#include "noto/osnap_derived.hpp"
#include "noto/scene.hpp"

#include <algorithm>
#include <cmath>

namespace noto {
namespace {

struct Candidate {
    const Entity* entity{nullptr};
    Handle handle{kNullHandle};
};

// Distance from the cursor to a world point, in pixels. False when the point
// does not project finitely.
bool screen_distance(const Viewport& vp, const ScreenPoint& cursor, const Vec3& p, double* out) {
    const ScreenPoint sp = vp.project(p);
    if (!std::isfinite(sp.x) || !std::isfinite(sp.y)) return false;
    const double dx = sp.x - cursor.x;
    const double dy = sp.y - cursor.y;
    *out = std::sqrt(dx * dx + dy * dy);
    return true;
}

// Note what is NOT here: a distance filter. The aperture chose the entity; a
// snap point it produced is a candidate however far from the cursor it lands.
// That is what makes "hover anywhere on a line with END running and get the
// nearer end" work, and it is the same rule that snaps to a circle's centre
// from its rim. Filtering points by distance instead would mean you had to be
// on the snap already in order to find it.
void add_hit(std::vector<OsnapHit>& out, const Viewport& vp, const ScreenPoint& cursor,
             const Vec3& pos, OsnapType type, Handle a, Handle b) {
    double d = 0.0;
    if (!screen_distance(vp, cursor, pos, &d)) return;

    OsnapHit h;
    h.pos = pos;
    h.type = type;
    h.entity = a;
    h.entity2 = b;
    h.distance_px = d;
    h.valid = true;
    out.push_back(h);
}

// Strict weak ordering, and deliberately no epsilon anywhere in it. Letting
// priority break near-ties -- `if (fabs(da - db) > eps) return da < db;` -- is
// the obvious-looking way to write this and it is not transitive, which makes
// std::sort undefined rather than merely differently ordered. Distances compare
// exactly; the handle tiebreak is what makes the result deterministic.
bool better(const OsnapHit& a, const OsnapHit& b) {
    const bool a_discrete = osnap_is_discrete(a.type);
    const bool b_discrete = osnap_is_discrete(b.type);
    if (a_discrete != b_discrete) return a_discrete;

    if (a.distance_px != b.distance_px) return a.distance_px < b.distance_px;

    const int pa = osnap_priority(a.type);
    const int pb = osnap_priority(b.type);
    if (pa != pb) return pa < pb;

    if (a.entity != b.entity) return a.entity < b.entity;
    return a.entity2 < b.entity2;
}

void collect_static(std::vector<OsnapHit>& out, const Candidate& c, const Viewport& vp,
                    const ScreenPoint& cursor, const OsnapQuery& q,
                    std::vector<OsnapPoint>& scratch) {
    scratch.clear();
    c.entity->osnap_points(scratch);
    for (const OsnapPoint& p : scratch) {
        if (!osnap_enabled(q.mask, p.type)) continue;
        add_hit(out, vp, cursor, p.pos, p.type, c.handle, kNullHandle);
    }
}

void collect_derived(std::vector<OsnapHit>& out, const Candidate& c, const Viewport& vp,
                     const ScreenPoint& cursor, const OsnapQuery& q) {
    Vec3 p{};

    // NEAREST is the one that genuinely wants the cursor: the point on the
    // entity closest to where you are pointing.
    if (q.has_reference && osnap_enabled(q.mask, OsnapType::Nearest) &&
        nearest_point(*c.entity, q.reference, &p)) {
        add_hit(out, vp, cursor, p, OsnapType::Nearest, c.handle, kNullHandle);
    }

    // TANGENT with no rubber-band base is DEFERRED rather than answered.
    //
    // There is no tangent until the line has another end, so nothing here can
    // compute one. Answering from LASTPOINT instead -- which is what the
    // has_from_point fallback used to do -- is not an approximation but a wrong
    // answer: LASTPOINT has nothing to do with where this line will go, so the
    // snap lands somewhere arbitrary and then stays there when the far end
    // moves. The marker goes on the curve nearest the cursor, and the command
    // solves the constraint once it has the far end.
    if (!q.from_point_is_base && q.has_reference && osnap_enabled(q.mask, OsnapType::Tangent) &&
        nearest_point(*c.entity, q.reference, &p)) {
        add_hit(out, vp, cursor, p, OsnapType::Tangent, c.handle, kNullHandle);
        if (!out.empty()) out.back().deferred = true;
    }

    // PER and TAN are measured from the rubber-band base, not the cursor.
    // Using the cursor makes the foot of the perpendicular the closest point on
    // the target, which is NEAREST wearing a different marker.
    if (!q.has_from_point) return;

    if (osnap_enabled(q.mask, OsnapType::Perpendicular) &&
        perpendicular_point(*c.entity, q.from_point, &p)) {
        add_hit(out, vp, cursor, p, OsnapType::Perpendicular, c.handle, kNullHandle);
    }
    if (q.from_point_is_base && osnap_enabled(q.mask, OsnapType::Tangent)) {
        Vec3 tan[kMaxTangents];
        const int n = tangent_points(*c.entity, q.from_point, tan);
        for (int i = 0; i < n; ++i) {
            add_hit(out, vp, cursor, tan[i], OsnapType::Tangent, c.handle, kNullHandle);
        }
    }
}

void collect_intersections(std::vector<OsnapHit>& out, const std::vector<Candidate>& set,
                           const Viewport& vp, const ScreenPoint& cursor) {
    Vec3 pts[kMaxIntersections];
    for (std::size_t i = 0; i < set.size(); ++i) {
        for (std::size_t j = i + 1; j < set.size(); ++j) {
            const int n = intersect_entities(*set[i].entity, *set[j].entity, pts);
            for (int k = 0; k < n; ++k) {
                // Both handles recorded, in drawing order, so the pair is
                // reported the same way whichever side it was found from.
                add_hit(out, vp, cursor, pts[k], OsnapType::Intersection, set[i].handle,
                        set[j].handle);
            }
        }
    }
}

}  // namespace

int osnap_priority(OsnapType t) {
    switch (t) {
        case OsnapType::Endpoint: return 0;
        case OsnapType::Midpoint: return 1;
        case OsnapType::Center: return 2;
        case OsnapType::Node: return 3;
        case OsnapType::Quadrant: return 4;
        case OsnapType::Intersection: return 5;
        case OsnapType::Insert: return 6;
        case OsnapType::Perpendicular: return 7;
        case OsnapType::Tangent: return 8;
        case OsnapType::Nearest: return 9;
    }
    return 99;
}

bool osnap_is_discrete(OsnapType t) {
    switch (t) {
        case OsnapType::Perpendicular:
        case OsnapType::Tangent:
        case OsnapType::Nearest: return false;
        default: return true;
    }
}

void osnap_candidates(const Database& db, const Viewport& vp, const ScreenPoint& cursor,
                      const OsnapQuery& q, std::vector<OsnapHit>& out) {
    out.clear();
    if (q.mask == kOsnapNone) return;  // the default state of a drawing

    // The aperture set, and it is the whole selection step: everything in it
    // offers all of its enabled snap points at any distance, everything outside
    // offers none. Two ways in, and both are needed:
    //
    //   1. The aperture touches the entity's drawn geometry. This is what makes
    //      "hover anywhere on a line, get its nearer end" work, and what gets
    //      you a circle's centre from its rim.
    //   2. The aperture touches one of the entity's enabled snap points. A
    //      circle's centre has no geometry at it, so rule 1 alone would mean
    //      hovering the middle of a circle found nothing -- which is exactly
    //      where you reach for CEN. Same for an endpoint approached from off
    //      the end of the line.
    //
    // Measured against geometry rather than the bounding box, since a bbox hit
    // would put every snap of a large tilted circle in play whenever the cursor
    // was near its extent and nowhere near the curve. Gathered backwards so the
    // cap keeps the topmost entities, which are the ones being pointed at.
    std::vector<Candidate> set;
    std::vector<OsnapPoint> scratch;
    const std::vector<Handle>& order = db.order();
    for (std::size_t i = order.size(); i-- > 0 && set.size() < kMaxApertureEntities;) {
        const Entity* e = db.get(order[i]);
        if (!e) continue;
        if (!entity_visible(db, *e)) continue;
        if (!entity_near_cursor(*e, vp, cursor, q.aperture_px)) continue;  // broad phase

        double d = 0.0;
        bool in_set = entity_pick_distance(*e, vp, cursor, &d) && d <= q.aperture_px;

        if (!in_set) {
            scratch.clear();
            e->osnap_points(scratch);
            for (const OsnapPoint& p : scratch) {
                if (!osnap_enabled(q.mask, p.type)) continue;
                double pd = 0.0;
                if (screen_distance(vp, cursor, p.pos, &pd) && pd <= q.aperture_px) {
                    in_set = true;
                    break;
                }
            }
        }
        if (!in_set) continue;

        set.push_back(Candidate{e, order[i]});
    }
    if (set.empty()) return;

    for (const Candidate& c : set) {
        collect_static(out, c, vp, cursor, q, scratch);
        collect_derived(out, c, vp, cursor, q);
    }

    if (osnap_enabled(q.mask, OsnapType::Intersection)) {
        collect_intersections(out, set, vp, cursor);
    }

    std::sort(out.begin(), out.end(), better);
}

OsnapHit osnap_search(const Database& db, const Viewport& vp, const ScreenPoint& cursor,
                      const OsnapQuery& q) {
    std::vector<OsnapHit> candidates;
    osnap_candidates(db, vp, cursor, q, candidates);
    return candidates.empty() ? OsnapHit{} : candidates.front();
}

}  // namespace noto
