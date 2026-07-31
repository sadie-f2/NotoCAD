// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Blocks: the definition table, the INSERT entity, and the commands.
//
// The properties that carry the weight:
//
//   A definition is not in the drawing. Its entities have no handles, are not
//   in the drawing order, and are not written to ENTITIES. A block whose
//   contents leak into the drawing is the failure that makes every count wrong.
//
//   Redefinition updates every insertion. That is R12's behaviour and it falls
//   out of inserts holding the definition's address rather than a copy -- which
//   is a design decision worth a test, because a refactor to store copies would
//   pass every other test here.
//
//   Placement round-trips through R12's four fields. The entity holds a matrix
//   and DXF holds a point, three scales, an angle and an extrusion; anything
//   R12 can express has to survive the conversion exactly.

#include "test.hpp"

#include "ncad/command.hpp"
#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/dxf.hpp"
#include "ncad/dxf_read.hpp"
#include "ncad/entities.hpp"
#include "ncad/intersect.hpp"
#include "ncad/osnap_derived.hpp"

#include <cmath>
#include <memory>
#include <numbers>
#include <sstream>

using namespace ncad;

namespace {

constexpr double kPi = std::numbers::pi;

// A unit square block, defined around its own origin.
BlockDef square_def(const char* name = "SQ") {
    BlockDef def;
    def.name = name;
    def.entities.push_back(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));
    def.entities.push_back(std::make_unique<Line>(Vec3{1, 0, 0}, Vec3{1, 1, 0}));
    def.entities.push_back(std::make_unique<Line>(Vec3{1, 1, 0}, Vec3{0, 1, 0}));
    def.entities.push_back(std::make_unique<Line>(Vec3{0, 1, 0}, Vec3{0, 0, 0}));
    return def;
}

const Insert* last_insert(const Database& db) {
    const Entity* e = db.get(db.last());
    if (!e || e->type() != EntityType::Insert) return nullptr;
    return static_cast<const Insert*>(e);
}

bool has_snap(const std::vector<OsnapPoint>& pts, OsnapType t, const Vec3& at) {
    for (const OsnapPoint& p : pts) {
        if (p.type == t && near_equal(p.pos, at, 1e-7)) return true;
    }
    return false;
}

}  // namespace

// --- the table --------------------------------------------------------------

TEST_CASE("blocks: a definition is not part of the drawing") {
    Database db;
    db.add_block(square_def());

    CHECK(db.block_count() == 1);
    // Four lines in the definition, nothing in the drawing.
    CHECK(db.size() == 0);
    CHECK(db.order().empty());
}

TEST_CASE("blocks: a definition is found by name and keeps its address") {
    Database db;
    const BlockId a = db.add_block(square_def("A"));
    const BlockDef* first = db.block(a);
    REQUIRE(first != nullptr);

    // Adding more must not move what an Insert is pointing at.
    for (int i = 0; i < 32; ++i) {
        db.add_block(square_def(("B" + std::to_string(i)).c_str()));
    }
    CHECK(db.block(a) == first);
    CHECK(db.find_block("A") == a);
    CHECK(db.find_block("NOPE") == kInvalidBlock);
}

TEST_CASE("blocks: redefining a name updates every insertion of it") {
    Database db;
    const BlockId id = db.add_block(square_def());
    const BlockDef* def = db.block(id);

    db.add(std::make_unique<Insert>(def, Mat4::identity()));
    db.add(std::make_unique<Insert>(def, Mat4::translation({10, 0, 0})));

    // The square is 1x1, so two insertions ten apart span 11 in x.
    CHECK_NEAR(db.extents().max.x, 11.0, 1e-9);

    // Redefine to a bigger square. Both insertions must follow.
    BlockDef bigger;
    bigger.name = "SQ";
    bigger.entities.push_back(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{5, 0, 0}));
    db.add_block(std::move(bigger));

    CHECK(db.block_count() == 1);
    CHECK_NEAR(db.extents().max.x, 15.0, 1e-9);
}

TEST_CASE("blocks: an insertion counts as a reference") {
    Database db;
    const BlockId id = db.add_block(square_def());
    CHECK(!db.block_is_referenced(id));

    db.add(std::make_unique<Insert>(db.block(id), Mat4::identity()));
    CHECK(db.block_is_referenced(id));
}

// --- the entity -------------------------------------------------------------

TEST_CASE("insert: bounds come from the definition, placed") {
    Database db;
    const BlockId id = db.add_block(square_def());
    Insert ins(db.block(id), Mat4::translation({10, 20, 0}));

    const BBox box = ins.bbox();
    REQUIRE(box.valid());
    CHECK(near_equal(box.min, Vec3{10, 20, 0}, 1e-9));
    CHECK(near_equal(box.max, Vec3{11, 21, 0}, 1e-9));
}

TEST_CASE("insert: transform composes rather than needing to be representable") {
    Database db;
    const BlockId id = db.add_block(square_def());
    Insert ins(db.block(id), Mat4::identity());

    // A rotation about an arbitrary axis, which R12's single rotation angle
    // cannot express on its own. The entity holds a matrix, so it just works.
    ins.transform(Mat4::rotation({0, 0, 0}, normalize(Vec3{1, 1, 1}), kPi / 3.0));
    ins.transform(Mat4::translation({5, 0, 0}));

    const BBox box = ins.bbox();
    CHECK(box.valid());
    CHECK(near_equal(ins.insertion_point(), Vec3{5, 0, 0}, 1e-9));
}

TEST_CASE("insert: the insertion point is where the base point landed") {
    Database db;
    BlockDef def = square_def();
    def.base = {0.5, 0.5, 0.0};  // the square's centre
    const BlockId id = db.add_block(std::move(def));

    InsertPlacement p;
    p.insertion = {10, 10, 0};
    Insert ins(db.block(id), compose_placement(p, db.block(id)->base));

    CHECK(near_equal(ins.insertion_point(), Vec3{10, 10, 0}, 1e-9));
    // Placed by its centre, so the square straddles the point.
    const BBox box = ins.bbox();
    CHECK(near_equal(box.min, Vec3{9.5, 9.5, 0}, 1e-9));
}

TEST_CASE("insert: MINSERT arrays without becoming separate entities") {
    Database db;
    const BlockId id = db.add_block(square_def());
    Insert ins(db.block(id), Mat4::identity());
    ins.set_array(2, 3, 10.0, 20.0);

    CHECK(ins.is_array());
    const BBox box = ins.bbox();
    // Three columns 20 apart: 0, 20, 40, plus the square's own width.
    CHECK_NEAR(box.max.x, 41.0, 1e-9);
    // Two rows 10 apart.
    CHECK_NEAR(box.max.y, 11.0, 1e-9);
}

TEST_CASE("insert: nesting is followed") {
    Database db;
    const BlockId inner = db.add_block(square_def("INNER"));

    BlockDef outer;
    outer.name = "OUTER";
    outer.entities.push_back(
        std::make_unique<Insert>(db.block(inner), Mat4::translation({100, 0, 0})));
    const BlockId outer_id = db.add_block(std::move(outer));

    Insert ins(db.block(outer_id), Mat4::identity());
    const BBox box = ins.bbox();
    REQUIRE(box.valid());
    CHECK_NEAR(box.min.x, 100.0, 1e-9);
    CHECK_NEAR(box.max.x, 101.0, 1e-9);
}

TEST_CASE("insert: a cycle costs a truncated drawing, not a stack overflow") {
    // A file can claim what a drawing cannot contain. The depth guard is the
    // only thing between that and a crash.
    Database db;
    BlockDef def;
    def.name = "LOOP";
    const BlockId id = db.add_block(std::move(def));

    // Make the definition insert itself, which BLOCK refuses but a broken DXF
    // could produce.
    const_cast<BlockDef*>(db.block(id))
        ->entities.push_back(std::make_unique<Insert>(db.block(id), Mat4::translation({1, 0, 0})));

    Insert ins(db.block(id), Mat4::identity());
    const BBox box = ins.bbox();  // must return rather than recurse forever
    CHECK(true);
    (void)box;
}

// --- placement decomposition ------------------------------------------------

TEST_CASE("insert: placement round-trips through R12's four fields") {
    const Vec3 base{0.5, 0.25, 0};

    InsertPlacement p;
    p.insertion = {3, 4, 5};
    p.scale = {2.0, 3.0, 4.0};
    p.rotation = kPi / 5.0;
    p.normal = normalize(Vec3{1, 2, 3});

    const Mat4 m = compose_placement(p, base);
    const InsertPlacement back = decompose_placement(m, base);

    CHECK(near_equal(back.insertion, p.insertion, 1e-9));
    CHECK_NEAR(back.scale.x, p.scale.x, 1e-9);
    CHECK_NEAR(back.scale.y, p.scale.y, 1e-9);
    CHECK_NEAR(back.scale.z, p.scale.z, 1e-9);
    CHECK_NEAR(back.rotation, p.rotation, 1e-9);
    CHECK(near_equal(back.normal, p.normal, 1e-9));
}

TEST_CASE("insert: a mirrored placement comes back as a negative scale") {
    // R12 records a flip as a negative scale factor rather than as a reversed
    // extrusion, so the block's own sense of up survives.
    InsertPlacement p;
    p.scale = {-1.0, 1.0, 1.0};

    const Mat4 m = compose_placement(p, Vec3{});
    const InsertPlacement back = decompose_placement(m, Vec3{});
    CHECK(back.scale.x < 0.0);
    CHECK(near_equal(back.normal, kWorldZ, 1e-9));
}

// --- DXF --------------------------------------------------------------------

TEST_CASE("blocks: definitions and insertions survive a DXF round trip") {
    Database source;
    const BlockId id = source.add_block(square_def());
    InsertPlacement p;
    p.insertion = {10, 20, 0};
    p.scale = {2, 2, 2};
    p.rotation = kPi / 4.0;
    source.add(std::make_unique<Insert>(source.block(id), compose_placement(p, Vec3{})));

    std::ostringstream out;
    DxfWriter w(out, source);
    w.write_document();

    Database loaded;
    const DxfReadResult r = read_dxf_text(loaded, out.str());
    CHECK(r.ok);
    CHECK(r.blocks == 1);
    CHECK(r.unresolved_inserts == 0);

    // One entity in the drawing; the definition's four lines are NOT entities.
    REQUIRE(loaded.size() == 1);
    REQUIRE(loaded.block_count() == 1);

    const Insert* back = last_insert(loaded);
    REQUIRE(back != nullptr);
    REQUIRE(back->definition() != nullptr);
    CHECK(back->definition()->name == "SQ");
    CHECK(back->definition()->entities.size() == 4);

    const InsertPlacement got = decompose_placement(back->placement(), Vec3{});
    CHECK(near_equal(got.insertion, Vec3{10, 20, 0}, 1e-7));
    CHECK_NEAR(got.scale.x, 2.0, 1e-7);
    CHECK_NEAR(got.rotation, kPi / 4.0, 1e-7);
}

TEST_CASE("blocks: this is the gap that used to lose definitions") {
    // Before the BLOCKS section was read, a file with blocks came back with its
    // INSERTs as proxies and its definitions gone entirely. That was the
    // data-loss bug; this is the test that says it is closed.
    Database source;
    const BlockId id = source.add_block(square_def());
    source.add(std::make_unique<Insert>(source.block(id), Mat4::identity()));

    std::ostringstream out;
    DxfWriter w(out, source);
    w.write_document();

    Database loaded;
    read_dxf_text(loaded, out.str());

    REQUIRE(loaded.block_count() == 1);
    const Insert* back = last_insert(loaded);
    REQUIRE(back != nullptr);
    CHECK(back->type() == EntityType::Insert);  // not a Proxy
}

TEST_CASE("blocks: a block referring to a block defined later still resolves") {
    // The reason inserts are resolved in a second pass rather than inline.
    const char* text =
        "  0\nSECTION\n  2\nBLOCKS\n"
        "  0\nBLOCK\n  8\n0\n  2\nOUTER\n 70\n0\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  3\nOUTER\n"
        "  0\nINSERT\n  8\n0\n  2\nINNER\n 10\n5.0\n 20\n0.0\n 30\n0.0\n"
        "  0\nENDBLK\n  8\n0\n"
        "  0\nBLOCK\n  8\n0\n  2\nINNER\n 70\n0\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  3\nINNER\n"
        "  0\nLINE\n  8\n0\n 10\n0.0\n 20\n0.0\n 30\n0.0\n 11\n1.0\n 21\n0.0\n 31\n0.0\n"
        "  0\nENDBLK\n  8\n0\n"
        "  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nINSERT\n  8\n0\n  2\nOUTER\n 10\n0.0\n 20\n0.0\n 30\n0.0\n"
        "  0\nENDSEC\n  0\nEOF\n";

    Database db;
    const DxfReadResult r = read_dxf_text(db, text);
    CHECK(r.ok);
    CHECK(r.blocks == 2);
    CHECK(r.unresolved_inserts == 0);

    const Insert* outer = last_insert(db);
    REQUIRE(outer != nullptr);
    REQUIRE(outer->definition() != nullptr);
    // The nested insert placed the inner line at x = 5..6.
    const BBox box = outer->bbox();
    REQUIRE(box.valid());
    CHECK_NEAR(box.min.x, 5.0, 1e-7);
    CHECK_NEAR(box.max.x, 6.0, 1e-7);
}

TEST_CASE("blocks: an INSERT naming no known block is counted, not dropped") {
    const char* text =
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nINSERT\n  8\n0\n  2\nGHOST\n 10\n0.0\n 20\n0.0\n 30\n0.0\n"
        "  0\nENDSEC\n  0\nEOF\n";

    Database db;
    const DxfReadResult r = read_dxf_text(db, text);
    CHECK(r.ok);
    CHECK(r.unresolved_inserts == 1);
    // Kept, so the drawing is not silently short an entity.
    CHECK(db.size() == 1);
    const Insert* ins = last_insert(db);
    REQUIRE(ins != nullptr);
    CHECK(ins->definition() == nullptr);
    CHECK(!ins->bbox().valid());
}

// --- snapping and picking through a block -----------------------------------

TEST_CASE("blocks: static snaps reach the geometry inside") {
    Database db;
    const BlockId id = db.add_block(square_def());
    Insert ins(db.block(id), Mat4::translation({10, 0, 0}));

    std::vector<OsnapPoint> snaps;
    ins.osnap_points(snaps);

    // The insertion point itself...
    CHECK(has_snap(snaps, OsnapType::Insert, {10, 0, 0}));
    // ...and the corners of the square inside, in world space.
    CHECK(has_snap(snaps, OsnapType::Endpoint, {11, 0, 0}));
    CHECK(has_snap(snaps, OsnapType::Endpoint, {11, 1, 0}));
    CHECK(has_snap(snaps, OsnapType::Midpoint, {10.5, 0, 0}));
}

TEST_CASE("blocks: NEAREST reaches inside a block reference") {
    Database db;
    const BlockId id = db.add_block(square_def());
    Insert ins(db.block(id), Mat4::translation({10, 0, 0}));

    Vec3 got{};
    REQUIRE(nearest_point(ins, Vec3{10.5, -3, 0}, &got));
    // The nearest point on the square's bottom edge.
    CHECK(near_equal(got, Vec3{10.5, 0, 0}, 1e-7));
}

TEST_CASE("blocks: a line meets the geometry inside a block") {
    Database db;
    const BlockId id = db.add_block(square_def());
    Insert ins(db.block(id), Mat4::identity());
    Line l{{-1, 0.5, 0}, {2, 0.5, 0}};

    IntersectionList hits;
    const std::size_t n = intersect(l, ins, IntersectMode::Bounded, hits);
    // Crosses the square's left and right edges.
    CHECK(n == 2);
}

// --- the commands -----------------------------------------------------------

TEST_CASE("block command: defines a block and removes the originals") {
    Database db;
    const Handle a = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));
    const Handle b = db.add(std::make_unique<Line>(Vec3{1, 0, 0}, Vec3{1, 1, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("BLOCK"));
    engine.supply(InputValue::of_string("WIDGET"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_entity(a));
    engine.supply(InputValue::of_entity(b));
    engine.supply(InputValue::none());

    REQUIRE(db.block_count() == 1);
    CHECK(db.block(db.find_block("WIDGET"))->entities.size() == 2);
    // R12 removes them: BLOCK is not a copy.
    CHECK(db.size() == 0);
}

TEST_CASE("block command: geometry is stored relative to the base point") {
    Database db;
    const Handle a = db.add(std::make_unique<Line>(Vec3{10, 10, 0}, Vec3{11, 10, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("BLOCK"));
    engine.supply(InputValue::of_string("W"));
    engine.supply(InputValue::of_point({10, 10, 0}));  // base at the line's start
    engine.supply(InputValue::of_entity(a));
    engine.supply(InputValue::none());

    const BlockDef* def = db.block(db.find_block("W"));
    REQUIRE(def != nullptr);
    REQUIRE(def->entities.size() == 1);
    const Line* l = static_cast<const Line*>(def->entities[0].get());
    // Moved to the origin, so an insertion is a plain placement.
    CHECK(near_equal(l->start(), Vec3{0, 0, 0}, 1e-9));
    CHECK(near_equal(l->end(), Vec3{1, 0, 0}, 1e-9));
}

TEST_CASE("block command: one UNDO puts the geometry back") {
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("BLOCK"));
    engine.supply(InputValue::of_string("W"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_keyword("ALL"));
    engine.supply(InputValue::none());
    CHECK(db.size() == 0);
    CHECK(db.block_count() == 1);

    engine.begin(make_command("UNDO"));
    CHECK(db.size() == 1);
    CHECK(db.block_count() == 0);
}

TEST_CASE("insert command: places a reference") {
    Database db;
    db.add_block(square_def());

    CommandEngine engine(db);
    engine.begin(make_command("INSERT"));
    engine.supply(InputValue::of_string("SQ"));
    engine.supply(InputValue::of_point({10, 20, 0}));
    engine.supply(InputValue::none());  // X scale 1
    engine.supply(InputValue::none());  // Y scale = X
    engine.supply(InputValue::none());  // rotation 0

    REQUIRE(db.size() == 1);
    const Insert* ins = last_insert(db);
    REQUIRE(ins != nullptr);
    CHECK(near_equal(ins->insertion_point(), Vec3{10, 20, 0}, 1e-9));
}

TEST_CASE("insert command: an unknown block name fails") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("INSERT"));
    const EngineStatus status = engine.supply(InputValue::of_string("NOPE"));
    CHECK(status == EngineStatus::Failed);
    CHECK(db.size() == 0);
}

TEST_CASE("insert command: one scale factor scales all three axes") {
    Database db;
    db.add_block(square_def());

    CommandEngine engine(db);
    engine.begin(make_command("INSERT"));
    engine.supply(InputValue::of_string("SQ"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(3.0));
    engine.supply(InputValue::none());  // Y defaults to X
    engine.supply(InputValue::none());

    const Insert* ins = last_insert(db);
    REQUIRE(ins != nullptr);
    const BBox box = ins->bbox();
    CHECK_NEAR(box.max.x, 3.0, 1e-9);
    CHECK_NEAR(box.max.y, 3.0, 1e-9);
}

TEST_CASE("minsert command: asks for rows and columns") {
    Database db;
    db.add_block(square_def());

    CommandEngine engine(db);
    engine.begin(make_command("MINSERT"));
    engine.supply(InputValue::of_string("SQ"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    // Enter at the X scale takes the default and skips the Y question with it,
    // so the next Enter answers rotation.
    engine.supply(InputValue::none());
    engine.supply(InputValue::none());
    engine.supply(InputValue::of_integer(2));   // rows
    engine.supply(InputValue::of_integer(3));   // columns
    engine.supply(InputValue::of_real(10.0));   // row spacing
    engine.supply(InputValue::of_real(20.0));   // column spacing

    REQUIRE(db.size() == 1);
    const Insert* ins = last_insert(db);
    REQUIRE(ins != nullptr);
    CHECK(ins->rows() == 2);
    CHECK(ins->columns() == 3);
    CHECK_NEAR(ins->bbox().max.x, 41.0, 1e-9);
}

TEST_CASE("explode: a reference becomes its contents") {
    Database db;
    const BlockId id = db.add_block(square_def());
    const Handle h = db.add(std::make_unique<Insert>(db.block(id), Mat4::translation({10, 0, 0})));

    CommandEngine engine(db);
    engine.begin(make_command("EXPLODE"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());

    // Four lines where the reference was; the reference is gone.
    CHECK(db.size() == 4);
    CHECK(db.get(h) == nullptr);
    // The definition survives -- exploding a reference does not purge the block.
    CHECK(db.block_count() == 1);

    const Entity* first = db.get(db.order()[0]);
    REQUIRE(first->type() == EntityType::Line);
    // Placed, not in definition coordinates.
    CHECK(near_equal(static_cast<const Line*>(first)->start(), Vec3{10, 0, 0}, 1e-9));
}

TEST_CASE("explode: one level only, so a nested reference stays a reference") {
    Database db;
    const BlockId inner = db.add_block(square_def("INNER"));

    BlockDef outer;
    outer.name = "OUTER";
    outer.entities.push_back(std::make_unique<Insert>(db.block(inner), Mat4::identity()));
    outer.entities.push_back(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{5, 0, 0}));
    const BlockId outer_id = db.add_block(std::move(outer));

    const Handle h = db.add(std::make_unique<Insert>(db.block(outer_id), Mat4::identity()));

    CommandEngine engine(db);
    engine.begin(make_command("EXPLODE"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());

    // One INSERT and one LINE -- not five lines.
    REQUIRE(db.size() == 2);
    bool saw_insert = false;
    for (Handle handle : db.order()) {
        if (db.get(handle)->type() == EntityType::Insert) saw_insert = true;
    }
    CHECK(saw_insert);
}

TEST_CASE("explode: a non-uniform scale is approximated rather than refused") {
    // A circle in a block scaled (2,1,1) should be an ellipse, which R12 cannot
    // represent. It explodes anyway, scaled by the X factor -- the same
    // approximation transform_frame() documents for SCALE.
    Database db;
    BlockDef def;
    def.name = "C";
    def.entities.push_back(std::make_unique<Circle>(Vec3{0, 0, 0}, 5.0));
    const BlockId id = db.add_block(std::move(def));

    InsertPlacement p;
    p.scale = {2.0, 1.0, 1.0};
    const Handle h =
        db.add(std::make_unique<Insert>(db.block(id), compose_placement(p, Vec3{})));

    CommandEngine engine(db);
    engine.begin(make_command("EXPLODE"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());

    REQUIRE(db.size() == 1);
    const Entity* e = db.get(db.order()[0]);
    REQUIRE(e->type() == EntityType::Circle);
    CHECK_NEAR(static_cast<const Circle*>(e)->radius(), 10.0, 1e-9);
}

TEST_CASE("explode: selecting something that is not a reference fails cleanly") {
    Database db;
    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));

    CommandEngine engine(db);
    engine.begin(make_command("EXPLODE"));
    engine.supply(InputValue::of_entity(h));
    const EngineStatus status = engine.supply(InputValue::none());

    CHECK(status == EngineStatus::Failed);
    CHECK(db.size() == 1);  // the line is untouched
}

TEST_CASE("explode: MINSERT yields one copy per array element") {
    Database db;
    const BlockId id = db.add_block(square_def());
    auto ins = std::make_unique<Insert>(db.block(id), Mat4::identity());
    ins->set_array(2, 3, 10.0, 20.0);
    const Handle h = db.add(std::move(ins));

    CommandEngine engine(db);
    engine.begin(make_command("EXPLODE"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());

    // Six copies of a four-line square.
    CHECK(db.size() == 24);
}

TEST_CASE("base: sets INSBASE") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("BASE"));
    engine.supply(InputValue::of_point({3, 4, 5}));

    CHECK(near_equal(db.sysvars().get_point(Sysvar::InsBase), Vec3{3, 4, 5}, 1e-9));
}
