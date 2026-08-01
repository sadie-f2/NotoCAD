// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The save cycle: the drawing's name, the modified flag, and the file commands.
//
// The centre of it is that "is this drawing saved?" is answered by a SERIAL and
// not by a depth. The branch test below is the one that matters: a depth-based
// answer passes every other test here and fails that one silently.

#include "test.hpp"

#include "ncad/command.hpp"
#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/entities.hpp"
#include "ncad/paths.hpp"
#include "ncad/sysvar.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace ncad;

namespace {

std::string temp_dir() {
    const std::filesystem::path p =
        std::filesystem::temp_directory_path() / "ncad_save_tests";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p.string();
}

std::string temp_path(const char* leaf) { return temp_dir() + "/" + leaf; }

// One committed undo group, as a command would make.
void make_edit(Database& db, double x) {
    db.journal().begin_group("LINE");
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{x, x, 0}));
    db.journal().end_group();
}

}  // namespace

// --- Dirty tracking ---------------------------------------------------------

TEST_CASE("dirty: an untouched drawing is clean, an edited one is not") {
    Database db;
    CHECK(!db.journal().dirty());

    make_edit(db, 1.0);
    CHECK(db.journal().dirty());

    db.journal().mark_saved();
    CHECK(!db.journal().dirty());
}

TEST_CASE("dirty: undo across the save point goes dirty, redo back goes clean") {
    Database db;
    make_edit(db, 1.0);
    make_edit(db, 2.0);
    db.journal().mark_saved();

    // Undoing past the saved state is a change from what is on disk.
    CHECK(db.journal().undo(db));
    CHECK(db.journal().dirty());

    // And coming back is not. This is the direction a plain "modified" boolean
    // gets wrong, because a boolean can only ever be set.
    CHECK(db.journal().redo(db));
    CHECK(!db.journal().dirty());
}

TEST_CASE("dirty: further edits past the save point are dirty") {
    Database db;
    make_edit(db, 1.0);
    db.journal().mark_saved();

    make_edit(db, 2.0);
    CHECK(db.journal().dirty());

    // Undoing back to the saved state is clean again.
    CHECK(db.journal().undo(db));
    CHECK(!db.journal().dirty());
}

TEST_CASE("dirty: a REBUILT BRANCH of the same depth stays dirty") {
    // THE TEST THAT MATTERS. A depth or a step count answers this one wrong,
    // and only this one -- which is why it would have shipped.
    //
    // Save at depth two, undo to depth one, then make a new edit. The redo
    // stack is discarded by the new work, so the depth is two again and a
    // depth comparison reports the drawing as saved. It holds different
    // geometry.
    Database db;
    make_edit(db, 1.0);
    make_edit(db, 2.0);
    db.journal().mark_saved();
    const std::size_t saved_depth = db.journal().undo_depth();

    CHECK(db.journal().undo(db));
    make_edit(db, 99.0);  // discards the redo branch

    CHECK(db.journal().undo_depth() == saved_depth);  // the trap
    CHECK(db.journal().dirty());                      // the answer

    // And it can never become clean by undoing, because that state is gone.
    CHECK(db.journal().undo(db));
    CHECK(db.journal().dirty());
}

TEST_CASE("dirty: clearing history leaves a clean drawing") {
    // NEW and OPEN land here. Nothing has been changed relative to what is on
    // disk, and an empty stack has top serial zero, so this falls out.
    Database db;
    make_edit(db, 1.0);
    CHECK(db.journal().dirty());

    db.journal().clear();
    CHECK(!db.journal().dirty());
}

// --- Path handling ----------------------------------------------------------

TEST_CASE("paths: ~ expands from HOME, and only in the forms that mean it") {
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        const std::string h = home;
        CHECK(expand_user_path("~") == h);
        CHECK(expand_user_path("~/drawings/a.dxf") == h + "/drawings/a.dxf");
    }

    // `~user/` needs getpwnam. Left verbatim rather than half-supported, so it
    // fails naming the path the user actually typed.
    CHECK(expand_user_path("~bob/a.dxf") == "~bob/a.dxf");
    CHECK(expand_user_path("a~b.dxf") == "a~b.dxf");
    CHECK(expand_user_path("") == "");
}

TEST_CASE("paths: an extension is added only when there is none") {
    CHECK(ensure_extension("plan", ".dxf") == "plan.dxf");
    CHECK(ensure_extension("plan.dxf", ".dxf") == "plan.dxf");
    CHECK(ensure_extension("PLAN.DXF", ".dxf") == "PLAN.DXF");
    // A deliberate extension is the user's business, not ours to append to.
    CHECK(ensure_extension("plan.bak", ".dxf") == "plan.bak");
}

TEST_CASE("paths: the same file spelled two ways is the same file") {
    // The bug this exists for: QSAVE warning about overwriting the drawing it
    // had just written, because the names differed textually.
    CHECK(same_file("/tmp/a.dxf", "/tmp/./a.dxf"));
    CHECK(same_file("/tmp/x/../a.dxf", "/tmp/a.dxf"));
    CHECK(!same_file("/tmp/a.dxf", "/tmp/b.dxf"));
    CHECK(!same_file("", "/tmp/a.dxf"));
}

// --- The commands -----------------------------------------------------------

TEST_CASE("save: writes, records the name, and clears the modified flag") {
    const std::string path = temp_path("basic.dxf");
    std::filesystem::remove(path);

    Database db;
    CommandEngine engine(db);
    make_edit(db, 5.0);
    CHECK(db.journal().dirty());

    engine.begin(make_command("SAVE"));
    engine.supply(InputValue::of_string(path));

    CHECK(std::filesystem::exists(path));
    CHECK(!db.journal().dirty());
    CHECK(db.sysvars().get_string(Sysvar::DwgName) == "basic.dxf");
    CHECK(!db.sysvars().get_string(Sysvar::DwgPrefix).empty());
}

TEST_CASE("save: recording the name does not itself dirty the drawing") {
    // The bug that produced Sysvars::set_metadata. DWGNAME and DWGPREFIX are
    // journalled writes if set the ordinary way, so the group closing after the
    // save point was recorded pushed a new serial -- and the drawing was dirty
    // the instant it was saved.
    const std::string path = temp_path("clean.dxf");
    std::filesystem::remove(path);

    Database db;
    CommandEngine engine(db);
    make_edit(db, 1.0);

    engine.begin(make_command("SAVE"));
    engine.supply(InputValue::of_string(path));

    CHECK(!db.journal().dirty());
}

TEST_CASE("save: a failed write leaves the drawing modified") {
    Database db;
    CommandEngine engine(db);
    make_edit(db, 1.0);

    // A path that cannot be created. The flag must survive, or a full disk
    // quietly becomes a lost drawing.
    //
    // The unwritable path is built rather than hardcoded: a REGULAR FILE used
    // as a directory component fails with ENOTDIR on every POSIX system, and
    // fails for that reason regardless of privileges. This used to be
    // /proc/..., which does not exist on macOS at all -- so the test passed
    // there by accident rather than by testing what it names.
    const std::string blocker = temp_path("not-a-directory");
    { std::ofstream make_it(blocker); }
    REQUIRE(std::filesystem::is_regular_file(blocker));

    engine.begin(make_command("SAVE"));
    engine.supply(InputValue::of_string(blocker + "/nope.dxf"));

    CHECK(db.journal().dirty());
    CHECK(db.sysvars().get_string(Sysvar::DwgName).empty());
}

TEST_CASE("saveas: an existing OTHER file must be confirmed, and No means no") {
    const std::string first = temp_path("one.dxf");
    const std::string second = temp_path("two.dxf");
    std::filesystem::remove(first);
    std::filesystem::remove(second);

    Database db;
    CommandEngine engine(db);
    make_edit(db, 1.0);

    engine.begin(make_command("SAVE"));
    engine.supply(InputValue::of_string(first));
    engine.begin(make_command("SAVE"));
    engine.supply(InputValue::of_string(second));
    CHECK(std::filesystem::exists(second));

    // Now SAVEAS back over the first: it exists and is not the current drawing.
    make_edit(db, 2.0);
    engine.begin(make_command("SAVEAS"));
    engine.supply(InputValue::of_string(first));
    engine.supply(InputValue::of_keyword("NO"));

    // Refused, so the drawing is still modified and still named the second.
    CHECK(db.journal().dirty());
    CHECK(db.sysvars().get_string(Sysvar::DwgName) == "two.dxf");
}

TEST_CASE("qsave: overwrites the current drawing without asking") {
    const std::string path = temp_path("quick.dxf");
    std::filesystem::remove(path);

    Database db;
    CommandEngine engine(db);
    make_edit(db, 1.0);

    engine.begin(make_command("SAVE"));
    engine.supply(InputValue::of_string(path));

    make_edit(db, 2.0);
    CHECK(db.journal().dirty());

    // No prompt: one step and it is done. Asking here would defeat the command.
    engine.begin(make_command("QSAVE"));
    CHECK(!db.journal().dirty());
}

TEST_CASE("new: keeps the drawing unless told plainly to discard it") {
    Database db;
    CommandEngine engine(db);
    make_edit(db, 1.0);

    engine.begin(make_command("NEW"));
    engine.supply(InputValue::of_keyword("NO"));
    CHECK(db.order().size() == 1);  // still there

    engine.begin(make_command("NEW"));
    engine.supply(InputValue::of_keyword("YES"));
    CHECK(db.order().empty());
    CHECK(!db.journal().dirty());
}

TEST_CASE("new: a clean drawing is replaced without a question") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("NEW"));
    // start() finished it: nothing to lose, so nothing to ask.
    CHECK(!engine.active());
}

TEST_CASE("open: the drawing remembers the file it came from") {
    // Reported from use: open, edit, SAVE -- and the prompt asked for a name as
    // though the drawing had come from nowhere. DWGNAME was set by SAVE and by
    // nothing else.
    const std::string path = temp_path("opened.dxf");
    std::filesystem::remove(path);

    {
        Database seed;
        CommandEngine engine(seed);
        make_edit(seed, 3.0);
        engine.begin(make_command("SAVE"));
        engine.supply(InputValue::of_string(path));
    }

    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("OPEN"));
    engine.supply(InputValue::of_string(path));

    CHECK(db.sysvars().get_string(Sysvar::DwgName) == "opened.dxf");
    CHECK(!db.journal().dirty());  // a freshly opened drawing owes nothing

    // And QSAVE can now write it back without asking anything at all.
    make_edit(db, 4.0);
    engine.begin(make_command("QSAVE"));
    CHECK(!engine.active());
    CHECK(!db.journal().dirty());
}

TEST_CASE("open: a stale name does not survive onto a different drawing") {
    const std::string first = temp_path("stale_a.dxf");
    const std::string second = temp_path("stale_b.dxf");
    std::filesystem::remove(first);
    std::filesystem::remove(second);

    Database db;
    CommandEngine engine(db);
    make_edit(db, 1.0);

    engine.begin(make_command("SAVE"));
    engine.supply(InputValue::of_string(first));

    // Build a second file, then open it. The name must follow the OPEN, or SAVE
    // would offer the first drawing's name for the second drawing's contents.
    {
        Database other;
        CommandEngine oe(other);
        make_edit(other, 2.0);
        oe.begin(make_command("SAVE"));
        oe.supply(InputValue::of_string(second));
    }

    engine.begin(make_command("OPEN"));
    engine.supply(InputValue::of_string(second));
    CHECK(db.sysvars().get_string(Sysvar::DwgName) == "stale_b.dxf");
}

TEST_CASE("open: a failed read leaves the drawing and its name alone") {
    const std::string path = temp_path("keeper.dxf");
    std::filesystem::remove(path);

    Database db;
    CommandEngine engine(db);
    make_edit(db, 1.0);
    engine.begin(make_command("SAVE"));
    engine.supply(InputValue::of_string(path));

    engine.begin(make_command("OPEN"));
    engine.supply(InputValue::of_string(temp_path("no_such_file_here.dxf")));

    CHECK(db.order().size() == 1);
    CHECK(db.sysvars().get_string(Sysvar::DwgName) == "keeper.dxf");
}
