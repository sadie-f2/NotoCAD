// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "ncad/database.hpp"
#include "ncad/ecs.hpp"
#include "ncad/entities.hpp"
#include "ncad/highlight.hpp"
#include "ncad/render.hpp"
#include "ncad/scene.hpp"

#include <cmath>
#include <numbers>
#include <vector>

using namespace ncad;

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

TEST_CASE("render: HighlightRenderer forces the colour and touches nothing else") {
    RecordingRenderer rec;
    HighlightRenderer hi(rec, 6);

    EntityProps props;
    props.color = kColorByLayer;
    props.layer = 3;
    props.linetype = 2;
    hi.begin_entity(props);

    const Vec3 pts[3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}};
    hi.polyline(pts, 3, false);

    REQUIRE(rec.polys.size() == 1);
    CHECK(rec.polys[0].props.color == 6);
    // Everything else survives, which is what lets it chain over DashRenderer:
    // the linetype still has to reach the backend to be cut into dashes.
    CHECK(rec.polys[0].props.layer == 3);
    CHECK(rec.polys[0].props.linetype == 2);
    CHECK(rec.polys[0].pts.size() == 3);
    CHECK(!rec.polys[0].closed);
    CHECK_VEC(rec.polys[0].pts[2], 1.0, 1.0, 0.0, 1e-12);
}

TEST_CASE("render: draw_database can skip handles") {
    Database db;
    const Handle a = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));
    const Handle b = db.add(std::make_unique<Line>(Vec3{0, 1, 0}, Vec3{1, 1, 0}));
    const Handle c = db.add(std::make_unique<Line>(Vec3{0, 2, 0}, Vec3{1, 2, 0}));

    RecordingRenderer all;
    draw_database(db, DrawContext{}, all);
    CHECK(all.polys.size() == 3);

    RecordingRenderer some;
    draw_database(db, DrawContext{}, some, std::vector<Handle>{b});
    REQUIRE(some.polys.size() == 2);
    // Drawing order is preserved for what remains -- skipping is a filter, not
    // a reordering.
    CHECK_VEC(some.polys[0].pts[0], 0.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(some.polys[1].pts[0], 0.0, 2.0, 0.0, 1e-12);

    // An empty skip list is the ordinary walk, which is what COPY passes.
    RecordingRenderer none;
    draw_database(db, DrawContext{}, none, std::vector<Handle>{});
    CHECK(none.polys.size() == 3);

    RecordingRenderer every;
    draw_database(db, DrawContext{}, every, std::vector<Handle>{a, b, c});
    CHECK(every.polys.empty());
}

TEST_CASE("render: draw_handles takes a subset, in the order given") {
    Database db;
    const Handle a = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));
    const Handle b = db.add(std::make_unique<Line>(Vec3{0, 1, 0}, Vec3{1, 1, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 2, 0}, Vec3{1, 2, 0}));

    RecordingRenderer rec;
    // Selection order, which is deliberately not drawing order.
    draw_handles(db, DrawContext{}, rec, std::vector<Handle>{b, a});
    REQUIRE(rec.polys.size() == 2);
    CHECK_VEC(rec.polys[0].pts[0], 0.0, 1.0, 0.0, 1e-12);
    CHECK_VEC(rec.polys[1].pts[0], 0.0, 0.0, 0.0, 1e-12);

    // A dangling handle is skipped rather than crashing: a selection can
    // outlive the entities in it.
    db.erase(a);
    RecordingRenderer after;
    draw_handles(db, DrawContext{}, after, std::vector<Handle>{b, a});
    CHECK(after.polys.size() == 1);
}

TEST_CASE("render: draw_handles still honours layer visibility") {
    // A handle held by AutoLISP can name an entity on a frozen layer. Drawing
    // it because it happens to be selected would put geometry on screen that
    // the drawing says is not there.
    Database db;
    const LayerId hidden = db.add_layer("HIDDEN");
    auto l = std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0});
    l->props().layer = hidden;
    const Handle h = db.add(std::move(l));

    RecordingRenderer visible;
    draw_handles(db, DrawContext{}, visible, std::vector<Handle>{h});
    CHECK(visible.polys.size() == 1);

    db.set_layer_frozen(hidden, true);
    RecordingRenderer frozen;
    draw_handles(db, DrawContext{}, frozen, std::vector<Handle>{h});
    CHECK(frozen.polys.empty());
}

TEST_CASE("render: draw_entities draws things the database has never seen") {
    // In-flight ghosts are clones a command is holding and has not committed,
    // so they have no handle and no place in drawing order.
    std::vector<EntityPtr> ghosts;
    ghosts.push_back(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{5, 0, 0}));
    ghosts.push_back(std::make_unique<Circle>(Vec3{0, 0, 0}, 2.0));
    ghosts.push_back(nullptr);  // tolerated rather than dereferenced

    RecordingRenderer rec;
    draw_entities(ghosts, DrawContext{0.01}, rec);

    CHECK(rec.begin_calls == 2);
    REQUIRE(rec.polys.size() >= 2);
    CHECK_VEC(rec.polys[0].pts[1], 5.0, 0.0, 0.0, 1e-12);
    // The circle is flattened by the same tolerance the database walk uses.
    CHECK(rec.polys[1].pts.size() > 8);
}
