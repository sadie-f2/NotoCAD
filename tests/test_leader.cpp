// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// LEADER: an arrow, a path, and a note.
//
// Built on R13's shape rather than R12's -- the annotation is an entity the
// leader carries, not line work baked into a block -- so the things worth
// pinning are the ones that shape creates: that the note travels with the
// leader, that a clone is deep, and that what goes to DXF is still something
// every reader understands.

#include "test.hpp"

#include "ncad/command.hpp"
#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/dxf.hpp"
#include "ncad/entities.hpp"

#include <cmath>
#include <memory>
#include <numbers>
#include <sstream>
#include <string>

using namespace ncad;

namespace {

constexpr double kTol = 1e-9;

std::size_t count_of(const std::vector<EntityPtr>& parts, EntityType t) {
    std::size_t n = 0;
    for (const EntityPtr& e : parts) {
        if (e && e->type() == t) ++n;
    }
    return n;
}

const Leader* first_leader(const Database& db) {
    for (const Handle h : db.order()) {
        const Entity* e = db.get(h);
        if (e && e->type() == EntityType::Leader) return static_cast<const Leader*>(e);
    }
    return nullptr;
}

// A leader with a note, built the way the command builds one.
std::unique_ptr<Leader> leader_with(std::vector<Vec3> pts, const std::string& note) {
    auto l = std::make_unique<Leader>();
    l->apply_style(4.0, 3.0);
    l->set_vertices(std::move(pts));
    if (!note.empty()) {
        auto t = std::make_unique<Text>(l->annotation_origin(), note, l->text_height());
        t->set_align(l->annotation_on_left() ? TextHAlign::Right : TextHAlign::Left,
                     TextVAlign::Baseline);
        t->set_align_point(l->annotation_origin());
        l->set_annotation(std::move(t));
    }
    return l;
}

std::string dxf_of(const Database& db, DxfVersion v) {
    std::ostringstream out;
    DxfWriter w(out, db, v);
    w.write_document();
    return out.str();
}

}  // namespace

TEST_CASE("leader: the line work is an arrow, the segments, and a hook") {
    auto l = leader_with({{0, 0, 0}, {20, 20, 0}, {40, 20, 0}}, "");
    std::vector<EntityPtr> parts;
    l->regenerate(parts);

    // One SOLID arrowhead, at the tip.
    CHECK(count_of(parts, EntityType::Solid) == 1);
    // Two picked segments; the last is already horizontal, so no hook.
    CHECK(count_of(parts, EntityType::Line) == 2);
    CHECK(!l->has_hook());
}

TEST_CASE("leader: a sloped last segment gets the shoulder R12 adds for you") {
    // R12 appends the horizontal landing itself rather than making you draw it,
    // which is why there is no prompt for it.
    auto sloped = leader_with({{0, 0, 0}, {20, 20, 0}}, "");
    CHECK(sloped->has_hook());

    std::vector<EntityPtr> parts;
    sloped->regenerate(parts);
    CHECK(count_of(parts, EntityType::Line) == 2);  // the segment and the hook

    // The hook is horizontal in the entity's plane and starts where the path
    // ended.
    CHECK_VEC(sloped->hook_start(), 20.0, 20.0, 0.0, kTol);
    CHECK(std::abs(sloped->hook_end().y - 20.0) < kTol);
    CHECK(sloped->hook_end().x > 20.0);
}

TEST_CASE("leader: the shoulder continues the way the path was going") {
    // A leader coming in from the left lands pointing right, and its note sits
    // to the right of the landing. Coming from the right, everything mirrors.
    auto rightward = leader_with({{0, 0, 0}, {20, 20, 0}}, "");
    CHECK(!rightward->annotation_on_left());
    CHECK(rightward->hook_end().x > rightward->hook_start().x);

    auto leftward = leader_with({{100, 0, 0}, {80, 20, 0}}, "");
    CHECK(leftward->annotation_on_left());
    CHECK(leftward->hook_end().x < leftward->hook_start().x);
}

TEST_CASE("leader: a path of fewer than two points draws nothing") {
    Leader bare;
    bare.set_vertices({{5, 5, 0}});
    std::vector<EntityPtr> parts;
    bare.regenerate(parts);
    CHECK(parts.empty());
    CHECK(!bare.has_hook());
}

TEST_CASE("leader: the note is an entity it carries, not generated line work") {
    // The whole of why this is R13's shape and not a DimKind. regenerate()
    // emits the leader's OWN geometry; the annotation draws itself.
    auto l = leader_with({{0, 0, 0}, {20, 20, 0}}, "NOTE");
    REQUIRE(l->annotation() != nullptr);
    CHECK(l->annotation()->type() == EntityType::Text);
    CHECK(static_cast<const Text*>(l->annotation())->value() == "NOTE");

    std::vector<EntityPtr> parts;
    l->regenerate(parts);
    CHECK(count_of(parts, EntityType::Text) == 0);
}

TEST_CASE("leader: the note sits clear of the landing, on the side it reads from") {
    auto right = leader_with({{0, 0, 0}, {20, 20, 0}}, "N");
    // Past the hook end, so the text does not touch the line it ends.
    CHECK(right->annotation_origin().x > right->hook_end().x);
    // And lifted, so it rests on the landing rather than being cut by it.
    CHECK(right->annotation_origin().y > right->hook_end().y);

    auto left = leader_with({{100, 0, 0}, {80, 20, 0}}, "N");
    CHECK(left->annotation_origin().x < left->hook_end().x);
}

TEST_CASE("leader: a clone is deep, so COPY does not make two leaders share a note") {
    auto l = leader_with({{0, 0, 0}, {20, 20, 0}}, "ORIGINAL");
    const EntityPtr copy = l->clone();
    const Leader* c = static_cast<const Leader*>(copy.get());

    REQUIRE(c->annotation() != nullptr);
    // Different object, same content. A shared note would make editing one
    // edit the other, which is the failure ownership has to avoid.
    CHECK(c->annotation() != l->annotation());
    CHECK(static_cast<const Text*>(c->annotation())->value() == "ORIGINAL");
    CHECK(c->vertices().size() == l->vertices().size());
    CHECK(std::abs(c->text_height() - l->text_height()) < kTol);
}

TEST_CASE("leader: the note travels with the leader under transform") {
    auto l = leader_with({{0, 0, 0}, {20, 20, 0}}, "N");
    const Vec3 before = static_cast<const Text*>(l->annotation())->position();

    l->transform(Mat4::translation({10, 5, 0}));

    CHECK_VEC(l->vertices()[0], 10.0, 5.0, 0.0, kTol);
    const Vec3 after = static_cast<const Text*>(l->annotation())->position();
    CHECK_VEC(after, before.x + 10.0, before.y + 5.0, before.z, kTol);
}

TEST_CASE("leader: scaling takes the arrow and the text with it") {
    // Annotation left at its old size on a scaled drawing is worse than
    // useless -- the same rule dimensions follow.
    auto l = leader_with({{0, 0, 0}, {20, 20, 0}}, "N");
    const double arrow = l->arrow_size();
    const double height = l->text_height();

    l->transform(Mat4::uniform_scaling(2.0));

    CHECK(std::abs(l->arrow_size() - arrow * 2.0) < kTol);
    CHECK(std::abs(l->text_height() - height * 2.0) < kTol);
}

TEST_CASE("leader: the bounding box covers the note as well as the path") {
    // ZOOM Extents that cut the text off would be wrong about the drawing.
    auto with = leader_with({{0, 0, 0}, {20, 20, 0}}, "A LONG NOTE INDEED");
    auto without = leader_with({{0, 0, 0}, {20, 20, 0}}, "");
    CHECK(with->bbox().max.x > without->bbox().max.x);
}

TEST_CASE("leader: naming every stretch grip equals translating it") {
    // The property SF_todo names as load-bearing for the whole grip vtable:
    // it is what makes STRETCH degenerate into MOVE rather than into nonsense.
    auto stretched = leader_with({{0, 0, 0}, {20, 20, 0}, {40, 30, 0}}, "N");
    auto moved = leader_with({{0, 0, 0}, {20, 20, 0}, {40, 30, 0}}, "N");

    std::vector<Grip> grips;
    stretched->grips(grips);
    REQUIRE(grips.size() == 3);

    std::vector<GripIndex> all;
    for (const Grip& g : grips) all.push_back(g.index);

    const Vec3 delta{7, -3, 2};
    stretched->stretch(delta, all.data(), all.size());
    moved->transform(Mat4::translation(delta));

    for (std::size_t i = 0; i < 3; ++i) {
        CHECK_VEC(stretched->vertices()[i], moved->vertices()[i].x, moved->vertices()[i].y,
                  moved->vertices()[i].z, kTol);
    }
    const Vec3 a = static_cast<const Text*>(stretched->annotation())->position();
    const Vec3 b = static_cast<const Text*>(moved->annotation())->position();
    CHECK_VEC(a, b.x, b.y, b.z, kTol);
}

TEST_CASE("leader: dragging the far end takes the note with it, the tip does not") {
    auto l = leader_with({{0, 0, 0}, {20, 20, 0}}, "N");
    const Vec3 note_before = static_cast<const Text*>(l->annotation())->position();

    // The tip is what the note is ABOUT; moving it re-points the arrow and
    // leaves the note where it was placed.
    const GripIndex tip = 0;
    l->stretch({0, -5, 0}, &tip, 1);
    CHECK_VEC(static_cast<const Text*>(l->annotation())->position(), note_before.x,
              note_before.y, note_before.z, kTol);

    // The far end is how a leader is repositioned, and the note rides it.
    const GripIndex last = 1;
    l->stretch({3, 4, 0}, &last, 1);
    CHECK_VEC(static_cast<const Text*>(l->annotation())->position(), note_before.x + 3.0,
              note_before.y + 4.0, note_before.z, kTol);
}

TEST_CASE("leader: an out-of-range grip index is ignored rather than raising") {
    auto l = leader_with({{0, 0, 0}, {20, 20, 0}}, "");
    const GripIndex bogus = 99;
    l->stretch({1, 1, 1}, &bogus, 1);
    CHECK_VEC(l->vertices()[0], 0.0, 0.0, 0.0, kTol);
}

TEST_CASE("leader: the arrow tip offers an endpoint snap") {
    auto l = leader_with({{0, 0, 0}, {20, 20, 0}}, "");
    std::vector<OsnapPoint> snaps;
    l->osnap_points(snaps);
    REQUIRE(!snaps.empty());
    CHECK_VEC(snaps[0].pos, 0.0, 0.0, 0.0, kTol);
    CHECK(snaps[0].type == OsnapType::Endpoint);
}

// --- DXF ----------------------------------------------------------------------

TEST_CASE("leader: DXF degrades to line work, at both versions") {
    // Stated rather than discovered. R13 has a LEADER entity, but writing one
    // means a hard pointer to an annotation record -- and our reader does not
    // know LEADER, so writing it would open a round trip we cannot close.
    Database db;
    db.add(leader_with({{0, 0, 0}, {20, 20, 0}}, "NOTE"));

    for (const DxfVersion v : {DxfVersion::R12, DxfVersion::R2000}) {
        const std::string out = dxf_of(db, v);
        CHECK(out.find("\nLEADER\r\n") == std::string::npos);
        // What every reader understands instead: the segments, the arrowhead
        // and the note.
        CHECK(out.find("\nLINE\r\n") != std::string::npos);
        CHECK(out.find("\nSOLID\r\n") != std::string::npos);
        CHECK(out.find("NOTE") != std::string::npos);
    }
}

TEST_CASE("leader: a note-less leader still writes its line work") {
    Database db;
    db.add(leader_with({{0, 0, 0}, {20, 20, 0}}, ""));
    const std::string out = dxf_of(db, DxfVersion::R12);
    CHECK(out.find("\nLINE\r\n") != std::string::npos);
    CHECK(out.find("\nSOLID\r\n") != std::string::npos);
}

// --- the command --------------------------------------------------------------

TEST_CASE("leader command: the tip first, then the path, then the note") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("LEADER"));
    engine.supply(InputValue::of_point({0, 0, 0}));    // Leader start -- the ARROW
    engine.supply(InputValue::of_point({20, 20, 0}));  // To point
    engine.supply(InputValue::of_point({40, 20, 0}));  // To point
    engine.supply(InputValue::none());                 // Enter ends the path
    engine.supply(InputValue::of_string("SEE DETAIL"));
    REQUIRE(engine.status() == EngineStatus::Finished);

    const Leader* l = first_leader(db);
    REQUIRE(l != nullptr);
    REQUIRE(l->vertices().size() == 3);
    CHECK_VEC(l->vertices()[0], 0.0, 0.0, 0.0, kTol);
    REQUIRE(l->annotation() != nullptr);
    CHECK(static_cast<const Text*>(l->annotation())->value() == "SEE DETAIL");
}

TEST_CASE("leader command: one point is not a leader") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LEADER"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::none());
    CHECK(first_leader(db) == nullptr);
}

TEST_CASE("leader command: Enter at the note leaves a leader that only points") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LEADER"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({20, 20, 0}));
    engine.supply(InputValue::none());
    engine.supply(InputValue::none());
    REQUIRE(engine.status() == EngineStatus::Finished);

    const Leader* l = first_leader(db);
    REQUIRE(l != nullptr);
    CHECK(l->annotation() == nullptr);
}

TEST_CASE("leader command: the default note is the last dimension's measurement") {
    // The reason R12 kept LEader inside DIM at all: you dimension something,
    // then draw a leader, and it offers that number back.
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("DIMLINEAR"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({50, 0, 0}));
    engine.supply(InputValue::of_point({25, -20, 0}));
    REQUIRE(engine.status() == EngineStatus::Finished);

    engine.begin(make_command("LEADER"));
    engine.supply(InputValue::of_point({60, 0, 0}));
    engine.supply(InputValue::of_point({80, 15, 0}));
    engine.supply(InputValue::none());
    engine.supply(InputValue::none());  // Enter accepts the offered default
    REQUIRE(engine.status() == EngineStatus::Finished);

    const Leader* l = first_leader(db);
    REQUIRE(l != nullptr);
    REQUIRE(l->annotation() != nullptr);
    CHECK(static_cast<const Text*>(l->annotation())->value() == "50.0000");
}

TEST_CASE("leader command: with nothing measured yet, Enter means no note") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LEADER"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({20, 20, 0}));
    engine.supply(InputValue::none());
    engine.supply(InputValue::none());
    REQUIRE(first_leader(db) != nullptr);
    CHECK(first_leader(db)->annotation() == nullptr);
}

TEST_CASE("leader command: DIM's LEader builds the same command, not a second one") {
    // DIM owns no geometry code -- each subcommand forwards to the real
    // command, so the mode and the command cannot drift apart.
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("DIM"));
    engine.supply(InputValue::of_keyword("LEADER"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({20, 20, 0}));
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_string("FROM DIM"));

    const Leader* l = first_leader(db);
    REQUIRE(l != nullptr);
    REQUIRE(l->annotation() != nullptr);
    CHECK(static_cast<const Text*>(l->annotation())->value() == "FROM DIM");
}

TEST_CASE("leader command: the style is baked in from the DIM variables") {
    // draw() is handed no database and could not read them later, which is the
    // same reason a dimension bakes its own.
    Database db;
    db.sysvars().set_real(Sysvar::DimScale, 2.0);
    db.sysvars().set_real(Sysvar::DimTxt, 3.0);
    db.sysvars().set_real(Sysvar::DimAsz, 1.5);

    CommandEngine engine(db);
    engine.begin(make_command("LEADER"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({20, 20, 0}));
    engine.supply(InputValue::none());
    engine.supply(InputValue::none());

    const Leader* l = first_leader(db);
    REQUIRE(l != nullptr);
    CHECK(std::abs(l->text_height() - 6.0) < kTol);
    CHECK(std::abs(l->arrow_size() - 3.0) < kTol);
}

TEST_CASE("leader: EXPLODE gives the line work and hands over the note") {
    // The one place R13's split shows through: the annotation is already an
    // entity, so exploding hands it over rather than regenerating it.
    Database db;
    db.add(leader_with({{0, 0, 0}, {20, 20, 0}}, "NOTE"));

    CommandEngine engine(db);
    engine.begin(make_command("EXPLODE"));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());

    CHECK(first_leader(db) == nullptr);

    std::size_t lines = 0, solids = 0, texts = 0;
    for (const Handle h : db.order()) {
        const Entity* e = db.get(h);
        if (!e) continue;
        if (e->type() == EntityType::Line) ++lines;
        if (e->type() == EntityType::Solid) ++solids;
        if (e->type() == EntityType::Text) ++texts;
    }
    CHECK(lines == 2);  // the segment and the hook
    CHECK(solids == 1);
    CHECK(texts == 1);
}
