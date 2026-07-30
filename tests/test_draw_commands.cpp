// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// POINT, SOLID, 3DFACE and TEXT: the last of phase 7's creation commands.
//
// The entities were built earlier and are tested in test_entities7.cpp. What is
// pinned here is what the commands do that the entities cannot: the SOLID strip
// continuation, and TEXT's justification, where the answer has to reach three
// different DXF groups and the placeholder geometry at once.

#include "test.hpp"

#include "noto/command.hpp"
#include "noto/commands.hpp"
#include "noto/database.hpp"
#include "noto/dxf.hpp"
#include "noto/dxf_read.hpp"
#include "noto/entities.hpp"

#include <cmath>
#include <numbers>
#include <sstream>

using namespace noto;

namespace {

constexpr double kPi = std::numbers::pi;

const Text* last_text(const Database& db) {
    const Entity* e = db.get(db.last());
    if (!e || e->type() != EntityType::Text) return nullptr;
    return static_cast<const Text*>(e);
}

const Face* face_at(const Database& db, std::size_t i) {
    const Entity* e = db.get(db.order()[i]);
    if (!e) return nullptr;
    return static_cast<const Face*>(e);
}

}  // namespace

// --- POINT ------------------------------------------------------------------

TEST_CASE("point: one point makes one entity") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("POINT"));
    engine.supply(InputValue::of_point({3, 4, 5}));

    REQUIRE(db.size() == 1);
    const Entity* e = db.get(db.last());
    REQUIRE(e->type() == EntityType::Point);
    CHECK(near_equal(static_cast<const PointEntity*>(e)->position(), Vec3{3, 4, 5}, 1e-12));
}

// --- SOLID and 3DFACE -------------------------------------------------------

TEST_CASE("solid: four points make one quadrilateral") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("SOLID"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_point({0, 10, 0}));
    engine.supply(InputValue::of_point({10, 10, 0}));
    engine.supply(InputValue::none());  // stop the strip

    REQUIRE(db.size() == 1);
    const Face* f = face_at(db, 0);
    REQUIRE(f != nullptr);
    CHECK(f->type() == EntityType::Solid);
    CHECK(near_equal(f->corner(3), Vec3{10, 10, 0}, 1e-12));
    CHECK(!f->triangular());
}

TEST_CASE("solid: Enter at the fourth point makes a triangle") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("SOLID"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_point({0, 10, 0}));
    engine.supply(InputValue::none());  // triangle
    engine.supply(InputValue::none());  // stop

    REQUIRE(db.size() == 1);
    const Face* f = face_at(db, 0);
    REQUIRE(f != nullptr);
    // The format spells a triangle as the fourth corner repeating the third.
    CHECK(f->triangular());
    CHECK(near_equal(f->corner(3), f->corner(2), 1e-12));
}

TEST_CASE("solid: the strip continues from the last edge") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("SOLID"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_point({0, 10, 0}));
    engine.supply(InputValue::of_point({10, 10, 0}));
    // Two more points continue the strip rather than starting fresh.
    engine.supply(InputValue::of_point({0, 20, 0}));
    engine.supply(InputValue::of_point({10, 20, 0}));
    engine.supply(InputValue::none());

    REQUIRE(db.size() == 2);
    const Face* second = face_at(db, 1);
    REQUIRE(second != nullptr);
    // The previous third and fourth corners became this one's first and second,
    // which is what makes the two share an edge instead of overlapping.
    CHECK(near_equal(second->corner(0), Vec3{0, 10, 0}, 1e-12));
    CHECK(near_equal(second->corner(1), Vec3{10, 10, 0}, 1e-12));
    CHECK(near_equal(second->corner(2), Vec3{0, 20, 0}, 1e-12));
}

TEST_CASE("3dface: the same sequence, a different entity type") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("3DFACE"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_point({0, 10, 5}));
    engine.supply(InputValue::of_point({10, 10, 5}));
    engine.supply(InputValue::none());

    REQUIRE(db.size() == 1);
    CHECK(face_at(db, 0)->type() == EntityType::Face3d);
}

TEST_CASE("solid: giving up before a full quadrilateral draws nothing") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("SOLID"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    const EngineStatus status = engine.supply(InputValue::none());

    CHECK(status == EngineStatus::Failed);
    CHECK(db.size() == 0);
}

// --- TEXT -------------------------------------------------------------------

TEST_CASE("text: start point, height, rotation, string") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("TEXT"));

    engine.supply(InputValue::of_point({1, 2, 0}));
    engine.supply(InputValue::of_real(2.5));
    engine.supply(InputValue::of_real(30.0));  // degrees
    engine.supply(InputValue::of_string("HELLO"));

    REQUIRE(db.size() == 1);
    const Text* t = last_text(db);
    REQUIRE(t != nullptr);
    CHECK(t->value() == "HELLO");
    CHECK_NEAR(t->height(), 2.5, 1e-12);
    CHECK_NEAR(t->rotation(), kPi / 6.0, 1e-12);
    CHECK(!t->is_justified());
}

TEST_CASE("text: empty text draws nothing") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("TEXT"));

    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(1.0));
    engine.supply(InputValue::of_real(0.0));
    engine.supply(InputValue::none());

    CHECK(db.size() == 0);
}

TEST_CASE("text: Right justification sets group 72 and the alignment point") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("TEXT"));

    engine.supply(InputValue::of_keyword("JUSTIFY"));
    engine.supply(InputValue::of_keyword("RIGHT"));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_real(1.0));
    engine.supply(InputValue::of_real(0.0));
    engine.supply(InputValue::of_string("ABC"));

    const Text* t = last_text(db);
    REQUIRE(t != nullptr);
    CHECK(t->h_align() == TextHAlign::Right);
    CHECK(t->v_align() == TextVAlign::Baseline);
    CHECK(t->is_justified());
    CHECK(near_equal(t->align_point(), Vec3{10, 0, 0}, 1e-12));
}

TEST_CASE("text: justification moves the box, not the insertion point") {
    // The property that matters: a right-justified string ends where it was
    // placed, so its box lies entirely behind that point.
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("TEXT"));

    engine.supply(InputValue::of_keyword("JUSTIFY"));
    engine.supply(InputValue::of_keyword("RIGHT"));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_real(1.0));
    engine.supply(InputValue::of_real(0.0));
    engine.supply(InputValue::of_string("ABC"));

    const Text* t = last_text(db);
    REQUIRE(t != nullptr);
    const BBox box = t->bbox();
    CHECK(box.max.x <= 10.0 + 1e-9);
    CHECK(box.min.x < 10.0);
}

TEST_CASE("text: the middle of a Middle-justified string is where it was put") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("TEXT"));

    engine.supply(InputValue::of_keyword("JUSTIFY"));
    engine.supply(InputValue::of_keyword("MC"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(2.0));
    engine.supply(InputValue::of_real(0.0));
    engine.supply(InputValue::of_string("WXYZ"));

    const Text* t = last_text(db);
    REQUIRE(t != nullptr);
    const BBox box = t->bbox();
    CHECK_NEAR((box.min.x + box.max.x) * 0.5, 0.0, 1e-9);

    // MC centres on the UPPERCASE height, not on the whole cell -- that is what
    // separates it from Middle. So the cap top sits half a height above the
    // point and the baseline half a height below it, and the box then runs
    // lower still because it reserves the descender the font declares.
    CHECK_NEAR(box.max.y, 1.0, 1e-9);
    CHECK(box.min.y < -1.0);
}

TEST_CASE("text: the INSERT snap follows the justification point") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("TEXT"));

    engine.supply(InputValue::of_keyword("JUSTIFY"));
    engine.supply(InputValue::of_keyword("RIGHT"));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_real(1.0));
    engine.supply(InputValue::of_real(0.0));
    engine.supply(InputValue::of_string("ABC"));

    const Text* t = last_text(db);
    REQUIRE(t != nullptr);
    std::vector<OsnapPoint> snaps;
    t->osnap_points(snaps);
    REQUIRE(snaps.size() == 1);
    // Not the abandoned group 10 -- snapping to that would put the cursor
    // somewhere no part of the text is.
    CHECK(near_equal(snaps[0].pos, Vec3{10, 0, 0}, 1e-12));
}

TEST_CASE("text: Align spans the two points and derives the height") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("TEXT"));

    engine.supply(InputValue::of_keyword("JUSTIFY"));
    engine.supply(InputValue::of_keyword("ALIGN"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_string("ABCDE"));

    const Text* t = last_text(db);
    REQUIRE(t != nullptr);
    CHECK(t->h_align() == TextHAlign::Aligned);
    // The height was solved for, so the placeholder now spans the distance.
    CHECK_NEAR(t->text_width(), 10.0, 1e-9);
    CHECK_NEAR(t->width_factor(), 1.0, 1e-12);
}

TEST_CASE("text: Fit spans the two points and squeezes the width instead") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("TEXT"));

    engine.supply(InputValue::of_keyword("JUSTIFY"));
    engine.supply(InputValue::of_keyword("FIT"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_real(3.0));  // height is kept
    engine.supply(InputValue::of_string("ABCDE"));

    const Text* t = last_text(db);
    REQUIRE(t != nullptr);
    CHECK(t->h_align() == TextHAlign::Fit);
    CHECK_NEAR(t->height(), 3.0, 1e-12);
    CHECK_NEAR(t->text_width(), 10.0, 1e-9);
}

TEST_CASE("text: Align takes its rotation from the two points") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("TEXT"));

    engine.supply(InputValue::of_keyword("JUSTIFY"));
    engine.supply(InputValue::of_keyword("ALIGN"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({0, 10, 0}));  // straight up
    engine.supply(InputValue::of_string("ABC"));

    const Text* t = last_text(db);
    REQUIRE(t != nullptr);
    CHECK_NEAR(t->rotation(), kPi / 2.0, 1e-9);
}

TEST_CASE("text: justification survives a DXF round trip") {
    Database source;
    auto t = std::make_unique<Text>(Vec3{1, 2, 0}, "HELLO", 2.0);
    t->set_align(TextHAlign::Right, TextVAlign::Top);
    t->set_align_point({9, 8, 0});
    source.add(std::move(t));

    std::ostringstream out;
    DxfWriter w(out, source);
    w.write_document();

    Database loaded;
    const DxfReadResult r = read_dxf_text(loaded, out.str());
    CHECK(r.ok);
    REQUIRE(loaded.size() == 1);

    const Text* back = last_text(loaded);
    REQUIRE(back != nullptr);
    CHECK(back->h_align() == TextHAlign::Right);
    CHECK(back->v_align() == TextVAlign::Top);
    CHECK(near_equal(back->align_point(), Vec3{9, 8, 0}, 1e-9));
}

TEST_CASE("text: unjustified text writes no group 11 and reads back unchanged") {
    // The common case has to stay exactly as it was, or every existing drawing
    // gains two groups it did not have.
    Database source;
    source.add(std::make_unique<Text>(Vec3{1, 2, 0}, "PLAIN", 2.0));

    std::ostringstream out;
    DxfWriter w(out, source);
    w.write_document();

    const std::string text = out.str();
    CHECK(text.find("\n 72\n") == std::string::npos);
    CHECK(text.find("\n 73\n") == std::string::npos);

    Database loaded;
    read_dxf_text(loaded, text);
    const Text* back = last_text(loaded);
    REQUIRE(back != nullptr);
    CHECK(!back->is_justified());
    CHECK(near_equal(back->position(), Vec3{1, 2, 0}, 1e-9));
}

TEST_CASE("text: moving justified text moves both of its points together") {
    Text t(Vec3{0, 0, 0}, "ABC", 1.0);
    t.set_align(TextHAlign::Right, TextVAlign::Baseline);
    t.set_align_point({10, 0, 0});

    t.transform(Mat4::translation({0, 5, 0}));

    CHECK(near_equal(t.position(), Vec3{0, 5, 0}, 1e-12));
    CHECK(near_equal(t.align_point(), Vec3{10, 5, 0}, 1e-12));
}
