#include "test.hpp"

#include "noto/database.hpp"
#include "noto/ecs.hpp"
#include "noto/entities.hpp"

#include <algorithm>
#include <numbers>

using namespace noto;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kTol = 1e-12;

bool has_snap(const std::vector<OsnapPoint>& pts, OsnapType type, const Vec3& at) {
    return std::any_of(pts.begin(), pts.end(), [&](const OsnapPoint& p) {
        return p.type == type && near_equal(p.pos, at, 1e-9);
    });
}

std::size_t count_snaps(const std::vector<OsnapPoint>& pts, OsnapType type) {
    return static_cast<std::size_t>(
        std::count_if(pts.begin(), pts.end(), [&](const OsnapPoint& p) { return p.type == type; }));
}

}  // namespace

TEST_CASE("line: transform, bbox and snaps") {
    Line l{{0, 0, 0}, {4, 2, 0}};
    CHECK_NEAR(l.length(), std::sqrt(20.0), kTol);
    CHECK_VEC(l.midpoint(), 2.0, 1.0, 0.0, kTol);

    const BBox b = l.bbox();
    CHECK(b.valid());
    CHECK_VEC(b.min, 0.0, 0.0, 0.0, kTol);
    CHECK_VEC(b.max, 4.0, 2.0, 0.0, kTol);

    std::vector<OsnapPoint> snaps;
    l.osnap_points(snaps);
    CHECK(has_snap(snaps, OsnapType::Endpoint, Vec3{0, 0, 0}));
    CHECK(has_snap(snaps, OsnapType::Endpoint, Vec3{4, 2, 0}));
    CHECK(has_snap(snaps, OsnapType::Midpoint, Vec3{2, 1, 0}));

    l.transform(Mat4::translation({1, 1, 1}));
    CHECK_VEC(l.start(), 1.0, 1.0, 1.0, kTol);
    CHECK_VEC(l.end(), 5.0, 3.0, 1.0, kTol);
}

TEST_CASE("circle: rotating out of the world plane carries the normal") {
    Circle c{{0, 0, 0}, 2.0};
    CHECK_VEC(c.props().normal, 0.0, 0.0, 1.0, kTol);

    // Tip the circle 90 degrees about the world X axis; it now lies in XZ.
    c.transform(Mat4::rotation(Vec3{}, kWorldX, kPi / 2.0));
    CHECK_NEAR(c.radius(), 2.0, 1e-12);
    CHECK_VEC(c.props().normal, 0.0, -1.0, 0.0, 1e-12);

    // Its bounding box must now be flat in Y.
    const BBox b = c.bbox();
    CHECK_NEAR(b.size().y, 0.0, 1e-9);
    CHECK_NEAR(b.size().x, 4.0, 1e-9);
    CHECK_NEAR(b.size().z, 4.0, 1e-9);
}

TEST_CASE("circle: uniform scale scales the radius") {
    Circle c{{1, 1, 0}, 3.0};
    c.transform(Mat4::uniform_scaling(2.0));
    CHECK_NEAR(c.radius(), 6.0, 1e-12);
    CHECK_VEC(c.center(), 2.0, 2.0, 0.0, kTol);
}

TEST_CASE("circle: bbox of a tilted circle is exact") {
    // A circle whose normal is (1,1,1)/sqrt(3): extent along each axis is
    // r*sqrt(1 - 1/3) = r*sqrt(2/3).
    const Circle c{{0, 0, 0}, 1.0, Vec3{1, 1, 1}};
    const BBox b = c.bbox();
    const double expect = std::sqrt(2.0 / 3.0);
    CHECK_NEAR(b.max.x, expect, 1e-12);
    CHECK_NEAR(b.max.y, expect, 1e-12);
    CHECK_NEAR(b.max.z, expect, 1e-12);
}

TEST_CASE("circle: quadrant snaps are on the circle and centre snap exists") {
    const Circle c{{2, -1, 3}, 5.0, Vec3{0.3, 1.0, -0.2}};
    std::vector<OsnapPoint> snaps;
    c.osnap_points(snaps);

    CHECK(has_snap(snaps, OsnapType::Center, Vec3{2, -1, 3}));
    CHECK(count_snaps(snaps, OsnapType::Quadrant) == 4);
    for (const OsnapPoint& p : snaps) {
        if (p.type != OsnapType::Quadrant) continue;
        CHECK_NEAR(length(p.pos - c.center()), 5.0, 1e-12);
        // On the circle's plane, so perpendicular to the normal.
        CHECK_NEAR(dot(p.pos - c.center(), c.props().normal), 0.0, 1e-12);
    }
}

TEST_CASE("arc: sweep, endpoints and midpoint") {
    // Quarter arc in the world XY plane, from 0 to 90 degrees.
    const Arc a{{0, 0, 0}, 2.0, 0.0, kPi / 2.0};
    CHECK_NEAR(a.sweep(), kPi / 2.0, 1e-12);
    CHECK_VEC(a.start_point(), 2.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(a.end_point(), 0.0, 2.0, 0.0, 1e-12);
    CHECK_VEC(a.midpoint(), std::sqrt(2.0), std::sqrt(2.0), 0.0, 1e-12);
}

TEST_CASE("arc: sweep wraps counterclockwise through zero") {
    // 270 degrees to 90 degrees is a 180 degree sweep passing through 0.
    const Arc a{{0, 0, 0}, 1.0, 3.0 * kPi / 2.0, kPi / 2.0};
    CHECK_NEAR(a.sweep(), kPi, 1e-12);
    CHECK(a.contains_angle(0.0));
    CHECK(!a.contains_angle(kPi));
}

TEST_CASE("arc: bbox includes only the quadrants inside the sweep") {
    // First-quadrant arc: box is exactly the corner region, not the full circle.
    const Arc a{{0, 0, 0}, 1.0, 0.0, kPi / 2.0};
    const BBox b = a.bbox();
    CHECK_NEAR(b.min.x, 0.0, 1e-12);
    CHECK_NEAR(b.min.y, 0.0, 1e-12);
    CHECK_NEAR(b.max.x, 1.0, 1e-12);
    CHECK_NEAR(b.max.y, 1.0, 1e-12);
}

TEST_CASE("arc: transform preserves the geometric endpoints") {
    Arc a{{1, 2, 0}, 3.0, 0.4, 2.1};
    const Vec3 p_start = a.start_point();
    const Vec3 p_end = a.end_point();
    const Vec3 p_mid = a.midpoint();

    const Mat4 m = Mat4::translation({5, -2, 3}) *
                   Mat4::rotation(Vec3{}, Vec3{1, 2, -1}, 0.9);
    a.transform(m);

    // The arc must still pass through the transformed endpoints, with the
    // start/end order intact.
    CHECK_VEC(a.start_point(), m.transform_point(p_start).x, m.transform_point(p_start).y,
              m.transform_point(p_start).z, 1e-9);
    CHECK_VEC(a.end_point(), m.transform_point(p_end).x, m.transform_point(p_end).y,
              m.transform_point(p_end).z, 1e-9);
    CHECK_VEC(a.midpoint(), m.transform_point(p_mid).x, m.transform_point(p_mid).y,
              m.transform_point(p_mid).z, 1e-9);
    CHECK_NEAR(a.radius(), 3.0, 1e-9);
}

TEST_CASE("arc: mirroring reverses direction but keeps the same curve") {
    Arc a{{0, 0, 0}, 1.0, 0.0, kPi / 2.0};
    const Vec3 p_start = a.start_point();
    const Vec3 p_end = a.end_point();

    // Mirror across the world YZ plane.
    const Mat4 m = Mat4::mirror(Vec3{}, kWorldX);
    a.transform(m);

    // A mirror flips the plane's orientation, so the normal reverses and the
    // arc still runs counterclockwise in its own (now flipped) frame.
    CHECK_VEC(a.props().normal, 0.0, 0.0, -1.0, 1e-12);
    CHECK_VEC(a.start_point(), m.transform_point(p_start).x, m.transform_point(p_start).y,
              m.transform_point(p_start).z, 1e-9);
    CHECK_VEC(a.end_point(), m.transform_point(p_end).x, m.transform_point(p_end).y,
              m.transform_point(p_end).z, 1e-9);
    CHECK_NEAR(a.sweep(), kPi / 2.0, 1e-9);
}

TEST_CASE("entity: clone is independent and carries properties") {
    Circle c{{1, 2, 3}, 4.0};
    c.props().color = 5;
    c.props().thickness = 0.25;

    const EntityPtr copy = c.clone();
    CHECK(copy->type() == EntityType::Circle);
    CHECK(copy->props().color == 5);
    CHECK_NEAR(copy->props().thickness, 0.25, kTol);
    // A clone has no identity until a database gives it one.
    CHECK(copy->handle() == kNullHandle);

    copy->transform(Mat4::translation({10, 0, 0}));
    CHECK_VEC(c.center(), 1.0, 2.0, 3.0, kTol);  // original untouched
}

TEST_CASE("database: handles are stable and never reused") {
    Database db;
    const Handle a = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));
    const Handle b = db.add(std::make_unique<Line>(Vec3{0, 1, 0}, Vec3{1, 1, 0}));
    const Handle c = db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 1.0));

    CHECK(a != b && b != c);
    CHECK(db.size() == 3);
    CHECK(db.get(a) != nullptr);

    // Erasing the middle entity must not disturb the others' handles -- an
    // AutoLISP ename held across the deletion has to stay valid.
    CHECK(db.erase(b));
    CHECK(db.get(b) == nullptr);
    CHECK(db.get(a) != nullptr);
    CHECK(db.get(c) != nullptr);
    CHECK(db.size() == 2);

    const Handle d = db.add(std::make_unique<Line>(Vec3{}, Vec3{1, 1, 1}));
    CHECK(d != b);  // the freed handle is not recycled
    CHECK(!db.erase(b));
}

TEST_CASE("database: iteration is in insertion order") {
    Database db;
    std::vector<Handle> added;
    for (int i = 0; i < 8; ++i) {
        added.push_back(db.add(std::make_unique<Line>(Vec3{}, Vec3{static_cast<double>(i), 0, 0})));
    }
    CHECK(db.order() == added);

    db.erase(added[3]);
    added.erase(added.begin() + 3);
    CHECK(db.order() == added);
}

TEST_CASE("database: layer and linetype tables") {
    Database db;
    // Layer "0" and CONTINUOUS exist from construction, as R12 requires.
    CHECK(db.find_layer("0") == kLayerZero);
    CHECK(db.find_linetype("CONTINUOUS") == kLinetypeContinuous);

    const LinetypeId hidden = db.add_linetype("HIDDEN", "Hidden line", {0.25, -0.125});
    const LayerId walls = db.add_layer("WALLS", 3, hidden);
    CHECK(walls != kLayerZero);
    CHECK(db.layer(walls).color == 3);
    CHECK(db.layer(walls).linetype == hidden);

    // Adding an existing name returns the existing id rather than duplicating.
    CHECK(db.add_layer("WALLS") == walls);
    CHECK(db.find_layer("NOPE") == kInvalidLayer);
}

TEST_CASE("database: extents covers every entity") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{-3, 0, 0}, Vec3{2, 0, 0}));
    db.add(std::make_unique<Circle>(Vec3{0, 5, 0}, 1.0));

    const BBox e = db.extents();
    CHECK_NEAR(e.min.x, -3.0, 1e-12);
    CHECK_NEAR(e.max.x, 2.0, 1e-12);
    CHECK_NEAR(e.max.y, 6.0, 1e-12);

    CHECK(!Database{}.extents().valid());  // empty database has no extents
}
