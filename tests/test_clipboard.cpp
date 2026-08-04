// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// COPYCLIP, CUTCLIP, PASTECLIP, and the DXF fragment they trade in.
//
// The fixture that matters here is TWO databases sharing one clipboard --
// that is the actual feature, a copy crossing windows -- so most cases build
// a source drawing, copy from it, and paste into a different drawing.

#include "test.hpp"

#include "ncad/clipboard.hpp"
#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/entities.hpp"

#include <memory>

using namespace ncad;

namespace {

const Line* line_at(const Database& db, std::size_t i) {
    const Entity* e = db.get(db.order()[i]);
    if (!e || e->type() != EntityType::Line) return nullptr;
    return static_cast<const Line*>(e);
}

BlockDef square_def(const char* name = "SQ") {
    BlockDef def;
    def.name = name;
    def.entities.push_back(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));
    def.entities.push_back(std::make_unique<Line>(Vec3{1, 0, 0}, Vec3{1, 1, 0}));
    def.entities.push_back(std::make_unique<Line>(Vec3{1, 1, 0}, Vec3{0, 1, 0}));
    def.entities.push_back(std::make_unique<Line>(Vec3{0, 1, 0}, Vec3{0, 0, 0}));
    return def;
}

}  // namespace

TEST_CASE("clipboard: the fragment is a DXF document carrying $INSBASE") {
    Database db;
    const Handle h = db.add(std::make_unique<Line>(Vec3{2, 3, 0}, Vec3{12, 3, 0}));

    const std::string text = clip_fragment(db, {h}, Vec3{2, 3, 0});
    CHECK(clip_looks_like_dxf(text));
    CHECK_VEC(clip_base(text), 2.0, 3.0, 0.0, 1e-12);

    // And what does not look like DXF, does not.
    CHECK(!clip_looks_like_dxf("hello world"));
    CHECK(!clip_looks_like_dxf(""));
    CHECK(!clip_looks_like_dxf("12.5"));
}

TEST_CASE("clipboard: the scanner takes either line ending") {
    Database db;
    const Handle h = db.add(std::make_unique<Line>(Vec3{1, 2, 3}, Vec3{4, 5, 6}));
    const std::string text = clip_fragment(db, {h}, Vec3{1, 2, 3});

    // The writer emits CRLF, so the fragment itself already covers that side.
    // A clipboard that crossed another program may come back LF-only -- and a
    // paste must not care which it got.
    std::string lf;
    for (const char c : text) {
        if (c != '\r') lf += c;
    }
    CHECK(lf.size() < text.size());  // the writer really does emit CRLF
    CHECK(clip_looks_like_dxf(lf));
    CHECK_VEC(clip_base(lf), 1.0, 2.0, 3.0, 1e-12);
}

TEST_CASE("clipboard: copy in one drawing, paste displaced into another") {
    Database src;
    CommandEngine copier(src);
    InProcessClipboard clip;
    copier.set_clipboard(&clip);

    const Handle h = src.add(std::make_unique<Line>(Vec3{2, 3, 0}, Vec3{12, 3, 0}));

    copier.begin(make_command("COPYCLIP"));
    copier.supply(InputValue::of_entity(h));
    copier.supply(InputValue::none());
    CHECK(copier.status() == EngineStatus::Finished);
    CHECK(src.size() == 1);  // COPYCLIP does not erase

    Database dst;
    CommandEngine paster(dst);
    paster.set_clipboard(&clip);

    paster.begin(make_command("PASTECLIP"));
    paster.supply(InputValue::of_point({100, 100, 0}));
    CHECK(paster.status() == EngineStatus::Finished);
    CHECK(dst.size() == 1);

    // The base point was the selection's bbox corner, (2,3,0); the line lands
    // with that corner at the picked point.
    CHECK_VEC(line_at(dst, 0)->start(), 100.0, 100.0, 0.0, 1e-12);
    CHECK_VEC(line_at(dst, 0)->end(), 110.0, 100.0, 0.0, 1e-12);
}

TEST_CASE("clipboard: Enter pastes in place, by the fragment's own coordinates") {
    Database src;
    CommandEngine copier(src);
    InProcessClipboard clip;
    copier.set_clipboard(&clip);

    src.add(std::make_unique<Line>(Vec3{7, 8, 9}, Vec3{17, 8, 9}));
    copier.begin(make_command("COPYCLIP"));
    copier.supply(InputValue::of_keyword("ALL"));
    copier.supply(InputValue::none());

    Database dst;
    CommandEngine paster(dst);
    paster.set_clipboard(&clip);
    paster.begin(make_command("PASTECLIP"));
    paster.supply(InputValue::none());
    CHECK(paster.status() == EngineStatus::Finished);

    CHECK_VEC(line_at(dst, 0)->start(), 7.0, 8.0, 9.0, 1e-12);
}

TEST_CASE("clipboard: CUTCLIP erases what it copied, and undo restores it") {
    Database db;
    CommandEngine engine(db);
    InProcessClipboard clip;
    engine.set_clipboard(&clip);

    const Handle h = db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));

    engine.begin(make_command("CUTCLIP"));
    engine.supply(InputValue::of_entity(h));
    engine.supply(InputValue::none());
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(db.empty());

    std::string text;
    CHECK(clip.get_text(text));
    CHECK(clip_looks_like_dxf(text));

    // One undo brings the entity back: the copy and the erase were one group.
    engine.begin(make_command("UNDO"));
    CHECK(db.size() == 1);
}

TEST_CASE("clipboard: layers and linetypes travel, and the existing name wins") {
    Database src;
    const LinetypeId lt = src.add_linetype("DASHED", "dashes", {0.5, -0.25});
    const LayerId walls = src.add_layer("WALLS", 1, lt);

    auto line = std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{5, 0, 0});
    line->props().layer = walls;
    line->props().linetype = lt;
    const Handle h = src.add(std::move(line));

    InProcessClipboard clip;
    CommandEngine copier(src);
    copier.set_clipboard(&clip);
    copier.begin(make_command("COPYCLIP"));
    copier.supply(InputValue::of_entity(h));
    copier.supply(InputValue::none());

    // The destination already has a WALLS layer with its own colour. Paste
    // must not repaint it: the drawing pasted into owns its own definitions,
    // the same rule DXFIN Merge applies.
    Database dst;
    const LayerId existing = dst.add_layer("WALLS", 3);
    CommandEngine paster(dst);
    paster.set_clipboard(&clip);
    paster.begin(make_command("PASTECLIP"));
    paster.supply(InputValue::none());
    CHECK(paster.status() == EngineStatus::Finished);

    CHECK(dst.size() == 1);
    const Entity* pasted = dst.get(dst.last());
    CHECK(dst.layer(pasted->props().layer).name == "WALLS");
    CHECK(dst.layer(existing).color == 3);

    // The linetype was NOT already there, so it arrives with its pattern.
    const LinetypeId arrived = dst.find_linetype("DASHED");
    CHECK(arrived != kInvalidLinetype);
    CHECK(dst.linetype(arrived).pattern.size() == 2);
}

TEST_CASE("clipboard: an INSERT travels with its definition, nested blocks included") {
    Database src;
    const BlockId inner = src.add_block(square_def("INNER"));

    // OUTER holds geometry of its own plus an insert of INNER.
    BlockDef outer;
    outer.name = "OUTER";
    outer.entities.push_back(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{2, 0, 0}));
    outer.entities.push_back(
        std::make_unique<Insert>(src.block(inner), Mat4::translation({5, 0, 0})));
    const BlockId outer_id = src.add_block(std::move(outer));

    const Handle h =
        src.add(std::make_unique<Insert>(src.block(outer_id), Mat4::translation({10, 10, 0})));

    InProcessClipboard clip;
    CommandEngine copier(src);
    copier.set_clipboard(&clip);
    copier.begin(make_command("COPYCLIP"));
    copier.supply(InputValue::of_entity(h));
    copier.supply(InputValue::none());

    Database dst;
    CommandEngine paster(dst);
    paster.set_clipboard(&clip);
    paster.begin(make_command("PASTECLIP"));
    paster.supply(InputValue::of_point({0, 0, 0}));
    CHECK(paster.status() == EngineStatus::Finished);

    // Both definitions crossed, and the pasted insert resolves -- against the
    // destination's copy, not a pointer into the source database.
    CHECK(dst.find_block("OUTER") != kInvalidBlock);
    CHECK(dst.find_block("INNER") != kInvalidBlock);

    const Entity* pasted = dst.get(dst.last());
    CHECK(pasted->type() == EntityType::Insert);
    const auto* ins = static_cast<const Insert*>(pasted);
    CHECK(ins->definition() != nullptr);
    CHECK(ins->definition() == dst.block(dst.find_block("OUTER")));
}

TEST_CASE("clipboard: the paste is one undo step and becomes Previous") {
    Database src;
    CommandEngine copier(src);
    InProcessClipboard clip;
    copier.set_clipboard(&clip);
    src.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));
    src.add(std::make_unique<Line>(Vec3{0, 1, 0}, Vec3{1, 1, 0}));
    copier.begin(make_command("COPYCLIP"));
    copier.supply(InputValue::of_keyword("ALL"));
    copier.supply(InputValue::none());

    Database dst;
    CommandEngine paster(dst);
    paster.set_clipboard(&clip);
    paster.begin(make_command("PASTECLIP"));
    paster.supply(InputValue::of_point({50, 0, 0}));
    CHECK(dst.size() == 2);

    // MOVE Previous picks up exactly what was pasted.
    paster.begin(make_command("MOVE"));
    paster.supply(InputValue::of_keyword("PREVIOUS"));
    paster.supply(InputValue::none());
    paster.supply(InputValue::of_point({0, 0, 0}));
    paster.supply(InputValue::of_point({0, 0, 5}));
    CHECK(paster.status() == EngineStatus::Finished);
    CHECK(line_at(dst, 0)->start().z == 5.0);
    CHECK(line_at(dst, 1)->start().z == 5.0);

    // Undo the move, then the paste: the paste comes off as ONE step.
    paster.begin(make_command("UNDO"));
    CHECK(dst.size() == 2);
    paster.begin(make_command("UNDO"));
    CHECK(dst.empty());
}

TEST_CASE("clipboard: paste refuses a clipboard that holds no DXF") {
    Database db;
    CommandEngine engine(db);
    InProcessClipboard clip;
    engine.set_clipboard(&clip);

    clip.set_text("just some words");
    engine.begin(make_command("PASTECLIP"));
    CHECK(engine.status() == EngineStatus::Failed);
    CHECK(db.empty());
}

TEST_CASE("clipboard: commands fail plainly when no clipboard is wired") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("COPYCLIP"));
    CHECK(engine.status() == EngineStatus::Failed);
    engine.begin(make_command("PASTECLIP"));
    CHECK(engine.status() == EngineStatus::Failed);
}
