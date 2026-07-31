// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The deferred tangent: TANGENT taken as the FIRST point of a line.
//
// There is no tangent until the line has another end, so the snap can only
// record which entity was pointed at and where. LINE holds that constraint and
// solves it when the second point arrives -- so the start slides along the
// curve as the far end moves, instead of freezing where it was picked.
//
// Everything is checked against the definition of tangency: the chord from the
// far end meets the radius at a right angle. Nothing here compares against a
// reference implementation.

#include "test.hpp"

#include "ncad/command.hpp"
#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/entities.hpp"
#include "ncad/inflight.hpp"

#include <cmath>
#include <vector>

using namespace ncad;

namespace {

const Line* last_line(const Database& db) {
    const Line* found = nullptr;
    for (const Handle h : db.order()) {
        const Entity* e = db.get(h);
        if (e != nullptr && e->type() == EntityType::Line) found = static_cast<const Line*>(e);
    }
    return found;
}

// The defining condition, stated once: P is a tangent point on the circle from
// `from` when it is on the circle and the chord is perpendicular to the radius.
void check_tangent(const Vec3& p, const Vec3& centre, double radius, const Vec3& from) {
    CHECK_NEAR(length(p - centre), radius, 1e-7);
    CHECK_NEAR(dot(normalize(p - from), normalize(p - centre)), 0.0, 1e-7);
}

}  // namespace

TEST_CASE("deferred tangent: the start moves to a true tangent when the far end lands") {
    Database db;
    const Handle circle = db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 50.0));

    CommandEngine engine(db);
    engine.begin(make_command("LINE"));

    // Pointed at the right-hand side of the circle, with no far end yet. This
    // is the position the marker was drawn at, not an answer.
    const Vec3 hint{50, 0, 0};
    engine.supply(InputValue::of_deferred_snap(hint, OsnapType::Tangent, circle));
    engine.supply(InputValue::of_point({0, 200, 0}));

    const Line* l = last_line(db);
    REQUIRE(l != nullptr);
    CHECK_VEC(l->end(), 0.0, 200.0, 0.0, 1e-9);

    // The whole point: the start is NOT where it was picked.
    CHECK(!near_equal(l->start(), hint, 1e-6));
    check_tangent(l->start(), {0, 0, 0}, 50.0, l->end());
}

TEST_CASE("deferred tangent: where you pointed chooses between the two answers") {
    // A circle has two tangents from any outside point. Only the user knows
    // which side they meant, and where they pointed is the whole of that
    // information.
    Database db;
    const Handle circle = db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 50.0));
    const Vec3 far{0, 200, 0};

    Vec3 got[2];
    int i = 0;
    for (const Vec3& hint : {Vec3{50, 0, 0}, Vec3{-50, 0, 0}}) {
        CommandEngine engine(db);
        engine.begin(make_command("LINE"));
        engine.supply(InputValue::of_deferred_snap(hint, OsnapType::Tangent, circle));
        engine.supply(InputValue::of_point(far));

        const Line* l = last_line(db);
        REQUIRE(l != nullptr);
        check_tangent(l->start(), {0, 0, 0}, 50.0, far);
        got[i++] = l->start();
    }

    // Pointing at opposite sides has to give opposite answers, or the hint is
    // being ignored and one of the two is simply always winning.
    CHECK(got[0].x > 0.0);
    CHECK(got[1].x < 0.0);
}

TEST_CASE("deferred tangent: the preview slides as the far end moves") {
    Database db;
    const Handle circle = db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 50.0));

    CommandEngine engine(db);
    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_deferred_snap({50, 0, 0}, OsnapType::Tangent, circle));

    // Two different tentative far ends give two different ghost starts, which is
    // the visible difference from the old behaviour -- the start used to be
    // frozen wherever it was first picked.
    std::vector<Vec3> starts;
    for (const Vec3& toward : {Vec3{0, 200, 0}, Vec3{0, -200, 0}}) {
        InFlight flight;
        REQUIRE(engine.preview(InputValue::of_point(toward), flight));
        REQUIRE(flight.ghosts.size() == 1);
        REQUIRE(flight.ghosts[0]->type() == EntityType::Line);

        const Line& ghost = static_cast<const Line&>(*flight.ghosts[0]);
        check_tangent(ghost.start(), {0, 0, 0}, 50.0, toward);
        starts.push_back(ghost.start());
    }
    CHECK(!near_equal(starts[0], starts[1], 1e-6));

    // And the preview committed nothing, which is InFlight's standing rule.
    CHECK(last_line(db) == nullptr);
}

TEST_CASE("deferred tangent: an ellipse resolves too, exactly") {
    // The closed-form ellipse tangent is what makes this worth having: the
    // deferred machinery is entity-agnostic because tangent_points() is.
    Database db;
    const Handle e = db.add(std::make_unique<Ellipse>(Vec3{0, 0, 0}, Vec3{60, 0, 0}, 0.5));

    CommandEngine engine(db);
    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_deferred_snap({60, 0, 0}, OsnapType::Tangent, e));

    const Vec3 far{0, 150, 0};
    engine.supply(InputValue::of_point(far));

    const Line* l = last_line(db);
    REQUIRE(l != nullptr);

    // On the ellipse...
    const double x = l->start().x / 60.0;
    const double y = l->start().y / 30.0;
    CHECK_NEAR(x * x + y * y, 1.0, 1e-7);

    // ...and genuinely tangent: the chord is parallel to the curve there. The
    // direction comes from a finite difference so this does not borrow the
    // solver's algebra.
    const Ellipse& el = static_cast<const Ellipse&>(*db.get(e));
    double best_t = 0.0, best = 1e30;
    for (int i = 0; i <= 20000; ++i) {
        const double t = 6.283185307179586 * static_cast<double>(i) / 20000.0;
        const double d = length_sq(el.point_at(t) - l->start());
        if (d < best) {
            best = d;
            best_t = t;
        }
    }
    const Vec3 dir = normalize(el.point_at(best_t + 1e-6) - el.point_at(best_t - 1e-6));
    CHECK(length(cross(normalize(l->start() - far), dir)) < 1e-3);
}

TEST_CASE("deferred tangent: an impossible one keeps the point rather than failing") {
    // The far end inside the circle: no tangent exists from there. Refusing
    // would strand the command, so the provisional point stands and moving the
    // far end fixes it.
    Database db;
    const Handle circle = db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 50.0));

    CommandEngine engine(db);
    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_deferred_snap({50, 0, 0}, OsnapType::Tangent, circle));
    engine.supply(InputValue::of_point({5, 5, 0}));

    const Line* l = last_line(db);
    REQUIRE(l != nullptr);
    CHECK_VEC(l->start(), 50.0, 0.0, 0.0, 1e-9);
}

TEST_CASE("deferred tangent: only the first segment is constrained") {
    Database db;
    const Handle circle = db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 50.0));

    CommandEngine engine(db);
    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_deferred_snap({50, 0, 0}, OsnapType::Tangent, circle));
    engine.supply(InputValue::of_point({0, 200, 0}));
    engine.supply(InputValue::of_point({300, 200, 0}));

    // The second segment starts where the first ended, with no tangent solving
    // left over to drag it somewhere else.
    const Line* l = last_line(db);
    REQUIRE(l != nullptr);
    CHECK_VEC(l->start(), 0.0, 200.0, 0.0, 1e-9);
    CHECK_VEC(l->end(), 300.0, 200.0, 0.0, 1e-9);
}
