// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// POINT, SOLID, 3DFACE and TEXT: the entities phase 7 added.

#include "test.hpp"

#include "ncad/database.hpp"
#include "ncad/dxf.hpp"
#include "ncad/dxf_read.hpp"
#include "ncad/entities.hpp"
#include "ncad/render.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <sstream>

using namespace ncad;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kTol = 1e-9;

class Capture final : public Renderer {
public:
    void begin_entity(const EntityProps&) override {}
    void polyline(const Vec3* pts, std::size_t count, bool closed) override {
        runs.push_back(std::vector<Vec3>(pts, pts + count));
        closed_flags.push_back(closed);
    }
    std::vector<std::vector<Vec3>> runs;
    std::vector<bool> closed_flags;
};

bool has_snap(const std::vector<OsnapPoint>& pts, OsnapType t, const Vec3& at) {
    for (const OsnapPoint& p : pts) {
        if (p.type == t && near_equal(p.pos, at, 1e-6)) return true;
    }
    return false;
}

// A unit square as R12 stores a SOLID: corners 3 and 4 run ACROSS the shape.
Face square_solid() {
    Face f(EntityType::Solid);
    f.set_corner(0, {0, 0, 0});
    f.set_corner(1, {10, 0, 0});
    f.set_corner(2, {0, 10, 0});
    f.set_corner(3, {10, 10, 0});
    return f;
}

}  // namespace

TEST_CASE("point: offers NODE, and nothing else does") {
    const PointEntity p({3, 4, 5});
    std::vector<OsnapPoint> pts;
    p.osnap_points(pts);
    CHECK(pts.size() == 1);
    CHECK(pts[0].type == OsnapType::Node);
    CHECK_VEC(pts[0].pos, 3.0, 4.0, 5.0, kTol);
}

TEST_CASE("point: draws a marker that does not scale with the drawing") {
    const PointEntity p({0, 0, 0});

    Capture near_view;
    DrawContext fine;
    fine.chord_tolerance = 0.001;
    p.draw(fine, near_view);

    Capture far_view;
    DrawContext coarse;
    coarse.chord_tolerance = 0.1;
    p.draw(coarse, far_view);

    // Both draw a cross; the zoomed-out one is bigger in world units, which is
    // what keeps it the same size on screen.
    CHECK(near_view.runs.size() == 2);
    CHECK(far_view.runs.size() == 2);
    const double near_span = length(near_view.runs[0][1] - near_view.runs[0][0]);
    const double far_span = length(far_view.runs[0][1] - far_view.runs[0][0]);
    CHECK(far_span > near_span);
}

TEST_CASE("point: bbox is the point, and it moves and stretches") {
    PointEntity p({1, 2, 3});
    CHECK(p.bbox().valid());
    CHECK_VEC(p.bbox().min, 1.0, 2.0, 3.0, kTol);

    p.transform(Mat4::translation({1, 1, 1}));
    CHECK_VEC(p.position(), 2.0, 3.0, 4.0, kTol);

    const GripIndex zero = 0;
    p.stretch({0, 0, 1}, &zero, 1);
    CHECK_VEC(p.position(), 2.0, 3.0, 5.0, kTol);
}

TEST_CASE("solid: draws as a loop, not as a bowtie") {
    const Face f = square_solid();
    Capture cap;
    f.draw(DrawContext{}, cap);

    CHECK(cap.runs.size() == 1);
    CHECK(cap.closed_flags[0]);
    CHECK(cap.runs[0].size() == 4);

    // R12 stores corners 3 and 4 across the shape. Walking them as stored gives
    // a bowtie; the drawn order has to swap them back.
    CHECK_VEC(cap.runs[0][0], 0.0, 0.0, 0.0, kTol);
    CHECK_VEC(cap.runs[0][1], 10.0, 0.0, 0.0, kTol);
    CHECK_VEC(cap.runs[0][2], 10.0, 10.0, 0.0, kTol);
    CHECK_VEC(cap.runs[0][3], 0.0, 10.0, 0.0, kTol);
}

TEST_CASE("solid: a repeated fourth corner means a triangle") {
    Face f(EntityType::Solid);
    f.set_corner(0, {0, 0, 0});
    f.set_corner(1, {10, 0, 0});
    f.set_corner(2, {0, 10, 0});
    f.set_corner(3, {0, 10, 0});  // the format's way of saying three-sided
    CHECK(f.triangular());

    Capture cap;
    f.draw(DrawContext{}, cap);
    CHECK(cap.runs[0].size() == 3);

    std::vector<Grip> g;
    f.grips(g);
    CHECK(g.size() == 3);
}

TEST_CASE("solid: snaps at corners and edge midpoints, in drawn order") {
    const Face f = square_solid();
    std::vector<OsnapPoint> pts;
    f.osnap_points(pts);

    CHECK(has_snap(pts, OsnapType::Endpoint, {0, 0, 0}));
    CHECK(has_snap(pts, OsnapType::Endpoint, {10, 10, 0}));
    // Midpoints follow the outline, so this is an edge of the square rather
    // than the diagonal the stored order would suggest.
    CHECK(has_snap(pts, OsnapType::Midpoint, {5, 0, 0}));
    CHECK(has_snap(pts, OsnapType::Midpoint, {10, 5, 0}));
    CHECK(!has_snap(pts, OsnapType::Midpoint, {5, 5, 0}));
}

TEST_CASE("solid: rotating out of plane records the new normal") {
    Face f = square_solid();
    CHECK_VEC(f.props().normal, 0.0, 0.0, 1.0, kTol);

    f.transform(Mat4::rotation({0, 0, 0}, {1, 0, 0}, kPi * 0.5));
    // Otherwise the extrusion would disagree with the geometry, and DXF would
    // store corners in a plane the entity is no longer in.
    CHECK_VEC(f.props().normal, 0.0, -1.0, 0.0, 1e-9);
}

TEST_CASE("3dface: hidden edges are not drawn") {
    Face f(EntityType::Face3d);
    f.set_corner(0, {0, 0, 0});
    f.set_corner(1, {10, 0, 0});
    f.set_corner(2, {0, 10, 5});
    f.set_corner(3, {10, 10, 5});

    Capture all;
    f.draw(DrawContext{}, all);
    CHECK(all.runs.size() == 4);  // one per edge, each open

    // Bit 1 hides the first edge, which is what makes a mesh of faces look
    // like a surface rather than a wire grid.
    f.set_edge_flags(1);
    Capture some;
    f.draw(DrawContext{}, some);
    CHECK(some.runs.size() == 3);
    CHECK(!f.edge_visible(0));
    CHECK(f.edge_visible(1));
}

TEST_CASE("3dface: a triangle's third and fourth corners move together") {
    Face f(EntityType::Face3d);
    f.set_corner(0, {0, 0, 0});
    f.set_corner(1, {10, 0, 0});
    f.set_corner(2, {5, 10, 0});
    f.set_corner(3, {5, 10, 0});

    const GripIndex two = 2;
    f.stretch({0, 0, 5}, &two, 1);
    // Or the shape splits open along an edge of zero length that suddenly has
    // one.
    CHECK(f.triangular());
    CHECK_VEC(f.corner(3), 5.0, 10.0, 5.0, kTol);
}

TEST_CASE("text: the entity holds what a font would need") {
    Text t({1, 2, 0}, "Hello", 0.25);
    CHECK(t.value() == "Hello");
    CHECK_NEAR(t.height(), 0.25, kTol);

    std::vector<OsnapPoint> pts;
    t.osnap_points(pts);
    // INSERT: the one place on a piece of text that means something exact.
    CHECK(pts.size() == 1);
    CHECK(pts[0].type == OsnapType::Insert);
    CHECK_VEC(pts[0].pos, 1.0, 2.0, 0.0, kTol);
}

TEST_CASE("text: glyphs are drawn as open strokes on the baseline") {
    Text t({0, 0, 0}, "ABC", 2.0);
    Capture cap;
    t.draw(DrawContext{}, cap);

    // Three letters of Roman Simplex are several separate pen-down runs -- A
    // alone is three. None of them is closed: a stroke font has no filled
    // shapes, and a closed run would join the end of a letter to its start.
    CHECK(cap.runs.size() >= 6);
    for (std::size_t i = 0; i < cap.runs.size(); ++i) {
        CHECK(!cap.closed_flags[i]);
        CHECK(cap.runs[i].size() >= 2);
    }

    double lo = 1e9, hi = -1e9, right = -1e9;
    for (const auto& run : cap.runs) {
        for (const Vec3& p : run) {
            lo = std::min(lo, p.y);
            hi = std::max(hi, p.y);
            right = std::max(right, p.x);
            CHECK(p.x >= -kTol);
        }
    }

    // Height means CAP height, so uppercase reaches exactly it, and none of
    // these three letters descends below the baseline.
    CHECK_NEAR(hi, 2.0, kTol);
    CHECK_NEAR(lo, 0.0, kTol);
    CHECK(right <= t.text_width() + kTol);
}

TEST_CASE("text: a descender goes below the baseline and a capital does not") {
    Capture caps;
    Text({0, 0, 0}, "H", 1.0).draw(DrawContext{}, caps);
    Capture desc;
    Text({0, 0, 0}, "p", 1.0).draw(DrawContext{}, desc);

    double cap_lo = 1e9, desc_lo = 1e9;
    for (const auto& run : caps.runs) {
        for (const Vec3& p : run) cap_lo = std::min(cap_lo, p.y);
    }
    for (const auto& run : desc.runs) {
        for (const Vec3& p : run) desc_lo = std::min(desc_lo, p.y);
    }

    CHECK_NEAR(cap_lo, 0.0, kTol);
    CHECK(desc_lo < -0.05);
}

TEST_CASE("text: empty text draws nothing") {
    Capture cap;
    Text({0, 0, 0}, "", 2.0).draw(DrawContext{}, cap);
    CHECK(cap.runs.empty());
}

TEST_CASE("text: scaling the drawing scales the text") {
    Text t({0, 0, 0}, "ABC", 2.0);
    t.transform(Mat4::uniform_scaling(3.0));
    // SCALE on a drawing full of annotation that left the annotation alone
    // would be worse than useless.
    CHECK_NEAR(t.height(), 6.0, 1e-9);
}

TEST_CASE("text: rotating the drawing rotates the text") {
    Text t({0, 0, 0}, "ABC", 1.0);
    t.transform(Mat4::rotation({0, 0, 0}, {0, 0, 1}, kPi * 0.5));
    CHECK_NEAR(t.rotation(), kPi * 0.5, 1e-9);
    CHECK_NEAR(t.height(), 1.0, 1e-9);
}

TEST_CASE("phase 7 entities: all four survive a DXF round trip") {
    Database source;
    source.add(std::make_unique<PointEntity>(Vec3{1, 2, 3}));
    source.add(std::make_unique<Text>(Vec3{4, 5, 0}, "Round trip", 0.5));

    auto solid = std::make_unique<Face>(square_solid());
    source.add(std::move(solid));

    auto face = std::make_unique<Face>(EntityType::Face3d);
    face->set_corner(0, {0, 0, 0});
    face->set_corner(1, {1, 0, 0});
    face->set_corner(2, {0, 1, 1});
    face->set_corner(3, {1, 1, 1});
    face->set_edge_flags(2);
    source.add(std::move(face));

    std::ostringstream out;
    DxfWriter w(out, source);
    w.write_document();

    Database loaded;
    const DxfReadResult r = read_dxf_text(loaded, out.str());
    CHECK(r.ok);
    CHECK(r.proxies == 0);  // all four are real entities now
    CHECK(loaded.size() == 4);

    CHECK(loaded.get(loaded.order()[0])->type() == EntityType::Point);
    CHECK(loaded.get(loaded.order()[1])->type() == EntityType::Text);
    CHECK(loaded.get(loaded.order()[2])->type() == EntityType::Solid);
    CHECK(loaded.get(loaded.order()[3])->type() == EntityType::Face3d);

    const Text* t = static_cast<const Text*>(loaded.get(loaded.order()[1]));
    CHECK(t->value() == "Round trip");
    CHECK_NEAR(t->height(), 0.5, 1e-9);

    // Edge visibility has to survive, or a mesh comes back as a wire grid.
    const Face* f = static_cast<const Face*>(loaded.get(loaded.order()[3]));
    CHECK(f->edge_flags() == 2);
    CHECK(!f->edge_visible(1));

    const PointEntity* p = static_cast<const PointEntity*>(loaded.get(loaded.order()[0]));
    CHECK_VEC(p->position(), 1.0, 2.0, 3.0, 1e-9);
}

TEST_CASE("phase 7 entities: a SOLID keeps its shape through a tilted round trip") {
    Database source;
    auto solid = std::make_unique<Face>(square_solid());
    solid->transform(Mat4::rotation({0, 0, 0}, {1, 0, 0}, kPi / 3.0));
    const Vec3 corner1 = solid->corner(1);
    const Vec3 corner2 = solid->corner(2);
    source.add(std::move(solid));

    std::ostringstream out;
    DxfWriter w(out, source);
    w.write_document();

    Database loaded;
    read_dxf_text(loaded, out.str());
    const Face* f = static_cast<const Face*>(loaded.get(loaded.order()[0]));

    // SOLID stores its corners in the entity coordinate system, so this is the
    // arbitrary-axis algorithm surviving a full trip out and back.
    CHECK_VEC(f->corner(1), corner1.x, corner1.y, corner1.z, 1e-9);
    CHECK_VEC(f->corner(2), corner2.x, corner2.y, corner2.z, 1e-9);
}
