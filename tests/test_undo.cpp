// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/entities.hpp"
#include "ncad/sysvar.hpp"
#include "ncad/undo.hpp"

#include <memory>

using namespace ncad;

namespace {

Handle add_line(Database& db, double x0, double y0, double x1, double y1) {
    return db.add(std::make_unique<Line>(Vec3{x0, y0, 0}, Vec3{x1, y1, 0}));
}

const Line* as_line(const Database& db, Handle h) {
    const Entity* e = db.get(h);
    return (e && e->type() == EntityType::Line) ? static_cast<const Line*>(e) : nullptr;
}

// Drives a whole command from a script of values, as the engine would.
void run(CommandEngine& engine, const char* name, std::initializer_list<InputValue> values) {
    engine.begin(make_command(name));
    for (const InputValue& v : values) engine.supply(v);
}

}  // namespace

TEST_CASE("undo: a new database has no history") {
    Database db;
    CHECK(!db.journal().can_undo());
    CHECK(!db.journal().can_redo());

    // The layer and linetype tables a new drawing is born with are what it *is*,
    // not something done to it, so they are not an undo step.
    CHECK(db.journal().undo_depth() == 0);
}

TEST_CASE("undo: an add outside a command is its own step") {
    Database db;
    const Handle h = add_line(db, 0, 0, 10, 0);
    CHECK(db.journal().can_undo());
    CHECK(db.size() == 1);

    CHECK(db.journal().undo(db));
    CHECK(db.size() == 0);
    CHECK(db.get(h) == nullptr);

    CHECK(db.journal().redo(db));
    CHECK(db.size() == 1);
    CHECK(db.get(h) != nullptr);  // the same handle, not a new one
}

TEST_CASE("undo: an erase comes back where it was") {
    Database db;
    const Handle a = add_line(db, 0, 0, 1, 0);
    const Handle b = add_line(db, 0, 1, 1, 1);
    const Handle c = add_line(db, 0, 2, 1, 2);

    CHECK(db.erase(b));
    CHECK(db.order().size() == 2);

    CHECK(db.journal().undo(db));
    CHECK(db.order().size() == 3);

    // Restored to its old position in the drawing order, not appended. Order is
    // what decides which entity draws on top, and what makes DXF deterministic.
    CHECK(db.order()[0] == a);
    CHECK(db.order()[1] == b);
    CHECK(db.order()[2] == c);
}

TEST_CASE("undo: a modification restores the old geometry") {
    Database db;
    const Handle h = add_line(db, 0, 0, 10, 0);

    auto changed = std::make_unique<Line>(Vec3{5, 5, 0}, Vec3{20, 20, 0});
    CHECK(db.replace(h, std::move(changed)));
    CHECK_VEC(as_line(db, h)->end(), 20.0, 20.0, 0.0, 1e-12);

    CHECK(db.journal().undo(db));
    CHECK_VEC(as_line(db, h)->start(), 0.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(as_line(db, h)->end(), 10.0, 0.0, 0.0, 1e-12);

    CHECK(db.journal().redo(db));
    CHECK_VEC(as_line(db, h)->end(), 20.0, 20.0, 0.0, 1e-12);
}

TEST_CASE("undo: system variables are journalled too") {
    Database db;
    const std::int32_t original = db.sysvars().get_int(Sysvar::OsMode);

    CHECK(db.sysvars().set_int(Sysvar::OsMode, 47) == Sysvars::SetStatus::Ok);
    CHECK(db.sysvars().get_int(Sysvar::OsMode) == 47);

    // Anything mutable that is not journalled is a hole in undo. Sysvars are
    // mutable, so they are journalled.
    CHECK(db.journal().undo(db));
    CHECK(db.sysvars().get_int(Sysvar::OsMode) == original);

    CHECK(db.journal().redo(db));
    CHECK(db.sysvars().get_int(Sysvar::OsMode) == 47);
}

TEST_CASE("undo: a rejected sysvar write is not a step") {
    Database db;
    CHECK(db.sysvars().set_int(Sysvar::PickBox, 900) == Sysvars::SetStatus::OutOfRange);
    CHECK(!db.journal().can_undo());
}

TEST_CASE("undo: one command is one step, however many segments") {
    Database db;
    CommandEngine engine(db);

    // Four points is three segments, and R12 undoes the LINE, not a segment.
    run(engine, "LINE",
        {InputValue::of_point({0, 0, 0}), InputValue::of_point({10, 0, 0}),
         InputValue::of_point({10, 10, 0}), InputValue::of_point({0, 10, 0}),
         InputValue::none()});
    CHECK(db.size() == 3);
    CHECK(db.journal().undo_depth() == 1);

    CHECK(db.journal().undo(db));
    CHECK(db.size() == 0);

    CHECK(db.journal().redo(db));
    CHECK(db.size() == 3);
}

TEST_CASE("undo: the group is named after the command") {
    Database db;
    CommandEngine engine(db);
    run(engine, "LINE",
        {InputValue::of_point({0, 0, 0}), InputValue::of_point({10, 0, 0}), InputValue::none()});
    CHECK(db.journal().undo_name() == "LINE");
}

TEST_CASE("undo: a command that changes nothing costs no step") {
    Database db;
    CommandEngine engine(db);

    // Started and immediately ended with nothing drawn. An undo that visibly
    // does nothing is worse than no undo entry at all.
    engine.begin(make_command("LINE"));
    engine.supply(InputValue::none());
    CHECK(!db.journal().can_undo());
}

TEST_CASE("undo: work committed before Escape survives as one step") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::of_point({20, 0, 0}));
    CHECK(db.size() == 2);

    // R12 keeps committed work through Escape. So do we -- and it is one
    // undoable step, not two, and not zero.
    engine.supply(InputValue::cancel());
    CHECK(db.size() == 2);
    CHECK(db.journal().undo_depth() == 1);

    CHECK(db.journal().undo(db));
    CHECK(db.size() == 0);
}

TEST_CASE("undo: several commands undo one at a time, newest first") {
    Database db;
    CommandEngine engine(db);

    run(engine, "LINE",
        {InputValue::of_point({0, 0, 0}), InputValue::of_point({10, 0, 0}), InputValue::none()});
    run(engine, "CIRCLE", {InputValue::of_point({0, 0, 0}), InputValue::of_real(5.0)});
    run(engine, "LINE",
        {InputValue::of_point({0, 5, 0}), InputValue::of_point({9, 5, 0}), InputValue::none()});

    CHECK(db.size() == 3);
    CHECK(db.journal().undo_depth() == 3);

    db.journal().undo(db);
    CHECK(db.size() == 2);
    db.journal().undo(db);
    CHECK(db.size() == 1);
    CHECK(db.get(db.order().front())->type() == EntityType::Line);
    db.journal().undo(db);
    CHECK(db.size() == 0);
    CHECK(!db.journal().can_undo());

    // And all the way forward again.
    while (db.journal().can_redo()) db.journal().redo(db);
    CHECK(db.size() == 3);
}

TEST_CASE("undo: new work discards the redo stack") {
    Database db;
    add_line(db, 0, 0, 1, 0);
    db.journal().undo(db);
    CHECK(db.journal().can_redo());

    // Branching history is a maze. Doing something new abandons the future you
    // undid past, which is what every editor does and what users expect.
    add_line(db, 0, 5, 1, 5);
    CHECK(!db.journal().can_redo());
}

TEST_CASE("undo: undoing does not itself become undoable") {
    Database db;
    add_line(db, 0, 0, 1, 0);
    const std::size_t before = db.journal().undo_depth();

    db.journal().undo(db);
    // The undo consumed one step and journalled nothing of its own; an undo
    // that could be undone by another undo is a history with no bottom.
    CHECK(db.journal().undo_depth() == before - 1);
    CHECK(db.journal().redo_depth() == 1);
}

TEST_CASE("undo: the UNDO and REDO commands") {
    Database db;
    CommandEngine engine(db);

    run(engine, "LINE",
        {InputValue::of_point({0, 0, 0}), InputValue::of_point({10, 0, 0}), InputValue::none()});
    CHECK(db.size() == 1);

    engine.begin(make_command("UNDO"));
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(db.size() == 0);

    engine.begin(make_command("REDO"));
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(db.size() == 1);

    // Running UNDO must not itself land on the stack, or REDO would replay an
    // undo instead of the work.
    engine.begin(make_command("UNDO"));
    CHECK(db.size() == 0);
    engine.begin(make_command("UNDO"));
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(engine.message() == "Nothing to undo");
}

TEST_CASE("undo: U is the alias, as in R12") {
    CHECK(resolve_command_name("U").name == "UNDO");
    CHECK(make_command("undo") != nullptr);
    CHECK(make_command("redo") != nullptr);
}

TEST_CASE("undo: nested groups collapse into one step") {
    Database db;
    UndoJournal& j = db.journal();

    // What a LISP function calling (command ...) several times must produce:
    // one step, not one per inner command.
    j.begin_group("OUTER");
    j.begin_group("INNER");
    add_line(db, 0, 0, 1, 0);
    j.end_group();
    add_line(db, 0, 1, 1, 1);
    j.end_group();

    CHECK(db.size() == 2);
    CHECK(j.undo_depth() == 1);
    CHECK(j.undo_name() == "OUTER");

    j.undo(db);
    CHECK(db.size() == 0);
}

TEST_CASE("undo: history is unlimited, not a ring") {
    Database db;
    for (int i = 0; i < 500; ++i) add_line(db, 0, i, 1, i);
    CHECK(db.journal().undo_depth() == 500);

    // All the way back to the start of the session, which is the requirement.
    while (db.journal().can_undo()) db.journal().undo(db);
    CHECK(db.size() == 0);
    CHECK(db.journal().redo_depth() == 500);
}

TEST_CASE("undo: a restored handle is never reissued") {
    Database db;
    const Handle h = add_line(db, 0, 0, 1, 0);
    db.journal().undo(db);
    db.journal().redo(db);

    // The entity came back under its old handle, so the next fresh handle must
    // be beyond it -- AutoLISP enames and DXF both depend on handles being
    // unique for the life of the drawing.
    const Handle next = add_line(db, 0, 9, 1, 9);
    CHECK(next != h);
    CHECK(db.get(h) != nullptr);
    CHECK(db.get(next) != nullptr);
}

TEST_CASE("undo: layer changes are journalled") {
    Database db;
    const LayerId walls = db.add_layer("WALLS", 3);
    CHECK(db.layer(walls).color == 3);

    CHECK(db.set_layer_color(walls, 5));
    CHECK(db.layer(walls).color == 5);

    // Anything mutable that is not journalled is a hole in undo, and a layer is
    // mutable. Turning a layer off and undoing must bring it back.
    CHECK(db.journal().undo(db));
    CHECK(db.layer(walls).color == 3);

    CHECK(db.journal().redo(db));
    CHECK(db.layer(walls).color == 5);
}

TEST_CASE("undo: freezing and locking are journalled too") {
    Database db;
    const LayerId walls = db.add_layer("WALLS");

    CHECK(db.set_layer_frozen(walls, true));
    CHECK(db.set_layer_locked(walls, true));
    CHECK(db.layer(walls).frozen);
    CHECK(db.layer(walls).locked);

    CHECK(db.journal().undo(db));
    CHECK(!db.layer(walls).locked);
    CHECK(db.layer(walls).frozen);  // one step at a time

    CHECK(db.journal().undo(db));
    CHECK(!db.layer(walls).frozen);
}

TEST_CASE("undo: adding a layer is a step") {
    Database db;
    const std::size_t before = db.layers().size();
    const LayerId walls = db.add_layer("WALLS");
    CHECK(db.layers().size() == before + 1);
    CHECK(walls == before);

    CHECK(db.journal().undo(db));
    CHECK(db.layers().size() == before);

    CHECK(db.journal().redo(db));
    CHECK(db.layers().size() == before + 1);
    CHECK(db.layer(walls).name == "WALLS");
}

TEST_CASE("undo: layer 0 and CONTINUOUS survive everything") {
    Database db;
    db.add_layer("WALLS");
    db.add_linetype("HIDDEN", "dashed", {0.5, -0.25});

    while (db.journal().can_undo()) db.journal().undo(db);

    // A new drawing is born with these. Undoing past them would leave entities
    // pointing at a layer table with nothing in it.
    CHECK(db.layers().size() >= 1);
    CHECK(db.layer(kLayerZero).name == "0");
    CHECK(db.linetypes().size() >= 1);
}

TEST_CASE("undo: a linetype's pattern is journalled") {
    Database db;
    const LinetypeId hidden = db.add_linetype("HIDDEN", "dashed", {0.5, -0.25});
    CHECK_NEAR(db.linetype(hidden).pattern_length(), 0.75, 1e-12);

    Linetype changed = db.linetype(hidden);
    changed.pattern = {1.0, -1.0, 0.0, -1.0};
    CHECK(db.set_linetype(hidden, changed));
    CHECK(db.linetype(hidden).pattern.size() == 4);

    CHECK(db.journal().undo(db));
    CHECK(db.linetype(hidden).pattern.size() == 2);
}

TEST_CASE("undo: a layer change inside a command is part of that command's step") {
    Database db;
    CommandEngine engine(db);
    const LayerId walls = db.add_layer("WALLS", 3);

    // Simulate what a LAYER command will do: change a layer inside a group.
    db.journal().begin_group("LAYER");
    db.set_layer_color(walls, 1);
    db.set_layer_frozen(walls, true);
    db.journal().end_group();

    // Two changes, one command, one undo.
    CHECK(db.journal().undo(db));
    CHECK(db.layer(walls).color == 3);
    CHECK(!db.layer(walls).frozen);
}

TEST_CASE("undo: a popped block definition outlives the Inserts that point at it") {
    // An Insert holds a raw `const BlockDef*`, and the undo stack holds Insert
    // CLONES that still point at it. Undoing far enough to remove the block
    // used to FREE the definition while those clones were alive, so redo handed
    // the drawing an Insert pointing at dead memory -- a use-after-free that
    // ASan catches on the next bbox(), draw() or dxf_write().
    //
    // Found by audit, reachable by typing six commands:
    //   LINE / BLOCK / INSERT / U / U / REDO / REDO / DXFOUT
    Database db;

    BlockDef def;
    def.name = "A";
    def.entities.push_back(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{5, 5, 0}));
    const BlockId id = db.add_block(std::move(def));
    const BlockDef* address_before = db.block(id);
    REQUIRE(address_before != nullptr);

    db.add(std::make_unique<Insert>(address_before, Mat4::identity()));

    // Undo past the insert and past the block itself.
    CHECK(db.journal().undo(db));
    CHECK(db.journal().undo(db));
    CHECK(db.find_block("A") == kInvalidBlock);

    // Redo brings both back. The definition must return at the SAME address,
    // because the Insert being restored alongside it still holds that pointer.
    CHECK(db.journal().redo(db));
    CHECK(db.journal().redo(db));

    const BlockDef* address_after = db.block(db.find_block("A"));
    REQUIRE(address_after != nullptr);
    CHECK(address_after == address_before);

    // And the restored Insert resolves through it rather than into freed
    // memory. Under ASan this line is the actual test.
    REQUIRE(db.size() == 1);
    const Entity* e = db.get(db.order().front());
    REQUIRE(e != nullptr);
    REQUIRE(e->type() == EntityType::Insert);
    const Insert* ins = static_cast<const Insert*>(e);
    REQUIRE(ins->definition() != nullptr);
    CHECK(ins->definition()->name == "A");
    CHECK(ins->bbox().valid());
}
