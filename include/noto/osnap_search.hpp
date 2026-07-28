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

#include "noto/osnap.hpp"
#include "noto/pick.hpp"
#include "noto/vec3.hpp"
#include "noto/viewport.hpp"

#include <vector>

namespace noto {

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
};

struct OsnapQuery {
    // OSMODE. kOsnapNone disables the search entirely, which is the default
    // state of a drawing and must stay cheap.
    OsnapMask mask{kOsnapNone};

    // APERTURE, in pixels, as a half-height: the box is twice this on a side.
    double aperture_px{10.0};

    // Where the cursor unprojected to. PER, TAN and NEA are all defined
    // relative to a point, and this is it. Without one they are skipped.
    //
    // Its quality bounds theirs: it is a point on the construction plane, not
    // a true ray hit, which is the same simplification the viewport already
    // makes when a click answers a point prompt.
    Vec3 reference{};
    bool has_reference{false};
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

// Every candidate within the aperture, best first.
void osnap_candidates(const Database& db, const Viewport& vp, const ScreenPoint& cursor,
                      const OsnapQuery& q, std::vector<OsnapHit>& out);

// The best candidate, or an OsnapHit with valid == false.
OsnapHit osnap_search(const Database& db, const Viewport& vp, const ScreenPoint& cursor,
                      const OsnapQuery& q);

}  // namespace noto
