// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/database.hpp"
#include "noto/ecs.hpp"
#include "noto/entities.hpp"
#include "noto/render.hpp"
#include "noto/scene.hpp"

#include <cmath>
#include <numbers>
#include <vector>

using namespace noto;

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

// The whole point of Renderer being abstract: the tests get a backend with no
// display, no Qt and no pixels, exercising exactly the path QPainter will.
struct RecordingRenderer final : Renderer {
    struct Poly {
        std::vector<Vec3> pts;
        bool closed{false};
        EntityProps props{};
    };

    std::vector<Poly> polys;
    EntityProps current{};
    int begin_calls{0};

    void begin_entity(const EntityProps& p) override {
        current = p;
        ++begin_calls;
    }

    void polyline(const Vec3* pts, std::size_t count, bool closed) override {
        Poly p;
        p.pts.assign(pts, pts + count);
        p.closed = closed;
        p.props = current;
        polys.push_back(std::move(p));
    }
};

DrawContext fine() { return DrawContext{1e-4}; }

}  // namespace

TEST_CASE("arc_segment_count respects the 45-degree floor") {
    // A tolerance far coarser than the geometry still yields 8 per full turn.
    CHECK(arc_segment_count(10.0, kTwoPi, 1e6) == 8);
    CHECK(arc_segment_count(10.0, std::numbers::pi, 1e6) == 4);
    // A zero radius has no curvature to resolve, but the sweep still does.
    CHECK(arc_segment_count(0.0, kTwoPi, 1e-6) == 8);
}

TEST_CASE("arc_segment_count tightens with tolerance and is bounded") {
    const int coarse = arc_segment_count(100.0, kTwoPi, 1.0);
    const int fine_n = arc_segment_count(100.0, kTwoPi, 0.01);
    CHECK(fine_n > coarse);
    CHECK(coarse >= 8);

    // A degenerate tolerance must not run away.
    CHECK(arc_segment_count(1e9, kTwoPi, 1e-12) == kMaxArcSegments);
    CHECK(arc_segment_count(1.0, kTwoPi, 0.0) == 8);
    CHECK(arc_segment_count(1.0, kTwoPi, -1.0) == 8);
}

TEST_CASE("the flattened circle stays within the chord tolerance") {
    const double tol = 0.05;
    const double r = 10.0;
    const int n = arc_segment_count(r, kTwoPi, tol);

    // Sagitta of the chord actually produced must not exceed the tolerance.
    const double sagitta = r * (1.0 - std::cos((kTwoPi / n) * 0.5));
    CHECK(sagitta <= tol);
}

TEST_CASE("Line draws as a single open two-point polyline") {
    RecordingRenderer r;
    Line l{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}};
    l.draw(fine(), r);

    CHECK(r.polys.size() == 1);
    CHECK(r.polys[0].pts.size() == 2);
    CHECK(!r.polys[0].closed);
    CHECK_VEC(r.polys[0].pts[0], 1.0, 2.0, 3.0, kEps);
    CHECK_VEC(r.polys[0].pts[1], 4.0, 5.0, 6.0, kEps);
}

TEST_CASE("Circle draws closed with no duplicated seam point") {
    RecordingRenderer r;
    Circle c{{0.0, 0.0, 0.0}, 5.0};
    c.draw(fine(), r);

    CHECK(r.polys.size() == 1);
    const auto& p = r.polys[0];
    CHECK(p.closed);
    CHECK(p.pts.size() == static_cast<std::size_t>(arc_segment_count(5.0, kTwoPi, 1e-4)));
    // A closed loop that also repeats its first point would draw a zero-length
    // segment and, later, confuse hit-testing.
    CHECK(!near_equal(p.pts.front(), p.pts.back()));
}

TEST_CASE("a tilted circle flattens into its own plane") {
    // The check that matters for ECS: every emitted point must lie on the
    // circle AND in the entity's plane, not in world XY.
    const Vec3 normal = normalize(Vec3{1.0, 2.0, 3.0});
    const Vec3 center{7.0, -3.0, 2.0};
    const double radius = 4.0;

    RecordingRenderer r;
    Circle c{center, radius, normal};
    c.draw(fine(), r);

    CHECK(r.polys.size() == 1);
    for (const Vec3& p : r.polys[0].pts) {
        const Vec3 offset = p - center;
        CHECK_NEAR(length(offset), radius, 1e-9);
        CHECK_NEAR(dot(offset, normal), 0.0, 1e-9);
    }
}

TEST_CASE("Arc draws open, spanning exactly its own endpoints") {
    const Vec3 normal = normalize(Vec3{0.0, -1.0, 1.0});
    Arc a{{1.0, 1.0, 1.0}, 3.0, 0.5, 2.0, normal};

    RecordingRenderer r;
    a.draw(fine(), r);

    CHECK(r.polys.size() == 1);
    const auto& p = r.polys[0];
    CHECK(!p.closed);
    CHECK(p.pts.size() >= 2);
    // The tessellation must land on the true endpoints, not near them --
    // otherwise adjoining geometry visibly fails to meet.
    CHECK_VEC(p.pts.front(), a.start_point().x, a.start_point().y, a.start_point().z, 1e-12);
    CHECK_VEC(p.pts.back(), a.end_point().x, a.end_point().y, a.end_point().z, 1e-12);
}

TEST_CASE("a degenerate radius emits nothing rather than a spike") {
    RecordingRenderer r;
    Circle{{0.0, 0.0, 0.0}, 0.0}.draw(fine(), r);
    Arc{{0.0, 0.0, 0.0}, 0.0, 0.0, 1.0}.draw(fine(), r);
    CHECK(r.polys.empty());
}

TEST_CASE("tessellation density tracks the draw context") {
    RecordingRenderer coarse;
    RecordingRenderer detailed;
    Circle c{{0.0, 0.0, 0.0}, 1000.0};
    c.draw(DrawContext{10.0}, coarse);
    c.draw(DrawContext{0.01}, detailed);
    CHECK(detailed.polys[0].pts.size() > coarse.polys[0].pts.size());
}

TEST_CASE("draw_database walks in drawing order and announces each entity") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{2, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{3, 0, 0}));

    RecordingRenderer r;
    draw_database(db, fine(), r);

    CHECK(r.begin_calls == 3);
    CHECK(r.polys.size() == 3);
    CHECK_NEAR(r.polys[0].pts[1].x, 1.0, kEps);
    CHECK_NEAR(r.polys[1].pts[1].x, 2.0, kEps);
    CHECK_NEAR(r.polys[2].pts[1].x, 3.0, kEps);
}

TEST_CASE("frozen and switched-off layers are not drawn") {
    Database db;
    const LayerId hidden = db.add_layer("HIDDEN");
    const LayerId off = db.add_layer("OFF");
    const LayerId shown = db.add_layer("SHOWN");
    db.set_layer_frozen(hidden, true);
    db.set_layer_color(off, -7);  // R12 stores "off" as a negative colour

    for (LayerId id : {hidden, off, shown}) {
        auto l = std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0});
        l->props().layer = id;
        db.add(std::move(l));
    }

    RecordingRenderer r;
    draw_database(db, fine(), r);

    CHECK(r.polys.size() == 1);
    CHECK(r.polys[0].props.layer == shown);
}

TEST_CASE("begin_entity carries the props of the entity that follows") {
    Database db;
    auto l = std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0});
    l->props().color = 3;
    db.add(std::move(l));

    RecordingRenderer r;
    draw_database(db, fine(), r);

    CHECK(r.polys.size() == 1);
    CHECK(r.polys[0].props.color == 3);
}
