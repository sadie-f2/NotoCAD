// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/database.hpp"
#include "noto/entities.hpp"
#include "noto/osnap_search.hpp"
#include "noto/viewport.hpp"

#include <memory>

using namespace noto;

namespace {

// One world unit per pixel about the origin, as in test_pick.cpp.
Viewport plan_800x600() {
    Viewport v;
    v.set_size(800, 600);
    v.set_view_height(600.0);
    v.set_plan_view();
    v.set_target({0, 0, 0});
    return v;
}

ScreenPoint at(double dx_px, double dy_px) {
    return ScreenPoint{400.0 + dx_px, 300.0 - dy_px};
}

// The cursor's world position, which is also the reference point the derived
// snaps need. At one unit per pixel in plan view these are the same numbers.
OsnapQuery query(OsnapMask mask, double wx, double wy, double aperture = 10.0) {
    OsnapQuery q;
    q.mask = mask;
    q.aperture_px = aperture;
    q.reference = Vec3{wx, wy, 0};
    q.has_reference = true;
    return q;
}

bool has_type(const std::vector<OsnapHit>& hits, OsnapType t) {
    for (const OsnapHit& h : hits) {
        if (h.type == t) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("osnap search: nothing when OSMODE is zero") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));
    const Viewport v = plan_800x600();

    const OsnapHit h = osnap_search(db, v, at(0, 0), query(kOsnapNone, 0, 0));
    CHECK(!h.valid);
    CHECK(h.entity == kNullHandle);
}

TEST_CASE("osnap search: an empty drawing yields nothing") {
    Database db;
    const Viewport v = plan_800x600();
    CHECK(!osnap_search(db, v, at(0, 0), query(kOsnapAll, 0, 0)).valid);
}

TEST_CASE("osnap search: an endpoint, with its provenance") {
    Database db;
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));
    const Viewport v = plan_800x600();

    // Three pixels from the line's start.
    const OsnapHit hit = osnap_search(db, v, at(3, 0), query(kOsnapEndpoint, 3, 0));
    CHECK(hit.valid);
    CHECK(hit.type == OsnapType::Endpoint);
    CHECK(hit.entity == h);
    CHECK(hit.entity2 == kNullHandle);
    CHECK_VEC(hit.pos, 0.0, 0.0, 0.0, 1e-9);
    CHECK_NEAR(hit.distance_px, 3.0, 1e-9);
}

TEST_CASE("osnap search: the mask filters, even past a nearer snap") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));
    const Viewport v = plan_800x600();

    // The cursor sits on the midpoint. With MID enabled that is what is found.
    const OsnapHit mid = osnap_search(db, v, at(50, 0), query(kOsnapMidpoint, 50, 0));
    CHECK(mid.valid);
    CHECK(mid.type == OsnapType::Midpoint);
    CHECK_VEC(mid.pos, 50.0, 0.0, 0.0, 1e-9);

    // With only END enabled, the midpoint under the cursor is invisible and
    // nothing is in range -- the endpoints are fifty pixels away.
    CHECK(!osnap_search(db, v, at(50, 0), query(kOsnapEndpoint, 50, 0)).valid);
}

TEST_CASE("osnap search: a discrete snap beats a continuous one") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));
    const Viewport v = plan_800x600();

    // The cursor sits exactly on the line, four pixels from its start. NEAREST
    // is zero pixels away and ENDPOINT is four, but ENDPOINT still wins: this
    // is the tier rule, and without it the discrete snaps would be unreachable.
    const OsnapHit h =
        osnap_search(db, v, at(4, 0), query(kOsnapEndpoint | kOsnapNearest, 4, 0));
    CHECK(h.valid);
    CHECK(h.type == OsnapType::Endpoint);
    CHECK_VEC(h.pos, 0.0, 0.0, 0.0, 1e-9);

    // Both were found; the ranking chose between them.
    std::vector<OsnapHit> all;
    osnap_candidates(db, v, at(4, 0), query(kOsnapEndpoint | kOsnapNearest, 4, 0), all);
    CHECK(has_type(all, OsnapType::Endpoint));
    CHECK(has_type(all, OsnapType::Nearest));
    CHECK(all.front().type == OsnapType::Endpoint);
}

TEST_CASE("osnap search: a continuous snap wins when no discrete one is in range") {
    Database db;
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));
    const Viewport v = plan_800x600();

    // Mid-span, far from either end and off the midpoint.
    const OsnapHit hit =
        osnap_search(db, v, at(30, 0), query(kOsnapEndpoint | kOsnapNearest, 30, 0));
    CHECK(hit.valid);
    CHECK(hit.type == OsnapType::Nearest);
    CHECK(hit.entity == h);
    CHECK_VEC(hit.pos, 30.0, 0.0, 0.0, 1e-9);
}

TEST_CASE("osnap search: the aperture bounds what is considered") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));
    const Viewport v = plan_800x600();

    // Nine pixels from the endpoint, with a ten-pixel aperture: found.
    CHECK(osnap_search(db, v, at(0, 9), query(kOsnapEndpoint, 0, 9, 10.0)).valid);

    // The same cursor with a five-pixel aperture: out of range.
    CHECK(!osnap_search(db, v, at(0, 9), query(kOsnapEndpoint, 0, 9, 5.0)).valid);
}

TEST_CASE("osnap search: a circle's centre and quadrants") {
    Database db;
    const Handle h = db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 100.0));
    const Viewport v = plan_800x600();

    const OsnapHit cen = osnap_search(db, v, at(2, 0), query(kOsnapCenter, 2, 0));
    CHECK(cen.valid);
    CHECK(cen.type == OsnapType::Center);
    CHECK(cen.entity == h);
    CHECK_VEC(cen.pos, 0.0, 0.0, 0.0, 1e-9);

    const OsnapHit qua = osnap_search(db, v, at(98, 0), query(kOsnapQuadrant, 98, 0));
    CHECK(qua.valid);
    CHECK(qua.type == OsnapType::Quadrant);
    CHECK_VEC(qua.pos, 100.0, 0.0, 0.0, 1e-9);
}

TEST_CASE("osnap search: intersection reports both entities") {
    Database db;
    const Handle a = db.add(std::make_unique<Line>(Vec3{-50, 0, 0}, Vec3{50, 0, 0}));
    const Handle b = db.add(std::make_unique<Line>(Vec3{0, -50, 0}, Vec3{0, 50, 0}));
    const Viewport v = plan_800x600();

    const OsnapHit h = osnap_search(db, v, at(2, 2), query(kOsnapIntersection, 2, 2));
    CHECK(h.valid);
    CHECK(h.type == OsnapType::Intersection);
    CHECK_VEC(h.pos, 0.0, 0.0, 0.0, 1e-9);

    // Both handles, whichever order the pair was walked in.
    CHECK((h.entity == a && h.entity2 == b) || (h.entity == b && h.entity2 == a));
}

TEST_CASE("osnap search: skew lines that only cross on screen do not intersect") {
    Database db;
    // Crossing in plan view, but a hundred units apart in Z. INT is a true
    // three-space intersection, never an apparent one.
    db.add(std::make_unique<Line>(Vec3{-50, 0, 0}, Vec3{50, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{0, -50, 100}, Vec3{0, 50, 100}));
    const Viewport v = plan_800x600();

    const OsnapHit h = osnap_search(db, v, at(0, 0), query(kOsnapIntersection, 0, 0));
    CHECK(!h.valid);
}

TEST_CASE("osnap search: entities on hidden layers contribute nothing") {
    Database db;
    const LayerId hidden = db.add_layer("HIDDEN");
    auto line = std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0});
    line->props().layer = hidden;
    db.add(std::move(line));
    const Viewport v = plan_800x600();

    CHECK(osnap_search(db, v, at(0, 0), query(kOsnapEndpoint, 0, 0)).valid);

    db.layer(hidden).color = -7;  // off
    CHECK(!osnap_search(db, v, at(0, 0), query(kOsnapEndpoint, 0, 0)).valid);

    db.layer(hidden).color = 7;
    db.layer(hidden).frozen = true;
    CHECK(!osnap_search(db, v, at(0, 0), query(kOsnapEndpoint, 0, 0)).valid);
}

TEST_CASE("osnap search: derived snaps are skipped without a reference point") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));
    const Viewport v = plan_800x600();

    OsnapQuery q;
    q.mask = kOsnapNearest;
    q.aperture_px = 10.0;
    q.has_reference = false;

    CHECK(!osnap_search(db, v, at(30, 0), q).valid);
}

TEST_CASE("osnap search: coincident geometry gives a deterministic winner") {
    Database db;
    const Handle first = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));
    const Viewport v = plan_800x600();

    // Two identical lines: same type, same distance, same priority. The handle
    // tiebreak decides, and it must decide the same way every time.
    const OsnapHit a = osnap_search(db, v, at(0, 0), query(kOsnapEndpoint, 0, 0));
    const OsnapHit b = osnap_search(db, v, at(0, 0), query(kOsnapEndpoint, 0, 0));
    CHECK(a.valid);
    CHECK(a.entity == b.entity);
    CHECK(a.entity == first);  // the lower handle
}

TEST_CASE("osnap search: candidates come back sorted, best first") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));
    const Viewport v = plan_800x600();

    std::vector<OsnapHit> all;
    osnap_candidates(db, v, at(4, 0), query(kOsnapAll, 4, 0, 40.0), all);
    CHECK(!all.empty());

    // The ordering the comparator promises, checked pairwise: no continuous
    // snap may precede a discrete one, and within a tier distance must not
    // decrease.
    for (std::size_t i = 1; i < all.size(); ++i) {
        const OsnapHit& prev = all[i - 1];
        const OsnapHit& cur = all[i];
        CHECK(!(!osnap_is_discrete(prev.type) && osnap_is_discrete(cur.type)));
        if (osnap_is_discrete(prev.type) == osnap_is_discrete(cur.type)) {
            CHECK(prev.distance_px <= cur.distance_px);
        }
    }

    // And every one of them is genuinely inside the aperture.
    for (const OsnapHit& h : all) {
        CHECK(h.valid);
        CHECK(h.distance_px <= 40.0);
    }
}

TEST_CASE("osnap search: priority breaks an exact distance tie") {
    Database db;
    // Two lines meeting at the origin, so ENDPOINT and INTERSECTION land on
    // exactly the same point at exactly the same distance.
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{0, 100, 0}));
    const Viewport v = plan_800x600();

    const OsnapHit h =
        osnap_search(db, v, at(0, 0), query(kOsnapEndpoint | kOsnapIntersection, 0, 0));
    CHECK(h.valid);
    // Both are discrete and both are zero pixels away; END sorts before INT.
    CHECK(h.type == OsnapType::Endpoint);
    CHECK_VEC(h.pos, 0.0, 0.0, 0.0, 1e-9);
}
