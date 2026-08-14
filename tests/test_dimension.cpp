// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Dimensions: what they measure, what they draw, and what survives DXF.
//
// The measurement is the thing worth pinning. A dimension that draws beautiful
// line work and states the wrong number is worse than one that draws nothing,
// because the drawing is then a lie a machinist acts on.

#include "test.hpp"

#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/dxf.hpp"
#include "ncad/dxf_read.hpp"
#include "ncad/entities.hpp"

#include <cmath>
#include <memory>
#include <numbers>

using namespace ncad;

namespace {

std::size_t count_of(const std::vector<EntityPtr>& parts, EntityType t) {
    std::size_t n = 0;
    for (const EntityPtr& e : parts) {
        if (e && e->type() == t) ++n;
    }
    return n;
}

const Dimension* first_dimension(const Database& db) {
    for (const Handle h : db.order()) {
        const Entity* e = db.get(h);
        if (e && e->type() == EntityType::Dimension) return static_cast<const Dimension*>(e);
    }
    return nullptr;
}

}  // namespace

TEST_CASE("dimension: a rotated one measures along its own axis, not between the points") {
    // The whole difference between Linear and Aligned, and what makes R12's
    // HORizontal and VERtical the same code with a different angle.
    Dimension h;
    h.set_kind(DimKind::Linear);
    h.set_points(Vec3{0, 0, 0}, Vec3{100, 25, 0});
    h.set_definition(Vec3{50, -20, 0});
    CHECK(std::abs(h.measurement() - 100.0) < 1e-12);

    Dimension v = h;
    v.set_rotation(std::numbers::pi / 2.0);
    CHECK(std::abs(v.measurement() - 25.0) < 1e-12);

    Dimension a = h;
    a.set_kind(DimKind::Aligned);
    CHECK(std::abs(a.measurement() - std::sqrt(100.0 * 100.0 + 25.0 * 25.0)) < 1e-12);
}

TEST_CASE("dimension: radius and diameter differ by exactly two") {
    Dimension r;
    r.set_kind(DimKind::Radius);
    r.set_definition(Vec3{10, 10, 0});
    r.set_points(Vec3{10, 35, 0}, Vec3{});
    CHECK(std::abs(r.measurement() - 25.0) < 1e-12);
    CHECK(r.label() == "R25.0000");

    Dimension d = r;
    d.set_kind(DimKind::Diameter);
    CHECK(std::abs(d.measurement() - 50.0) < 1e-12);
    // R12's escape for the diameter sign, which is what AutoCAD wants to see.
    CHECK(d.label() == "%%C50.0000");
}

TEST_CASE("dimension: an override replaces the label and not the measurement") {
    Dimension d;
    d.set_kind(DimKind::Aligned);
    d.set_points(Vec3{0, 0, 0}, Vec3{3, 4, 0});
    d.set_definition(Vec3{0, -5, 0});
    d.set_text_override("TYP");

    CHECK(d.label() == "TYP");
    // What it says and what it measures are different questions, and a drawing
    // that forgot the second could not be checked.
    CHECK(std::abs(d.measurement() - 5.0) < 1e-12);
}

TEST_CASE("dimension: it draws extension lines, arrowheads and one label") {
    Dimension d;
    d.set_kind(DimKind::Linear);
    d.set_points(Vec3{0, 0, 0}, Vec3{100, 0, 0});
    d.set_definition(Vec3{50, -20, 0});

    std::vector<EntityPtr> parts;
    d.regenerate(parts);

    // Two extension lines and a dimension line broken either side of the text.
    CHECK(count_of(parts, EntityType::Line) == 4);
    // Arrowheads are SOLIDs, which is what R12 puts in a dimension block and
    // what makes another program draw them filled.
    CHECK(count_of(parts, EntityType::Solid) == 2);
    CHECK(count_of(parts, EntityType::Text) == 1);
}

TEST_CASE("dimension: text too wide for the span leaves the line unbroken") {
    // Breaking a two-unit dimension line around a label wider than itself gives
    // two segments running backwards. The line stays whole instead.
    Dimension d;
    d.set_kind(DimKind::Linear);
    d.set_points(Vec3{0, 0, 0}, Vec3{2, 0, 0});
    d.set_definition(Vec3{1, -5, 0});

    std::vector<EntityPtr> parts;
    d.regenerate(parts);
    CHECK(count_of(parts, EntityType::Line) == 3);  // two extension, one whole
}

TEST_CASE("dimension: moving it does not change what it reads") {
    Dimension d;
    d.set_kind(DimKind::Aligned);
    d.set_points(Vec3{0, 0, 0}, Vec3{30, 40, 0});
    d.set_definition(Vec3{-10, 10, 0});
    const double before = d.measurement();

    d.transform(Mat4::translation(Vec3{1000, -500, 7}));
    CHECK(std::abs(d.measurement() - before) < 1e-9);

    // Scaling DOES change it, and takes the annotation with it -- a drawing
    // scaled up whose text stayed put would be unreadable.
    const double height = d.text_height();
    d.transform(Mat4::uniform_scaling(2.0));
    CHECK(std::abs(d.measurement() - before * 2.0) < 1e-9);
    CHECK(std::abs(d.text_height() - height * 2.0) < 1e-9);
}

TEST_CASE("dimension: the bounding box covers the label, not just the points") {
    // ZOOM Extents that cut the text off would be wrong in the direction people
    // notice, so the box comes from the drawn form.
    Dimension d;
    d.set_kind(DimKind::Linear);
    d.set_points(Vec3{0, 0, 0}, Vec3{100, 0, 0});
    d.set_definition(Vec3{50, -20, 0});

    const BBox box = d.bbox();
    REQUIRE(box.valid());
    CHECK(box.min.y < -20.0);  // reaches the dimension line and its text
}

// --- the commands -------------------------------------------------------------

TEST_CASE("dimension command: DIMLINEAR reads horizontal or vertical off the placement") {
    Database db;
    CommandEngine engine(db);

    // Below the pair: the horizontal distance was meant.
    engine.begin(make_command("DIMLINEAR"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({100, 25, 0}));
    engine.supply(InputValue::of_point({50, -30, 0}));
    REQUIRE(engine.status() == EngineStatus::Finished);
    REQUIRE(first_dimension(db) != nullptr);
    CHECK(std::abs(first_dimension(db)->measurement() - 100.0) < 1e-9);

    // Beside it: the vertical one.
    Database db2;
    CommandEngine e2(db2);
    e2.begin(make_command("DIMLINEAR"));
    e2.supply(InputValue::of_point({0, 0, 0}));
    e2.supply(InputValue::of_point({100, 25, 0}));
    e2.supply(InputValue::of_point({-40, 12, 0}));
    REQUIRE(first_dimension(db2) != nullptr);
    CHECK(std::abs(first_dimension(db2)->measurement() - 25.0) < 1e-9);
}

TEST_CASE("dimension command: placing a horizontal one off to the side still reads horizontal") {
    // The bug the placement rule was written twice for. Distance from the
    // MIDPOINT counts displacement along the span, so a dimension line dropped
    // below the left-hand point looked like a sideways drag and measured the
    // vertical distance -- which for two points at the same height is zero.
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("DIMLINEAR"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({100, 0, 0}));
    engine.supply(InputValue::of_point({0, -20, 0}));  // below the LEFT end
    CHECK(engine.status() == EngineStatus::Finished);
    REQUIRE(first_dimension(db) != nullptr);
    CHECK(std::abs(first_dimension(db)->measurement() - 100.0) < 1e-9);
}

TEST_CASE("dimension command: DIMRADIUS takes its direction from where you point") {
    Database db;
    CommandEngine engine(db);
    const Handle c = db.add(std::make_unique<Circle>(Vec3{0, 0, 0}, 25.0));

    engine.begin(make_command("DIMRADIUS"));
    engine.supply(InputValue::of_entity(c));
    engine.supply(InputValue::of_point({100, 0, 0}));  // any distance along +X
    REQUIRE(engine.status() == EngineStatus::Finished);

    const Dimension* d = first_dimension(db);
    REQUIRE(d != nullptr);
    CHECK(d->kind() == DimKind::Radius);
    // The leader stops ON the curve however far away the pick was.
    CHECK(std::abs(d->measurement() - 25.0) < 1e-9);
    CHECK_VEC(d->first(), 25.0, 0.0, 0.0, 1e-9);

    engine.begin(make_command("DIMDIAMETER"));
    engine.supply(InputValue::of_entity(c));
    engine.supply(InputValue::of_point({0, 5, 0}));
    CHECK(engine.status() == EngineStatus::Finished);
}

TEST_CASE("dimension command: DIMRADIUS refuses what has no radius") {
    Database db;
    CommandEngine engine(db);
    const Handle l = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));

    engine.begin(make_command("DIMRADIUS"));
    engine.supply(InputValue::of_entity(l));
    CHECK(engine.status() == EngineStatus::Failed);
    CHECK(db.size() == 1);
}

TEST_CASE("dim mode: it dispatches to the real commands and leaves on Enter or eXit") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("DIM"));
    // HORizontal forces the direction rather than inferring it, which is the
    // one thing the mode adds over DIMLINEAR.
    engine.supply(InputValue::of_keyword("HORIZONTAL"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({80, 30, 0}));
    engine.supply(InputValue::of_point({40, -15, 0}));
    CHECK(engine.active());  // the mode is still up

    engine.supply(InputValue::of_keyword("VERTICAL"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({0, 40, 0}));
    engine.supply(InputValue::of_point({-15, 20, 0}));
    CHECK(db.size() == 2);

    // Undo takes back the last one without touching the first.
    engine.supply(InputValue::of_keyword("UNDO"));
    CHECK(db.size() == 1);
    CHECK(std::abs(first_dimension(db)->measurement() - 80.0) < 1e-9);

    engine.supply(InputValue::none());  // bare Enter leaves, as eXit does
    CHECK(engine.status() == EngineStatus::Finished);
}

TEST_CASE("dim mode: DIM1 leaves after one, and the whole session is one undo step") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("DIM1"));
    engine.supply(InputValue::of_keyword("ALIGNED"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({3, 4, 0}));
    engine.supply(InputValue::of_point({0, -5, 0}));
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(db.size() == 1);

    engine.begin(make_command("UNDO"));
    CHECK(db.empty());
}

// --- DXF ------------------------------------------------------------------------

TEST_CASE("dimension: DXF round trip keeps the kind, the points and the measurement") {
    for (const DxfVersion version : {DxfVersion::R12, DxfVersion::R2000}) {
        Database db;
        auto d = std::make_unique<Dimension>();
        d->set_kind(DimKind::Linear);
        d->set_points(Vec3{0, 0, 0}, Vec3{100, 25, 0});
        d->set_definition(Vec3{50, -20, 0});
        d->set_rotation(std::numbers::pi / 2.0);
        db.add(std::move(d));

        const std::string text = write_dxf_text(db, version);

        Database back;
        REQUIRE(read_dxf_text(back, text).ok);
        const Dimension* got = first_dimension(back);
        REQUIRE(got != nullptr);
        CHECK(got->kind() == DimKind::Linear);
        CHECK_VEC(got->first(), 0.0, 0.0, 0.0, 1e-9);
        CHECK_VEC(got->second(), 100.0, 25.0, 0.0, 1e-9);
        CHECK(std::abs(got->measurement() - 25.0) < 1e-6);
    }
}

TEST_CASE("dimension: the drawn geometry goes out as an anonymous block") {
    // R12's arrangement, and the reason a reader that does not regenerate
    // dimensions still draws the right thing.
    Database db;
    auto d = std::make_unique<Dimension>();
    d->set_kind(DimKind::Linear);
    d->set_points(Vec3{0, 0, 0}, Vec3{100, 0, 0});
    d->set_definition(Vec3{50, -20, 0});
    db.add(std::move(d));

    const std::string text = write_dxf_text(db, DxfVersion::R12);
    CHECK(text.find("\r\n2\r\n*D1\r\n") != std::string::npos);
    CHECK(text.find("\r\nDIMENSION\r\n") != std::string::npos);

    // The block holds the line work, so the file carries both what is measured
    // and how it looked.
    const std::size_t block = text.find("*D1");
    REQUIRE(block != std::string::npos);
    CHECK(text.find("SOLID", block) != std::string::npos);

    // And reading it back does not leave the block behind as a stray
    // definition: the dimension regenerates, so the drawing holds one thing.
    Database back;
    REQUIRE(read_dxf_text(back, text).ok);
    CHECK(back.size() == 1);
    CHECK(first_dimension(back) != nullptr);
}

TEST_CASE("dimension: the style survives the header round trip") {
    Database db;
    // A drawing annotated for a plot scale, which is what DIMSCALE is for.
    db.sysvars().set_real(Sysvar::DimScale, 2.0);
    db.sysvars().set_real(Sysvar::DimTxt, 3.0);

    auto d = std::make_unique<Dimension>();
    d->set_kind(DimKind::Aligned);
    d->set_points(Vec3{0, 0, 0}, Vec3{100, 0, 0});
    d->set_definition(Vec3{50, -20, 0});
    d->apply_style(6.0, 5.0, 1.25, 2.5);
    db.add(std::move(d));

    Database back;
    REQUIRE(read_dxf_text(back, write_dxf_text(db, DxfVersion::R2000)).ok);

    CHECK(std::abs(back.sysvars().get_real(Sysvar::DimScale) - 2.0) < 1e-9);
    CHECK(std::abs(back.sysvars().get_real(Sysvar::DimTxt) - 3.0) < 1e-9);
    // The entity carries only what it measures, so its sizes come back from the
    // header -- DIMTXT times DIMSCALE, which is what it was drawn at.
    REQUIRE(first_dimension(back) != nullptr);
    CHECK(std::abs(first_dimension(back)->text_height() - 6.0) < 1e-9);
}

TEST_CASE("dimension: EXPLODE turns it into the line work it draws") {
    Database db;
    CommandEngine engine(db);
    auto d = std::make_unique<Dimension>();
    d->set_kind(DimKind::Linear);
    d->set_points(Vec3{0, 0, 0}, Vec3{100, 0, 0});
    d->set_definition(Vec3{50, -20, 0});
    const Handle h = db.add(std::move(d));

    engine.begin(make_command("EXPLODE"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());
    CHECK(engine.status() == EngineStatus::Finished);

    CHECK(db.get(h) == nullptr);       // the dimension is gone
    CHECK(first_dimension(db) == nullptr);
    CHECK(db.size() == 7);             // 4 lines, 2 arrowheads, 1 text
}
