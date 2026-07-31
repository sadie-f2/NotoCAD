// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Which object snap is under the cursor.
//
// This is where the two osnap families finally meet. Entity::osnap_points()
// produces static snaps that know their type but not their entity;
// osnap_derived.hpp produces bare Vec3 with no type at all. Neither knows how
// far it is from the cursor, because neither has a viewport. A snap only
// becomes usable once it carries all three -- what kind it is, which entity it
// came from, and how far away it is on screen -- and assembling that is the
// whole job here.
//
// Nothing is added to OsnapPoint to make this work: it stays a pure geometry
// value on the entity vtable, and provenance lives in OsnapHit instead.
//
// THE APERTURE SELECTS THE ENTITY, NOT THE SNAP POINT. This is the rule the
// whole thing turns on. An entity the aperture box catches offers *all* of its
// enabled snap points, however far from the cursor they land; an entity the box
// misses offers none. Filtering candidate points by distance instead would mean
// having to be on a snap already in order to find it, which makes END useless
// on anything longer than the aperture.
//
// Two ways for the box to catch an entity, and both are needed:
//
//   1. It touches the entity's drawn geometry -- hover anywhere along a line
//      and get its nearer end; hover a circle's rim and get its centre.
//   2. It touches one of the entity's enabled snap points. A circle's centre
//      has no geometry at it, so rule 1 alone would mean pointing at the middle
//      of a circle found nothing -- which is exactly where you reach for CEN.
//      Same for an endpoint approached from beyond the end of a short line.
//
// RANKING. Not simply nearest-wins, which does not work. NEAREST is within a
// pixel of the cursor by definition and PERPENDICULAR usually is too, so a
// plain distance sort would let the continuous snaps bury ENDPOINT and
// MIDPOINT and the feature would be useless. So snaps fall into two tiers:
// discrete ones that name a specific point (END, MID, CEN, NOD, QUA, INT, INS)
// and continuous ones that slide along the geometry (PER, TAN, NEA). A
// continuous snap is considered only when no discrete one is in the aperture.
// Within a tier the nearest wins, then R12's mode order, then handle, so the
// result is deterministic and testable.
//
// Cost: linear in the drawing per call, and this runs on every mouse move. The
// broad phase is a rectangle test, and the aperture set is capped, so the
// quadratic intersection pass cannot run away on a dense drawing.
#pragma once

#include "ncad/osnap.hpp"
#include "ncad/pick.hpp"
#include "ncad/vec3.hpp"
#include "ncad/viewport.hpp"

#include <vector>

namespace ncad {

class Database;

// A snap point, with everything the caller needs to draw it and use it.
struct OsnapHit {
    Vec3 pos{};
    OsnapType type{OsnapType::Endpoint};

    // Where it came from. INTERSECTION is the only snap produced by two
    // entities, and it is the only case where entity2 is set.
    Handle entity{kNullHandle};
    Handle entity2{kNullHandle};

    double distance_px{0.0};
    bool valid{false};

    // The snap names a CONSTRAINT rather than a location: `pos` is only where
    // the marker goes. A tangent picked with no other end yet is the case --
    // see InputValue::of_deferred_snap. The command resolves it later.
    bool deferred{false};
};

struct OsnapQuery {
    // OSMODE. kOsnapNone disables the search entirely, which is the default
    // state of a drawing and must stay cheap.
    OsnapMask mask{kOsnapNone};

    // APERTURE, in pixels, as a half-height: the box is twice this on a side.
    // It bounds which entities are considered, not which points -- see above.
    double aperture_px{10.0};

    // Where the cursor unprojected to. NEAREST is defined relative to it: the
    // point on the entity closest to where you are pointing.
    //
    // Its quality bounds NEA's: it is a point on the construction plane, not a
    // true ray hit, which is the same simplification the viewport already makes
    // when a click answers a point prompt.
    Vec3 reference{};
    bool has_reference{false};

    // Where the point being placed is measured FROM -- the rubber-band base,
    // which is the previous point of the line being drawn.
    //
    // PERPENDICULAR and TANGENT need this and not the cursor, and the
    // distinction is the whole difference between them working and not. "Snap
    // perpendicular" means perpendicular to the target as seen from where this
    // segment starts. Measured from the cursor instead, the foot of the
    // perpendicular is just the closest point on the target -- so PER silently
    // becomes NEA, and TAN likewise collapses.
    //
    // Absent at the first point of a command, since there is nothing to be
    // perpendicular or tangent from yet; PER and TAN are simply not offered
    // then. R12 defers them to a second pick, which is not modelled here.
    Vec3 from_point{};
    bool has_from_point{false};

    // True when `from_point` is the rubber-band origin of the prompt actually
    // in progress, rather than LASTPOINT standing in for one.
    //
    // This is the distinction a deferred tangent turns on. Resolving TANGENT
    // against LASTPOINT is not an approximation, it is a wrong answer: the
    // point has nothing to do with where the line being drawn will go, so the
    // snap lands somewhere arbitrary and then stays there. Without a real base
    // the tangent cannot be resolved at all and must be deferred.
    bool from_point_is_base{false};
};

// A dense drawing can put hundreds of entities under one aperture, and the
// intersection pass is quadratic in that count. Capping it bounds the work per
// mouse move; the entities kept are the topmost, which are the ones a user is
// pointing at.
inline constexpr std::size_t kMaxApertureEntities = 32;

// Lower sorts first. R12's mode order: END, MID, CEN, NOD, QUA, INT, INS, PER,
// TAN, NEA. Only breaks ties between snaps at the same distance.
int osnap_priority(OsnapType t);

// True for the snaps that name a specific point rather than sliding along the
// geometry. Discrete snaps beat continuous ones regardless of distance.
bool osnap_is_discrete(OsnapType t);

// Every candidate offered by the entities under the aperture, best first.
// Their distances are not bounded by aperture_px.
void osnap_candidates(const Database& db, const Viewport& vp, const ScreenPoint& cursor,
                      const OsnapQuery& q, std::vector<OsnapHit>& out);

// The best candidate, or an OsnapHit with valid == false.
OsnapHit osnap_search(const Database& db, const Viewport& vp, const ScreenPoint& cursor,
                      const OsnapQuery& q);

}  // namespace ncad
