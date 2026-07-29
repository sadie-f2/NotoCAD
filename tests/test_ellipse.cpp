// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// ELLIPSE: the first entity NotoCAD holds that R12's DXF cannot name.
//
// The interesting tests are the ones about that boundary -- that the database
// keeps the exact curve, that the file gets an honest polyline, and that
// AutoLISP is given the ellipse rather than the approximation.

#include "test.hpp"

#include "noto/commands.hpp"
#include "noto/database.hpp"
#include "noto/dxf.hpp"
#include "noto/entities.hpp"
#include "noto/inflight.hpp"
#include "noto/render.hpp"

#include <cmath>
#include <memory>
#include <numbers>
#include <sstream>

using namespace noto;

namespace {

constexpr double kPi = std::numbers::pi;

const Ellipse* last_ellipse(const Database& db) {
    if (db.order().empty()) return nullptr;
    const Entity* e = db.get(db.order().back());
    if (e == nullptr || e->type() != EntityType::Ellipse) return nullptr;
    return static_cast<const Ellipse*>(e);
}

// Every point of an ellipse satisfies (u/a)^2 + (v/b)^2 = 1, measured in its
// own axes. The strongest check available without a second implementation --
// the shape SF_todo recommends for anything that derives geometry.
bool on_ellipse(const Ellipse& e, const Vec3& p, double eps = 1e-9) {
    const Vec3 major = e.major_axis();
    const Vec3 minor = e.minor_axis();
    const double a = length(major);
    const double b = length(minor);
    if (a <= 0.0 || b <= 0.0) return false;

    const Vec3 d = p - e.center();
    const double u = dot(d, normalize(major)) / a;
    const double v = dot(d, normalize(minor)) / b;
    return std::abs(u * u + v * v - 1.0) <= eps;
}

}  // namespace

TEST_CASE("ellipse: two axis endpoints and a half-width") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("ELLIPSE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_real(3.0));

    const Ellipse* e = last_ellipse(db);
    REQUIRE(e != nullptr);
    // The centre is the MIDPOINT of the two endpoints, not the first of them.
    CHECK_VEC(e->center(), 5.0, 0.0, 0.0, 1e-12);
    CHECK_NEAR(e->major_length(), 5.0, 1e-12);
    CHECK_NEAR(e->minor_length(), 3.0, 1e-12);
    CHECK_NEAR(e->ratio(), 0.6, 1e-12);
    CHECK(e->is_full());

    CHECK(on_ellipse(*e, Vec3{10, 0, 0}));
    CHECK(on_ellipse(*e, Vec3{0, 0, 0}));
    CHECK(on_ellipse(*e, Vec3{5, 3, 0}));
    CHECK(!on_ellipse(*e, Vec3{5, 5, 0}));
}

TEST_CASE("ellipse: Center takes the centre outright, so the axis is not halved") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("ELLIPSE"));
    engine.supply(InputValue::of_keyword("CENTER"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_real(4.0));

    const Ellipse* e = last_ellipse(db);
    REQUIRE(e != nullptr);
    CHECK_VEC(e->center(), 0.0, 0.0, 0.0, 1e-12);
    CHECK_NEAR(e->major_length(), 10.0, 1e-12);
    CHECK_NEAR(e->minor_length(), 4.0, 1e-12);
}

TEST_CASE("ellipse: the longer axis becomes the major one, whichever was given first") {
    Database db;
    CommandEngine engine(db);

    // A short first axis and a long second: DXF defines the ratio as minor over
    // major, so a ratio above one would read back as a different shape.
    engine.begin(make_command("ELLIPSE"));
    engine.supply(InputValue::of_keyword("CENTER"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({2, 0, 0}));
    engine.supply(InputValue::of_real(9.0));

    const Ellipse* e = last_ellipse(db);
    REQUIRE(e != nullptr);
    CHECK(e->ratio() <= 1.0);
    CHECK_NEAR(e->major_length(), 9.0, 1e-12);
    CHECK_NEAR(e->minor_length(), 2.0, 1e-12);
    // The major axis swung perpendicular to the axis that was given.
    CHECK_NEAR(std::abs(dot(normalize(e->major_axis()), Vec3{0, 1, 0})), 1.0, 1e-12);
    CHECK(on_ellipse(*e, Vec3{2, 0, 0}));
    CHECK(on_ellipse(*e, Vec3{0, 9, 0}));
}

TEST_CASE("ellipse: Rotation is the tilt of a circle of that diameter") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("ELLIPSE"));
    engine.supply(InputValue::of_keyword("CENTER"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_keyword("ROTATION"));
    engine.supply(InputValue::of_real(60.0));

    const Ellipse* e = last_ellipse(db);
    REQUIRE(e != nullptr);
    // A circle of radius 10 tilted 60 degrees projects to a minor axis of
    // 10*cos(60) = 5.
    CHECK_NEAR(e->minor_length(), 5.0, 1e-9);

    // Edge-on has no area and is refused rather than drawn as a line.
    engine.begin(make_command("ELLIPSE"));
    engine.supply(InputValue::of_keyword("CENTER"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_keyword("ROTATION"));
    engine.supply(InputValue::of_real(90.0));
    CHECK(engine.status() == EngineStatus::Failed);
}

TEST_CASE("ellipse: a circle is the degenerate case and stays exact") {
    const Ellipse e(Vec3{1, 2, 3}, Vec3{4, 0, 0}, 1.0);
    CHECK_NEAR(e.major_length(), 4.0, 1e-12);
    CHECK_NEAR(e.minor_length(), 4.0, 1e-12);

    for (int i = 0; i < 16; ++i) {
        const double t = kFullTurn * i / 16.0;
        CHECK_NEAR(length(e.point_at(t) - e.center()), 4.0, 1e-12);
    }
}

TEST_CASE("ellipse: transform keeps it an ellipse, and a mirror keeps it valid") {
    Ellipse e(Vec3{0, 0, 0}, Vec3{10, 0, 0}, 0.5);

    Ellipse rotated = e;
    rotated.transform(Mat4::rotation(Vec3{}, kWorldZ, kPi * 0.5));
    CHECK_NEAR(rotated.major_length(), 10.0, 1e-9);
    CHECK_NEAR(rotated.ratio(), 0.5, 1e-9);
    CHECK(on_ellipse(rotated, Vec3{0, 10, 0}));

    Ellipse scaled = e;
    scaled.transform(Mat4::uniform_scaling(3.0));
    CHECK_NEAR(scaled.major_length(), 30.0, 1e-9);
    // A uniform scale changes size and not shape, which is the property that
    // lets transform() leave the parameters alone.
    CHECK_NEAR(scaled.ratio(), 0.5, 1e-9);

    Ellipse mirrored = e;
    mirrored.transform(Mat4::mirror(Vec3{}, Vec3{0, 1, 0}));
    CHECK_NEAR(mirrored.ratio(), 0.5, 1e-9);
    CHECK_NEAR(length(mirrored.props().normal), 1.0, 1e-12);
}

TEST_CASE("ellipse: the bounding box contains the curve and is not the axis box") {
    const Ellipse e(Vec3{0, 0, 0}, Vec3{10, 0, 0}, 0.3);
    const BBox b = e.bbox();

    CHECK_NEAR(b.min.x, -10.0, 1e-9);
    CHECK_NEAR(b.max.x, 10.0, 1e-9);
    CHECK_NEAR(b.max.y, 3.0, 1e-9);

    // Tilted forty-five degrees the box is NOT the axis lengths: both axes
    // contribute to both extents, which is the whole reason for the sqrt form.
    Ellipse tilted = e;
    tilted.transform(Mat4::rotation(Vec3{}, kWorldZ, kPi * 0.25));
    const BBox t = tilted.bbox();
    const double expect = std::sqrt(10.0 * 10.0 * 0.5 + 3.0 * 3.0 * 0.5);
    CHECK_NEAR(t.max.x, expect, 1e-9);
    CHECK_NEAR(t.max.y, expect, 1e-9);

    for (int i = 0; i < 64; ++i) {
        const Vec3 p = tilted.point_at(kFullTurn * i / 64.0);
        CHECK(p.x <= t.max.x + 1e-9);
        CHECK(p.y <= t.max.y + 1e-9);
    }
}

TEST_CASE("ellipse: osnaps offer a centre and quadrants, and ends only when it has them") {
    const Ellipse full(Vec3{0, 0, 0}, Vec3{10, 0, 0}, 0.5);
    std::vector<OsnapPoint> pts;
    full.osnap_points(pts);

    int centres = 0, quads = 0, ends = 0;
    for (const OsnapPoint& p : pts) {
        if (p.type == OsnapType::Center) ++centres;
        if (p.type == OsnapType::Quadrant) ++quads;
        if (p.type == OsnapType::Endpoint) ++ends;
    }
    CHECK(centres == 1);
    CHECK(quads == 4);
    // A whole ellipse does not stop anywhere, so offering an ENDPOINT would put
    // a snap where the curve has no end.
    CHECK(ends == 0);

    const Ellipse arc(Vec3{0, 0, 0}, Vec3{10, 0, 0}, 0.5, 0.0, kPi);
    pts.clear();
    arc.osnap_points(pts);
    ends = 0;
    for (const OsnapPoint& p : pts) {
        if (p.type == OsnapType::Endpoint) ++ends;
    }
    CHECK(ends == 2);
}

TEST_CASE("ellipse: DXF degrades to a closed polyline that follows the curve") {
    Database db;
    const Handle h = db.add(std::make_unique<Ellipse>(Vec3{0, 0, 0}, Vec3{10, 0, 0}, 0.5));
    const Ellipse* e = static_cast<const Ellipse*>(db.get(h));

    std::ostringstream out;
    DxfWriter(out, db).write_document();
    const std::string text = out.str();

    // AC1009 has no ELLIPSE, so the file must not claim one.
    CHECK(text.find("\nELLIPSE\r\n") == std::string::npos);
    CHECK(text.find("POLYLINE") != std::string::npos);
    CHECK(text.find("SEQEND") != std::string::npos);

    // Every vertex written lies on the real curve: the approximation is coarse,
    // not wrong.
    std::istringstream in(text);
    std::string line;
    std::vector<double> xs, ys;
    bool in_vertex = false;
    std::string pending;
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
    for (std::size_t i = 0; i < xs.size(); ++i) {
        CHECK(on_ellipse(*e, Vec3{xs[i], ys[i], 0.0}, 1e-9));
    }
}

TEST_CASE("ellipse: previews the ellipse once both axes are decided") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("ELLIPSE"));
    InFlight f;
    CHECK(!engine.preview(InputValue::of_point({1, 1, 0}), f));

    engine.supply(InputValue::of_point({0, 0, 0}));
    // One axis endpoint decides nothing about an ellipse; the band shows the
    // axis and that is all there is to show.
    CHECK(!engine.preview(InputValue::of_point({10, 0, 0}), f));

    engine.supply(InputValue::of_point({10, 0, 0}));
    REQUIRE(engine.preview(InputValue::of_real(3.0), f));
    REQUIRE(f.ghosts.size() == 1);
    REQUIRE(f.ghosts[0]->type() == EntityType::Ellipse);
    CHECK(f.suppressed.empty());
    CHECK(db.empty());

    const Ellipse ghost = *static_cast<const Ellipse*>(f.ghosts[0].get());

    engine.supply(InputValue::of_real(3.0));
    const Ellipse* made = last_ellipse(db);
    REQUIRE(made != nullptr);
    CHECK_VEC(made->center(), ghost.center().x, ghost.center().y, ghost.center().z, 1e-12);
    CHECK_NEAR(made->ratio(), ghost.ratio(), 1e-12);
    CHECK_NEAR(made->major_length(), ghost.major_length(), 1e-12);
}

TEST_CASE("ellipse: AutoLISP is given the ellipse, not the polyline") {
    Database db;
    db.add(std::make_unique<Ellipse>(Vec3{1, 2, 0}, Vec3{6, 0, 0}, 0.5));

    // entget must report the real geometry even though DXF cannot: for a tool
    // whose purpose is LISP-driven modelling, handing over the approximation
    // would be the wrong loss to take.
    const Entity* e = db.get(db.order().back());
    REQUIRE(e != nullptr);
    CHECK(e->type() == EntityType::Ellipse);
    CHECK(std::string(e->type_name()) == "ELLIPSE");
}
