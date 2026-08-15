// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Advisory drawing locks: AutoCAD's sibling `.dwl` / `.dwl2`.
//
// The point of these is a WARNING before clobbering a working session, not a
// mechanism nobody can override -- nothing in the OS enforces them and AutoCAD
// itself lets you past with a warning. So what is worth pinning is that the
// warning appears where it can still prevent the loss, that the default answer
// is the one that destroys nothing, and that an unreadable lock is still
// reported rather than swallowed.

#include "test.hpp"

#include "ncad/command.hpp"
#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/dxf_read.hpp"
#include "ncad/entities.hpp"
#include "ncad/paths.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace ncad;

namespace {

std::string temp_dir() {
    const std::filesystem::path p = std::filesystem::temp_directory_path() / "ncad_lock_tests";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p.string();
}

std::string temp_path(const char* leaf) { return temp_dir() + "/" + leaf; }

void write_file(const std::string& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

// A real `.dwl`, byte for byte, from AutoCAD 2026 holding a .dxf open.
// Specimens and notes in examples/acad-locks. Three lines, NO trailing newline,
// a trailing SPACE on the machine line, and CP1252 -- the apostrophe in
// "Sadie's MacBook Pro" is the single byte 0x92.
const char kRealDwl[] =
    "sadie\n"
    "Sadie\x92s MacBook Pro \n"
    "Friday, August 14, 2026  19:52:45 Eastern Daylight Time";

// And the matching `.dwl2`. Same fields, UTF-8 this time -- the same character
// is three bytes here -- and the declaration is MALFORMED: no closing `?`, so
// a real XML parser rejects the whole file.
const char kRealDwl2[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\">\n"
    "<whprops>\n"
    "<username>sadie</username>\n"
    "<machinename>Sadie\xe2\x80\x99s MacBook Pro </machinename>\n"
    "<fullname></fullname>\n"
    "<datetime>Friday, August 14, 2026  19:52:45 Eastern Daylight Time</datetime>\n"
    "</whprops>";

// The machine name as it should come back from either: UTF-8, trailing space
// trimmed.
const char kMachine[] = "Sadie\xe2\x80\x99s MacBook Pro";

// A drawing on disk with no lock beside it.
std::string fresh_drawing(const char* leaf) {
    const std::string path = temp_path(leaf);
    std::filesystem::remove(path);
    std::filesystem::remove(lock_path_for(path, ".dwl"));
    std::filesystem::remove(lock_path_for(path, ".dwl2"));

    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 10, 0}));
    CommandEngine engine(db);
    engine.begin(make_command("SAVE"));
    engine.supply(InputValue::of_string(path));
    engine.supply(InputValue::none());  // the version prompt takes its default
    return path;
}

std::size_t entity_count_of(const std::string& path) {
    Database db;
    read_dxf_file(db, path, DxfReadMode::Replace);
    return db.size();
}

}  // namespace

TEST_CASE("lock: the lock is named for the drawing, not for its format") {
    // plan.dxf locks as plan.dwl, exactly as plan.dwg does -- the extension is
    // REPLACED, not appended, or a lock AutoCAD wrote would never be found.
    CHECK(path_filename(lock_path_for("/tmp/plan.dxf", ".dwl")) == "plan.dwl");
    CHECK(path_filename(lock_path_for("/tmp/plan.dwg", ".dwl")) == "plan.dwl");
    CHECK(path_filename(lock_path_for("/tmp/plan.dxf", ".dwl2")) == "plan.dwl2");
    CHECK(lock_path_for("", ".dwl").empty());
}

TEST_CASE("lock: no sibling means no lock") {
    const std::string path = temp_path("unlocked.dxf");
    std::filesystem::remove(lock_path_for(path, ".dwl"));
    std::filesystem::remove(lock_path_for(path, ".dwl2"));

    const DrawingLock lock = read_drawing_lock(path);
    CHECK(!lock.present);
    // Empty, so a caller can use the description itself as the test.
    CHECK(describe_lock(lock).empty());
}

TEST_CASE("lock: a real .dwl, byte for byte, in CP1252") {
    // The encoding is not a detail. AutoCAD writes .dwl in CP1252, and macOS
    // names a machine "Sadie's MacBook Pro" with a curly apostrophe -- so the
    // very first real lock file on this platform carries a high byte. Stripping
    // it, which is what this did before a real pair was measured, silently
    // gives "Sadies MacBook Pro".
    const std::string path = temp_path("real-dwl.dxf");
    std::filesystem::remove(lock_path_for(path, ".dwl2"));
    write_file(lock_path_for(path, ".dwl"), kRealDwl);

    const DrawingLock lock = read_drawing_lock(path);
    CHECK(lock.present);
    CHECK(lock.owner == "sadie");
    CHECK(lock.machine == kMachine);

    std::filesystem::remove(lock_path_for(path, ".dwl"));
}

TEST_CASE("lock: a real .dwl2, byte for byte, in UTF-8") {
    // The SAME character, three bytes this time. Two files, two encodings, and
    // nothing anywhere says so -- which is why the bytes are sniffed rather
    // than the extension trusted.
    const std::string path = temp_path("real-dwl2.dxf");
    std::filesystem::remove(lock_path_for(path, ".dwl"));
    write_file(lock_path_for(path, ".dwl2"), kRealDwl2);

    const DrawingLock lock = read_drawing_lock(path);
    CHECK(lock.present);
    CHECK(lock.owner == "sadie");
    CHECK(lock.machine == kMachine);

    std::filesystem::remove(lock_path_for(path, ".dwl2"));
}

TEST_CASE("lock: the malformed XML declaration does not stop the scan") {
    // `<?xml version="1.0" encoding="UTF-8">` has no closing `?`, so every real
    // XML parser rejects the file. Handing it to one is the mistake this pins.
    const std::string path = temp_path("malformed.dxf");
    std::filesystem::remove(lock_path_for(path, ".dwl"));
    write_file(lock_path_for(path, ".dwl2"), kRealDwl2);

    CHECK(std::string(kRealDwl2).find("UTF-8\">") != std::string::npos);
    CHECK(std::string(kRealDwl2).find("UTF-8\"?>") == std::string::npos);
    CHECK(read_drawing_lock(path).owner == "sadie");

    std::filesystem::remove(lock_path_for(path, ".dwl2"));
}

TEST_CASE("lock: both files together read the same as either alone") {
    // AutoCAD writes both. Whichever survives, the answer must not change.
    const std::string path = temp_path("bothfiles.dxf");
    write_file(lock_path_for(path, ".dwl"), kRealDwl);
    write_file(lock_path_for(path, ".dwl2"), kRealDwl2);

    const DrawingLock lock = read_drawing_lock(path);
    CHECK(lock.owner == "sadie");
    CHECK(lock.machine == kMachine);
    // .dwl is the one named, because it is the one a user hunting a stale lock
    // will look for.
    CHECK(path_filename(lock.lock_path) == "bothfiles.dwl");

    std::filesystem::remove(lock_path_for(path, ".dwl"));
    std::filesystem::remove(lock_path_for(path, ".dwl2"));
}

TEST_CASE("lock: a .dwl names who holds it, and when") {
    const std::string path = temp_path("held.dxf");
    std::filesystem::remove(lock_path_for(path, ".dwl2"));
    write_file(lock_path_for(path, ".dwl"), "sadieforbes\r\n");

    const DrawingLock lock = read_drawing_lock(path);
    CHECK(lock.present);
    CHECK(lock.owner == "sadieforbes");
    // From the filesystem rather than parsed out of the file, so it is there
    // whatever the release wrote inside.
    CHECK(!lock.since.empty());

    const std::string said = describe_lock(lock);
    CHECK(said.find("sadieforbes") != std::string::npos);
    // Named, so the user can go and look at it -- or delete it, once they know
    // the session it belonged to is gone. We never clear it for them.
    CHECK(said.find("held.dwl") != std::string::npos);

    std::filesystem::remove(lock_path_for(path, ".dwl"));
}

TEST_CASE("lock: the machine is reported, not just the user") {
    // Over a shared folder this is the field that matters: "sadie" on this box
    // and "sadie" on another are very different situations, and the user name
    // alone cannot tell them apart.
    const std::string path = temp_path("whichbox.dxf");
    std::filesystem::remove(lock_path_for(path, ".dwl"));
    write_file(lock_path_for(path, ".dwl2"), kRealDwl2);

    const std::string said = describe_lock(read_drawing_lock(path));
    CHECK(said.find("by sadie") != std::string::npos);
    CHECK(said.find(std::string("on ") + kMachine) != std::string::npos);

    std::filesystem::remove(lock_path_for(path, ".dwl2"));
}

TEST_CASE("lock: a .dwl2 on its own is still a lock") {
    // Either file present means somebody has the drawing open. We may just be
    // able to say less about it.
    const std::string path = temp_path("dwl2only.dxf");
    std::filesystem::remove(lock_path_for(path, ".dwl"));
    write_file(lock_path_for(path, ".dwl2"), "someone\r\nTHEIRBOX\r\n");

    const DrawingLock lock = read_drawing_lock(path);
    CHECK(lock.present);
    CHECK(path_filename(lock.lock_path) == "dwl2only.dwl2");

    std::filesystem::remove(lock_path_for(path, ".dwl2"));
}

TEST_CASE("lock: an unreadable lock is still reported") {
    // The existence is the fact that matters. A lock whose contents we cannot
    // make sense of must not become no lock at all.
    //
    // "Unreadable" means CONTROL bytes, and only those. A high byte is not
    // garbage -- it is somebody's name, which is the whole lesson of 0x92 in a
    // real .dwl: an earlier version of this test asserted that 0xFF produced no
    // owner, and the fix that made "Sadie's MacBook Pro" come back correctly
    // rightly broke it.
    const std::string path = temp_path("binary.dxf");
    std::filesystem::remove(lock_path_for(path, ".dwl2"));
    write_file(lock_path_for(path, ".dwl"), std::string("\x00\x01\x02\x03", 4));

    const DrawingLock lock = read_drawing_lock(path);
    CHECK(lock.present);
    CHECK(lock.owner.empty());
    CHECK(!describe_lock(lock).empty());

    std::filesystem::remove(lock_path_for(path, ".dwl"));
}

TEST_CASE("lock: a high byte is a name, not garbage") {
    // The other half of the same point, stated positively.
    const std::string path = temp_path("latin1.dxf");
    std::filesystem::remove(lock_path_for(path, ".dwl2"));
    write_file(lock_path_for(path, ".dwl"), "bj\xf6rn\nDESKTOP\n");

    const DrawingLock lock = read_drawing_lock(path);
    // 0xF6 is Latin-1 o-umlaut in CP1252 too, so it round-trips to UTF-8.
    CHECK(lock.owner == "bj\xc3\xb6rn");

    std::filesystem::remove(lock_path_for(path, ".dwl"));
}

TEST_CASE("lock: an empty lock file is a lock with no name") {
    const std::string path = temp_path("empty.dxf");
    write_file(lock_path_for(path, ".dwl"), "");

    const DrawingLock lock = read_drawing_lock(path);
    CHECK(lock.present);
    CHECK(lock.owner.empty());

    std::filesystem::remove(lock_path_for(path, ".dwl"));
}

// --- the commands -------------------------------------------------------------

TEST_CASE("lock: OPEN reports the lock and reads the drawing anyway") {
    // Advisory, and reading somebody's drawing harms nothing. What the warning
    // buys is that it has already been given by the time a save comes round --
    // which is the case that can actually cost someone their work.
    const std::string path = fresh_drawing("openme.dxf");
    write_file(lock_path_for(path, ".dwl"), "otheruser\r\n");

    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("OPEN"));
    engine.supply(InputValue::of_string(path));
    CHECK(engine.status() == EngineStatus::Finished);

    // Read, not refused.
    CHECK(db.size() == 1);
    CHECK(engine.message().find("otheruser") != std::string::npos);

    std::filesystem::remove(lock_path_for(path, ".dwl"));
}

TEST_CASE("lock: SAVE over a locked file asks, and No leaves the file alone") {
    const std::string path = fresh_drawing("saveover.dxf");
    write_file(lock_path_for(path, ".dwl"), "otheruser\r\n");
    const std::size_t before = entity_count_of(path);

    Database db;
    for (int i = 0; i < 3; ++i) {
        db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{double(i), 1, 0}));
    }
    CommandEngine engine(db);
    engine.begin(make_command("SAVE"));
    engine.supply(InputValue::of_string(path));
    // The lock question stands here -- BEFORE the version prompt, so nothing
    // has been written by the time it is answered.
    engine.supply(InputValue::none());  // Enter means No

    CHECK(engine.status() == EngineStatus::Finished);
    // The whole point: their file is untouched.
    CHECK(entity_count_of(path) == before);

    std::filesystem::remove(lock_path_for(path, ".dwl"));
}

TEST_CASE("lock: SAVE over a locked file writes when told to") {
    // Advisory means overridable. A warning nobody can get past is a different
    // feature, and not the one that was asked for.
    const std::string path = fresh_drawing("saveyes.dxf");
    write_file(lock_path_for(path, ".dwl"), "otheruser\r\n");

    Database db;
    for (int i = 0; i < 3; ++i) {
        db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{double(i), 1, 0}));
    }
    CommandEngine engine(db);
    engine.begin(make_command("SAVE"));
    engine.supply(InputValue::of_string(path));
    engine.supply(InputValue::of_keyword("YES"));
    engine.supply(InputValue::none());  // the version prompt

    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(entity_count_of(path) == 3);

    std::filesystem::remove(lock_path_for(path, ".dwl"));
}

TEST_CASE("lock: QSAVE asks too, which is its one exception") {
    // QSAVE's rule is that it must not interrupt, and that rule is about
    // ROUTINE questions. A lock is not routine -- it is somebody else's unsaved
    // work about to be overwritten without a word.
    const std::string path = fresh_drawing("qsave.dxf");

    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("OPEN"));
    engine.supply(InputValue::of_string(path));
    REQUIRE(engine.status() == EngineStatus::Finished);

    // The lock appears between the open and the save, which is the only way
    // this can arise at all.
    write_file(lock_path_for(path, ".dwl"), "otheruser\r\n");
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{9, 9, 0}));

    engine.begin(make_command("QSAVE"));
    // Suspended rather than finished: it asked.
    CHECK(engine.status() == EngineStatus::Waiting);
    engine.supply(InputValue::none());  // No
    CHECK(entity_count_of(path) == 1);

    std::filesystem::remove(lock_path_for(path, ".dwl"));
}

TEST_CASE("lock: QSAVE with no lock still says nothing") {
    // The exception is narrow on purpose. With no lock, QSAVE is exactly what
    // it was: one step, no prompts, not even the version.
    const std::string path = fresh_drawing("qsaveclean.dxf");

    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("OPEN"));
    engine.supply(InputValue::of_string(path));
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{9, 9, 0}));

    engine.begin(make_command("QSAVE"));
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(entity_count_of(path) == 2);
}

TEST_CASE("lock: DXFOUT over a locked name asks as well") {
    // An export over a file somebody has open is the same clobber, whatever
    // the command that did it calls itself.
    const std::string path = fresh_drawing("exportto.dxf");
    write_file(lock_path_for(path, ".dwl"), "otheruser\r\n");
    const std::size_t before = entity_count_of(path);

    Database db;
    for (int i = 0; i < 4; ++i) {
        db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{double(i), 1, 0}));
    }
    CommandEngine engine(db);
    engine.begin(make_command("DXFOUT"));
    engine.supply(InputValue::of_string(path));
    engine.supply(InputValue::none());  // No

    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(entity_count_of(path) == before);

    std::filesystem::remove(lock_path_for(path, ".dwl"));
}

TEST_CASE("lock: saving a drawing nobody holds is not interrupted") {
    // The regression that would matter most: a lock check that fires when there
    // is no lock turns every save into a question.
    const std::string path = temp_path("nolock.dxf");
    std::filesystem::remove(path);
    std::filesystem::remove(lock_path_for(path, ".dwl"));
    std::filesystem::remove(lock_path_for(path, ".dwl2"));

    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 1, 0}));
    CommandEngine engine(db);
    engine.begin(make_command("SAVE"));
    engine.supply(InputValue::of_string(path));
    engine.supply(InputValue::none());  // straight to the version prompt

    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(std::filesystem::exists(path));
}
