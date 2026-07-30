// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// SPLINE: a NURBS curve, and the largest divergence from R12 so far.
//
// The load-bearing property is that an interpolating spline PASSES THROUGH the
// points it was given. Everything else here is downstream of that: the grips
// move fit points, the osnaps offer them as NODE, and the DXF degrade is judged
// against the curve they define.

#include "test.hpp"

#include "noto/commands.hpp"
#include "noto/database.hpp"
#include "noto/dxf.hpp"
#include "noto/entities.hpp"
#include "noto/inflight.hpp"

#include <cmath>
#include <memory>
#include <numbers>
#include <sstream>

using namespace noto;

namespace {

constexpr double kPi = std::numbers::pi;

const Spline* last_spline(const Database& db) {
    if (db.order().empty()) return nullptr;
    const Entity* e = db.get(db.order().back());
    if (e == nullptr || e->type() != EntityType::Spline) return nullptr;
    return static_cast<const Spline*>(e);
}

std::unique_ptr<Spline> through(const std::vector<Vec3>& pts, int degree = 3) {
    EntityPtr e = Spline::interpolating(pts, degree);
    if (!e) return nullptr;
    return std::unique_ptr<Spline>(static_cast<Spline*>(e.release()));
}

// The nearest approach of the curve to p: a coarse sweep, then the bracket
// around the best sample refined repeatedly. Deliberately not a closed-form
// projection, which would share code with what is being tested.
//
// The refinement is not optional and the first version of this went without it.
// A single sweep can only ever get within half a sample spacing of the curve,
// so a point lying EXACTLY on it still measures a few parts in ten thousand out
// -- which reads as an interpolation that nearly works and is really a test
// that cannot see better than its own resolution. The bound below is 1e-9;
// without refining, nothing could ever pass it.
double distance_to_curve(const Spline& s, const Vec3& p) {
    double lo = s.domain_min();
    double hi = s.domain_max();
    double best = 1e300;

    for (int pass = 0; pass < 8; ++pass) {
        constexpr int kSteps = 2000;
        double at = lo;
        best = 1e300;
        for (int i = 0; i <= kSteps; ++i) {
            const double u = lo + (hi - lo) * (static_cast<double>(i) / kSteps);
            const double d = length(s.point_at(u) - p);
            if (d < best) {
                best = d;
                at = u;
            }
        }
        const double step = (hi - lo) / kSteps;
        lo = at - step;
        hi = at + step;
    }
    return best;
}

}  // namespace

TEST_CASE("spline: an interpolating curve passes through every point it was given") {
    const std::vector<Vec3> pts = {{0, 0, 0}, {10, 8, 0}, {20, -6, 0}, {30, 4, 0}, {40, 0, 0}};
    auto s = through(pts);
    REQUIRE(s != nullptr);
    REQUIRE(s->valid());

    // The whole reason fit points exist. Not "close to" -- the interpolation
    // solves for exactly this, so it should hold to solver precision.
    for (const Vec3& p : pts) {
        CHECK(distance_to_curve(*s, p) < 1e-9);
    }

    // And the ends are exact, not merely near.
    CHECK_VEC(s->start_point(), 0.0, 0.0, 0.0, 1e-9);
    CHECK_VEC(s->end_point(), 40.0, 0.0, 0.0, 1e-9);
}

TEST_CASE("spline: the fit points are kept, and the control points are not them") {
    const std::vector<Vec3> pts = {{0, 0, 0}, {10, 10, 0}, {20, -10, 0}, {30, 0, 0}};
    auto s = through(pts);
    REQUIRE(s != nullptr);

    CHECK(s->has_fit_points());
    REQUIRE(s->fit_points().size() == pts.size());
    CHECK_VEC(s->fit_points()[1], 10.0, 10.0, 0.0, 1e-12);

    // A NURBS curve does not generally touch its control points, and this one
    // does not -- which is exactly why a designer is given the fit points.
    CHECK(s->control_points().size() == pts.size());
    bool differs = false;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        if (!near_equal(s->control_points()[i], pts[i], 1e-6)) differs = true;
    }
    CHECK(differs);
}

TEST_CASE("spline: degree drops rather than failing when there are too few points") {
    // Two points cannot carry a cubic. Refusing would make the command reject
    // its own first two picks.
    auto line = through({{0, 0, 0}, {10, 0, 0}});
    REQUIRE(line != nullptr);
    CHECK(line->degree() == 1);
    CHECK(line->valid());
    CHECK_VEC(line->point_at(line->domain_max() * 0.5), 5.0, 0.0, 0.0, 1e-9);

    auto quad = through({{0, 0, 0}, {5, 5, 0}, {10, 0, 0}});
    REQUIRE(quad != nullptr);
    CHECK(quad->degree() == 2);

    // One point is not a curve at all.
    CHECK(Spline::interpolating({{0, 0, 0}}) == nullptr);
    CHECK(Spline::interpolating({}) == nullptr);
    // Nor are two identical ones: the parameterisation would divide by zero.
    CHECK(Spline::interpolating({{1, 1, 0}, {1, 1, 0}}) == nullptr);
}

TEST_CASE("spline: an invalid curve is inert rather than dangerous") {
    // A knot vector that does not match the control points, which entmake or a
    // file can produce. Every accessor runs in the render path, so none of them
    // may read past an array.
    Spline bad(3, {{0, 0, 0}, {1, 1, 0}}, {0.0, 1.0});
    CHECK(!bad.valid());
    CHECK_VEC(bad.point_at(0.5), 0.0, 0.0, 0.0, 1e-12);
    CHECK(is_zero(bad.tangent_at(0.5)));
    CHECK(bad.segment_count(0.1) == 0);

    std::vector<OsnapPoint> snaps;
    bad.osnap_points(snaps);
    CHECK(snaps.empty());

    CHECK(!bad.bbox().valid());
}

TEST_CASE("spline: transform moves the control points and nothing else") {
    const std::vector<Vec3> pts = {{0, 0, 0}, {10, 5, 0}, {20, -5, 0}, {30, 0, 0}};
    auto s = through(pts);
    REQUIRE(s != nullptr);

    const std::vector<double> knots_before = s->knots();
    const int degree_before = s->degree();

    s->transform(Mat4::rotation(Vec3{}, kWorldZ, kPi * 0.5));

    // The basis functions live in parameter space, so an affine map is exact on
    // the control points and the knots do not move. That is the property that
    // makes this the cheapest transform in the kernel.
    CHECK(s->degree() == degree_before);
    REQUIRE(s->knots().size() == knots_before.size());
    for (std::size_t i = 0; i < knots_before.size(); ++i) {
        CHECK_NEAR(s->knots()[i], knots_before[i], 1e-15);
    }

    // And the curve still passes through the ROTATED points, which is the check
    // that the transform was applied to the geometry and not merely to a label.
    const Mat4 m = Mat4::rotation(Vec3{}, kWorldZ, kPi * 0.5);
    for (const Vec3& p : pts) {
        CHECK(distance_to_curve(*s, m.transform_point(p)) < 1e-9);
    }
}

TEST_CASE("spline: the bounding box contains the curve") {
    auto s = through({{0, 0, 0}, {10, 20, 0}, {20, -20, 0}, {30, 0, 0}});
    REQUIRE(s != nullptr);

    const BBox b = s->bbox();
    REQUIRE(b.valid());

    // The control polygon is the convex hull of the curve, so its box contains
    // the curve outright -- no sampling, and no bulge to step over.
    for (int i = 0; i <= 200; ++i) {
        const Vec3 p =
            s->point_at(s->domain_min() +
                        (s->domain_max() - s->domain_min()) * (static_cast<double>(i) / 200.0));
        CHECK(p.x >= b.min.x - 1e-9);
        CHECK(p.x <= b.max.x + 1e-9);
        CHECK(p.y >= b.min.y - 1e-9);
        CHECK(p.y <= b.max.y + 1e-9);
    }
}

TEST_CASE("spline: grips are the fit points, and naming them all is a translate") {
    const std::vector<Vec3> pts = {{0, 0, 0}, {10, 8, 0}, {20, -6, 0}, {30, 0, 0}};
    auto s = through(pts);
    REQUIRE(s != nullptr);

    std::vector<Grip> g;
    s->grips(g);
    REQUIRE(g.size() == pts.size());
    CHECK_VEC(g[1].pos, 10.0, 8.0, 0.0, 1e-12);

    // The invariant every Stretch grip has to satisfy, and the one STRETCH
    // degenerating into MOVE depends on. It holds here as a CONSEQUENCE --
    // interpolation is affine-equivariant, so re-solving from translated fit
    // points gives a translated curve -- rather than as a special case.
    const std::vector<GripIndex> all = {0, 1, 2, 3};
    const Vec3 delta{3, 4, 0};
    s->stretch(delta, all.data(), all.size());

    for (const Vec3& p : pts) {
        CHECK(distance_to_curve(*s, p + delta) < 1e-9);
    }
}

TEST_CASE("spline: moving one fit point re-solves and the curve still goes through it") {
    auto s = through({{0, 0, 0}, {10, 0, 0}, {20, 0, 0}, {30, 0, 0}});
    REQUIRE(s != nullptr);

    const std::vector<GripIndex> one = {1};
    s->stretch(Vec3{0, 12, 0}, one.data(), one.size());

    CHECK(distance_to_curve(*s, Vec3{10, 12, 0}) < 1e-9);
    // The others stayed where they were.
    CHECK(distance_to_curve(*s, Vec3{0, 0, 0}) < 1e-9);
    CHECK(distance_to_curve(*s, Vec3{30, 0, 0}) < 1e-9);
}

TEST_CASE("spline: osnaps offer the ends and the points the user chose") {
    auto s = through({{0, 0, 0}, {10, 8, 0}, {20, 0, 0}});
    REQUIRE(s != nullptr);

    std::vector<OsnapPoint> pts;
    s->osnap_points(pts);

    int ends = 0, nodes = 0, mids = 0;
    for (const OsnapPoint& p : pts) {
        if (p.type == OsnapType::Endpoint) ++ends;
        if (p.type == OsnapType::Node) ++nodes;
        if (p.type == OsnapType::Midpoint) ++mids;
    }
    CHECK(ends == 2);
    // NODE for the fit points: the curve demonstrably passes through them,
    // which is what NODE means. Control points are not offered, because a snap
    // to somewhere the geometry is not would be a lie.
    CHECK(nodes == 3);
    CHECK(mids == 1);
}

TEST_CASE("spline: DXF degrades to a polyline that follows the curve") {
    Database db;
    EntityPtr e = Spline::interpolating({{0, 0, 0}, {10, 8, 0}, {20, -6, 0}, {30, 0, 0}});
    REQUIRE(e != nullptr);
    const Spline* s = static_cast<const Spline*>(e.get());
    const Spline copy = *s;
    db.add(std::move(e));

    std::ostringstream out;
    DxfWriter(out, db).write_document();
    const std::string text = out.str();

    // AC1009 has no SPLINE, so the file must not claim one.
    CHECK(text.find("\nSPLINE\r\n") == std::string::npos);
    CHECK(text.find("POLYLINE") != std::string::npos);

    std::istringstream in(text);
    std::string line, pending;
    std::vector<double> xs, ys;
    bool in_vertex = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "VERTEX") {
            in_vertex = true;
            continue;
        }
        if (line == "SEQEND") in_vertex = false;
        if (!in_vertex) continue;
        if (line == "10" || line == "20") {
            pending = line;
            continue;
        }
        if (pending == "10") {
            xs.push_back(std::stod(line));
            pending.clear();
        } else if (pending == "20") {
            ys.push_back(std::stod(line));
            pending.clear();
        }
    }

    REQUIRE(xs.size() >= 8);
    REQUIRE(xs.size() == ys.size());
    // Every vertex written lies on the real curve: the approximation is coarse,
    // not wrong.
    for (std::size_t i = 0; i < xs.size(); ++i) {
        CHECK(distance_to_curve(copy, Vec3{xs[i], ys[i], 0.0}) < 1e-6);
    }
}

TEST_CASE("spline: the command grows a curve and previews the whole of it") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("SPLINE"));
    InFlight f;
    // One point is not a curve.
    CHECK(!engine.preview(InputValue::of_point({1, 1, 0}), f));

    engine.supply(InputValue::of_point({0, 0, 0}));
    REQUIRE(engine.preview(InputValue::of_point({10, 8, 0}), f));
    REQUIRE(f.ghosts.size() == 1);
    CHECK(f.ghosts[0]->type() == EntityType::Spline);
    // Nothing committed yet, so nothing to stand in for.
    CHECK(f.suppressed.empty());

    engine.supply(InputValue::of_point({10, 8, 0}));
    // Committed as it goes, the way PLINE is, so Escape keeps it.
    REQUIRE(db.size() == 1);

    InFlight g;
    REQUIRE(engine.preview(InputValue::of_point({20, -6, 0}), g));
    // The ghost is the WHOLE curve, so the committed one must be hidden or the
    // two draw over each other.
    REQUIRE(g.suppressed.size() == 1);

    const Spline ghost = *static_cast<const Spline*>(g.ghosts[0].get());
    engine.supply(InputValue::of_point({20, -6, 0}));
    engine.supply(InputValue::none());

    const Spline* made = last_spline(db);
    REQUIRE(made != nullptr);
    REQUIRE(made->control_points().size() == ghost.control_points().size());
    for (std::size_t i = 0; i < made->control_points().size(); ++i) {
        CHECK(near_equal(made->control_points()[i], ghost.control_points()[i], 1e-12));
    }
    CHECK(db.size() == 1);
}

TEST_CASE("spline: the command refuses fewer than two points and a repeated one") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("SPLINE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::none());
    CHECK(engine.status() == EngineStatus::Failed);
    CHECK(db.empty());

    engine.begin(make_command("SPLINE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({0, 0, 0}));
    CHECK(engine.status() == EngineStatus::Failed);
}

// --- flattening cost --------------------------------------------------------
//
// Found with gdb on a wedged viewport holding a million splines: every sample
// landed in Spline::draw. These pin the two decisions that came out of it.

TEST_CASE("spline: segments are bounded by the size on screen") {
    const EntityPtr s = Spline::interpolating({{0, 0, 0}, {3, 8, 0}, {8, -3, 0}, {11, 5, 0}});
    REQUIRE(s != nullptr);
    const Spline& sp = static_cast<const Spline&>(*s);

    // Zoomed in: the control-point floor governs, so the curve keeps its shape.
    const int close = sp.segment_count(0.001);
    CHECK(close >= static_cast<int>(sp.control_points().size()) * 4);

    // Zoomed out until the whole curve is a few pixels: detail below a pixel
    // cannot be seen, and emitting sixteen segments for it was the whole cost
    // of a frame in a drawing zoomed out to a million curves.
    const int far_out = sp.segment_count(100.0);
    CHECK(far_out < close);
    CHECK(far_out >= 1);  // never zero, or it draws nothing at all

    // Monotonic: a coarser tolerance never costs more.
    CHECK(sp.segment_count(10.0) <= sp.segment_count(1.0));
    CHECK(sp.segment_count(1.0) <= sp.segment_count(0.1));
}

TEST_CASE("spline: a sub-pixel curve is one segment, not sixteen") {
    const EntityPtr s = Spline::interpolating({{0, 0, 0}, {3, 8, 0}, {8, -3, 0}, {11, 5, 0}});
    const Spline& sp = static_cast<const Spline&>(*s);
    // A tolerance far larger than the curve itself.
    CHECK(sp.segment_count(10000.0) == 1);
}

TEST_CASE("spline: the degree is bounded, and a wilder one is not valid") {
    // The bound is what lets the evaluator keep its basis scratch on the stack,
    // which is why it exists at all -- see kMaxSplineDegree.
    std::vector<Vec3> control(kMaxSplineDegree + 3, Vec3{0, 0, 0});
    for (std::size_t i = 0; i < control.size(); ++i) control[i] = Vec3{double(i), 0, 0};

    std::vector<double> knots(control.size() + kMaxSplineDegree + 1 + 1, 0.0);
    for (std::size_t i = 0; i < knots.size(); ++i) knots[i] = double(i);

    const Spline too_wild(kMaxSplineDegree + 1, control, knots);
    CHECK(!too_wild.valid());
    // And an unusable spline evaluates to nothing rather than reading off the
    // end of a stack buffer.
    CHECK_VEC(too_wild.point_at(0.5), 0.0, 0.0, 0.0, 1e-12);
}
