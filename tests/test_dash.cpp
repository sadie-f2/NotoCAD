// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "ncad/dash.hpp"
#include "ncad/sysvar.hpp"
#include "ncad/database.hpp"
#include "ncad/entities.hpp"
#include "ncad/pick.hpp"
#include "ncad/scene.hpp"
#include "ncad/viewport.hpp"

#include <cmath>
#include <memory>

using namespace ncad;

namespace {

// Collects the runs a renderer is given, so the dashes can be measured.
class Capture final : public Renderer {
public:
    void begin_entity(const EntityProps&) override { ++entities; }
    void polyline(const Vec3* pts, std::size_t count, bool closed) override {
        runs.push_back(std::vector<Vec3>(pts, pts + count));
        closed_flags.push_back(closed);
    }

    double total_length() const {
        double total = 0.0;
        for (const std::vector<Vec3>& r : runs) {
            for (std::size_t i = 1; i < r.size(); ++i) total += length(r[i] - r[i - 1]);
        }
        return total;
    }

    std::vector<std::vector<Vec3>> runs;
    std::vector<bool> closed_flags;
    int entities{0};
};

// A drawing with one dashed line along +X, of the given length. Filled in place
// because Database owns a journal and is deliberately not copyable or movable.
void make_dashed_drawing(Database& db, double line_length, double ltscale = 1.0) {
    const LinetypeId dashed = db.add_linetype("DASHED", "dashed", {0.5, -0.25});
    const LayerId layer = db.add_layer("L");
    db.set_layer_linetype(layer, dashed);

    auto l = std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{line_length, 0, 0});
    l->props().layer = layer;
    db.add(std::move(l));

    db.sysvars().set_real(Sysvar::LtScale, ltscale);
}

}  // namespace

TEST_CASE("dash: a continuous linetype passes straight through") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    Capture cap;
    DashRenderer dashed(cap, db, 1.0);
    draw_database(db, DrawContext{}, dashed);

    // One run, unchanged. The common case must cost nothing.
    CHECK(cap.runs.size() == 1);
    CHECK(cap.runs[0].size() == 2);
    CHECK_NEAR(cap.total_length(), 10.0, 1e-12);
}

TEST_CASE("dash: a pattern cuts the line into pieces") {
    Database db;
    make_dashed_drawing(db, 3.0);

    Capture cap;
    DashRenderer dashed(cap, db, 1.0);
    draw_database(db, DrawContext{}, dashed);

    // 0.5 on, 0.25 off, repeating over three units: four dashes.
    CHECK(cap.runs.size() == 4);
    for (const std::vector<Vec3>& r : cap.runs) CHECK(r.size() == 2);

    // Two thirds of the length is drawn, which is 0.5 of every 0.75.
    CHECK_NEAR(cap.total_length(), 2.0, 1e-9);
}

TEST_CASE("dash: LTSCALE stretches the pattern") {
    Database db;
    make_dashed_drawing(db, 3.0, 2.0);

    Capture cap;
    DashRenderer dashed(cap, db, db.sysvars().get_real(Sysvar::LtScale));
    draw_database(db, DrawContext{}, dashed);

    // Twice the pattern length means half as many dashes over the same line,
    // and the same proportion drawn.
    CHECK(cap.runs.size() == 2);
    CHECK_NEAR(cap.total_length(), 2.0, 1e-9);
}

TEST_CASE("dash: every entity restarts its pattern") {
    Database db;
    const LinetypeId dashed = db.add_linetype("DASHED", "dashed", {0.5, -0.25});
    const LayerId layer = db.add_layer("L");
    db.set_layer_linetype(layer, dashed);

    // Two collinear lines, end to end.
    for (int i = 0; i < 2; ++i) {
        auto l = std::make_unique<Line>(Vec3{i * 3.0, 0, 0}, Vec3{i * 3.0 + 3.0, 0, 0});
        l->props().layer = layer;
        db.add(std::move(l));
    }

    Capture cap;
    DashRenderer dashed_r(cap, db, 1.0);
    draw_database(db, DrawContext{}, dashed_r);

    CHECK(cap.entities == 2);
    // Each begins with a dash rather than continuing one phase through the
    // drawing, which is what R12 does and why the second line's first dash
    // starts exactly at its own start point.
    CHECK(cap.runs.size() == 8);
    CHECK_VEC(cap.runs[0][0], 0.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(cap.runs[4][0], 3.0, 0.0, 0.0, 1e-12);
}

TEST_CASE("dash: an entity's own linetype beats its layer's") {
    Database db;
    const LinetypeId dashed = db.add_linetype("DASHED", "dashed", {0.5, -0.25});
    const LayerId layer = db.add_layer("L");
    db.set_layer_linetype(layer, dashed);

    auto l = std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{3, 0, 0});
    l->props().layer = layer;
    l->props().linetype = kLinetypeContinuous;
    db.add(std::move(l));

    // NOTE: continuous currently doubles as BYLAYER, so this inherits the
    // layer's dashes. Recorded in SF_todo.md -- when an explicit CONTINUOUS
    // becomes expressible this test says the opposite.
    Capture cap;
    DashRenderer dashed_r(cap, db, 1.0);
    draw_database(db, DrawContext{}, dashed_r);
    CHECK(cap.runs.size() == 4);
}

TEST_CASE("dash: a dashed outline is no longer a closed run") {
    Database db;
    const LinetypeId dashed = db.add_linetype("DASHED", "dashed", {0.5, -0.25});
    const LayerId layer = db.add_layer("L");
    db.set_layer_linetype(layer, dashed);

    auto c = std::make_unique<Circle>(Vec3{0, 0, 0}, 5.0);
    c->props().layer = layer;
    db.add(std::move(c));

    Capture cap;
    DashRenderer dashed_r(cap, db, 1.0);
    DrawContext ctx;
    ctx.chord_tolerance = 0.01;
    draw_database(db, ctx, dashed_r);

    CHECK(cap.runs.size() > 4);
    // Pieces of an outline are open, whatever the outline was.
    for (const bool closed : cap.closed_flags) CHECK(!closed);
}

TEST_CASE("dash: a degenerate pattern is treated as continuous") {
    Database db;
    const LinetypeId bad = db.add_linetype("BAD", "zero length", {0.0});
    const LayerId layer = db.add_layer("L");
    db.set_layer_linetype(layer, bad);

    auto l = std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{3, 0, 0});
    l->props().layer = layer;
    db.add(std::move(l));

    Capture cap;
    DashRenderer dashed_r(cap, db, 1.0);
    draw_database(db, DrawContext{}, dashed_r);
    // A pattern with no length would otherwise loop forever spending nothing.
    CHECK(cap.runs.size() >= 1);
    CHECK_NEAR(cap.total_length(), 3.0, 1e-6);
}

TEST_CASE("dash: hit-testing does not see the gaps") {
    Database db;
    make_dashed_drawing(db, 3.0);
    Viewport v;
    v.set_size(800, 600);
    v.set_view_height(6.0);
    v.set_plan_view();
    v.set_target({1.5, 0, 0});

    // The point at x = 0.6 falls in a gap: the first dash runs 0 to 0.5.
    const Entity* e = db.get(db.order().back());
    double d = 0.0;
    CHECK(entity_pick_distance(*e, v, v.project(Vec3{0.6, 0, 0}), &d));

    // Zero, because the probe drives Entity::draw() directly and never wraps in
    // a DashRenderer. This is the whole reason dashes are a wrapper: AutoCAD
    // picks a dashed line anywhere along it, gaps included.
    CHECK_NEAR(d, 0.0, 1e-6);
}

TEST_CASE("dash: a vanishingly small pattern draws continuous rather than hanging") {
    // The dashing loop spends one pattern element per iteration, so the count
    // is segment_length / period -- and nothing bounded the period from below.
    // LTSCALE is a Real sysvar and Real sysvars are not range-checked, so
    // an LTSCALE of 1e-6 on a 1000-unit line is ~1.3e9 iterations, each
    // emitting a two-point polyline downstream. A DXF's group 49 can do the
    // same with no user action at all.
    //
    // Below the floor a dash is far smaller than a pixel at any zoom, so
    // continuous is not a compromise -- it is what it would have looked like.
    Database db;
    make_dashed_drawing(db, 1000.0, 1.0e-6);

    Capture cap;
    DashRenderer dashed(cap, db, 1.0e-6);
    draw_database(db, DrawContext{}, dashed);

    // The real assertion is that this returns at all. One run rather than a
    // billion says it took the continuous path.
    CHECK(cap.runs.size() == 1);
    CHECK_NEAR(cap.total_length(), 1000.0, 1e-9);
}
