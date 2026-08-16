// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Taking a drawing lock, as against test_lock.cpp's reading of one.
//
// Two halves worth pinning, and they fail in different ways. The FORMAT half is
// about interoperation: AutoCAD's own reader is the thing we cannot test here,
// so what these assert is that we emit exactly the bytes that were measured --
// the trailing space, the missing newline, the CP1252 apostrophe and the
// deliberately malformed XML declaration. A "tidy-up" of any of those is silent
// until WHOHAS says nothing.
//
// The SEMANTIC half is about not making things worse. The lock is advisory, so
// the worst outcome is not a missed warning -- it is a lock that costs somebody
// a drawing. Hence the test that a lock which CANNOT be taken still lets the
// save through.

#include "test.hpp"

#include "../src/core/cp1252.hpp"
#include "ncad/command.hpp"
#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/drawing_lock.hpp"
#include "ncad/entities.hpp"
#include "ncad/host.hpp"
#include "ncad/paths.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

using namespace ncad;

namespace {

std::string temp_dir() {
    const std::filesystem::path p =
        std::filesystem::temp_directory_path() / "ncad_lock_write_tests";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p.string();
}

std::string temp_path(const char* leaf) { return temp_dir() + "/" + leaf; }

// A drawing path with nothing beside it. Not a saved file: acquisition never
// reads the drawing, only its siblings.
std::string clean_target(const char* leaf) {
    const std::string path = temp_path(leaf);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(lock_path_for(path, ".dwl"), ec);
    std::filesystem::remove(lock_path_for(path, ".dwl2"), ec);
    return path;
}

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_file(const std::string& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

}  // namespace

// --- the encoding, both ways --------------------------------------------------

TEST_CASE("lock write: CP1252 round trips through UTF-8 and back") {
    // Every byte a `.dwl` can carry. 0x80..0x9F is where CP1252 differs from
    // Latin-1 and is the whole reason this conversion exists; five of those
    // slots are unassigned and cannot round trip, which is itself the contract.
    for (int b = 0x20; b <= 0xFF; ++b) {
        const std::string one(1, static_cast<char>(b));
        const std::string back = text::from_utf8(text::to_utf8(one));
        if (b >= 0x80 && b < 0xA0) {
            // The five unassigned ones become '?', deliberately -- see from_utf8.
            const bool assigned = back == one;
            const bool replaced = back == "?";
            CHECK(assigned || replaced);
        } else {
            CHECK(back == one);
        }
    }
}

TEST_CASE("lock write: the curly apostrophe comes back as the single byte 0x92") {
    // The character that actually turns up: macOS names a machine "Sadie's
    // MacBook Pro". Getting this wrong writes three bytes where AutoCAD writes
    // one, and the file is no longer what AutoCAD wrote.
    const std::string utf8 = "Sadie\xe2\x80\x99s MacBook Pro";
    const std::string cp = text::from_utf8(utf8);
    CHECK(cp == "Sadie\x92s MacBook Pro");
    CHECK(text::to_utf8(cp) == utf8);
}

TEST_CASE("lock write: a name with no CP1252 spelling becomes a question mark") {
    // Not a crash, not a mangled multi-byte sequence, and not a silently
    // dropped character.
    CHECK(text::from_utf8("\xe6\x97\xa5") == "?");
}

// --- the byte formats ---------------------------------------------------------

TEST_CASE("lock write: what we write, our own reader reads back") {
    const std::string drawing = clean_target("roundtrip.dxf");
    DrawingLockFile lock;
    DrawingLock existing;
    CHECK(acquire_drawing_lock(drawing, lock, existing) == LockResult::Taken);

    const DrawingLock back = read_drawing_lock(drawing);
    CHECK(back.present);
    CHECK(back.owner == user_name());
    CHECK(back.machine == host_name());
    CHECK(!back.since.empty());
}

TEST_CASE("lock write: .dwl is three lines, no trailing newline, trailing space on line two") {
    const std::string drawing = clean_target("shape.dxf");
    DrawingLockFile lock;
    DrawingLock existing;
    CHECK(acquire_drawing_lock(drawing, lock, existing) == LockResult::Taken);

    const std::string dwl = slurp(lock_path_for(drawing, ".dwl"));
    REQUIRE(!dwl.empty());
    CHECK(dwl.back() != '\n');

    // Exactly two separators, so exactly three lines.
    std::size_t newlines = 0;
    for (const char c : dwl) {
        if (c == '\n') ++newlines;
    }
    CHECK(newlines == 2);

    // The measured trailing space after the machine name.
    const std::size_t first = dwl.find('\n');
    const std::size_t second = dwl.find('\n', first + 1);
    REQUIRE(second != std::string::npos);
    CHECK(dwl[second - 1] == ' ');
}

TEST_CASE("lock write: .dwl2 keeps the malformed declaration and ends without a newline") {
    const std::string drawing = clean_target("decl.dxf");
    DrawingLockFile lock;
    DrawingLock existing;
    CHECK(acquire_drawing_lock(drawing, lock, existing) == LockResult::Taken);

    const std::string dwl2 = slurp(lock_path_for(drawing, ".dwl2"));
    REQUIRE(!dwl2.empty());

    // No closing `?`, exactly as AutoCAD writes it. A well-formed declaration
    // here would be a "fix" that nothing asked for and nobody could verify.
    CHECK(dwl2.rfind("<?xml version=\"1.0\" encoding=\"UTF-8\">", 0) == 0);
    CHECK(dwl2.find("UTF-8\"?>") == std::string::npos);

    CHECK(dwl2.find("</whprops>") != std::string::npos);
    CHECK(dwl2.back() != '\n');
    CHECK(dwl2.substr(dwl2.size() - 10) == "</whprops>");
}

TEST_CASE("lock write: markup in a name cannot forge a tag") {
    // A machine called "R&D <box>" is legal and would otherwise write a `.dwl2`
    // whose tags do not mean what they say. Escaped out, unescaped back.
    const std::string awkward = "R&D <box>";
    const std::string xml =
        "<whprops>\n<username>R&amp;D &lt;box&gt;</username>\n</whprops>";
    const std::string path = temp_path("escape.dwl2");
    write_file(path, xml);

    const std::string drawing = temp_path("escape.dxf");
    const DrawingLock lock = read_drawing_lock(drawing);
    CHECK(lock.present);
    CHECK(lock.owner == awkward);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// --- semantics ----------------------------------------------------------------

TEST_CASE("lock write: a second holder is refused and told who has it") {
    const std::string drawing = clean_target("contended.dxf");

    DrawingLockHolder first;
    DrawingLock existing;
    CHECK(first.take(drawing, existing) == LockResult::Taken);

    DrawingLockHolder second;
    DrawingLock seen;
    CHECK(second.take(drawing, seen) == LockResult::HeldByAnother);
    CHECK(seen.present);
    CHECK(seen.owner == user_name());
    CHECK(!describe_lock(seen).empty());
}

TEST_CASE("lock write: taking the same drawing twice from one holder is not a collision") {
    // The self-lock trap. O_CREAT|O_EXCL makes check-and-take one syscall, which
    // is exactly why the second take would fail against our own file and report
    // the user as their own blocker.
    const std::string drawing = clean_target("twice.dxf");

    DrawingLockHolder holder;
    DrawingLock existing;
    CHECK(holder.take(drawing, existing) == LockResult::Taken);
    CHECK(holder.take(drawing, existing) == LockResult::AlreadyHeldByUs);
}

TEST_CASE("lock write: holds() compares files, not spellings") {
    const std::string drawing = clean_target("spelled.dxf");
    DrawingLockHolder holder;
    DrawingLock existing;
    CHECK(holder.take(drawing, existing) == LockResult::Taken);

    CHECK(holder.holds(drawing));
    CHECK(holder.holds(temp_dir() + "/./spelled.dxf"));
    CHECK(holder.holds(temp_dir() + "/../ncad_lock_write_tests/spelled.dxf"));
    CHECK(!holder.holds(temp_path("other.dxf")));
}

TEST_CASE("lock write: a lock replaced underneath us is no longer ours") {
    // What licenses QSAVE's silence: "we hold it" has to mean the file still
    // says so, not merely that we once created it.
    const std::string drawing = clean_target("stolen.dxf");
    DrawingLockHolder holder;
    DrawingLock existing;
    CHECK(holder.take(drawing, existing) == LockResult::Taken);
    CHECK(holder.holds(drawing));

    write_file(lock_path_for(drawing, ".dwl"), "someone\nelsewhere \nwhenever");
    CHECK(!holder.holds(drawing));
}

TEST_CASE("lock write: a foreign lock blocks acquisition and is not touched") {
    const std::string drawing = clean_target("foreign.dxf");
    const std::string dwl = lock_path_for(drawing, ".dwl");
    const std::string contents = "otheruser\nOther Machine \nwhenever";
    write_file(dwl, contents);

    DrawingLockFile lock;
    DrawingLock existing;
    CHECK(acquire_drawing_lock(drawing, lock, existing) == LockResult::HeldByAnother);
    CHECK(existing.owner == "otheruser");
    // Not truncated by our failed O_EXCL open, which is the bug O_TRUNC here
    // would have introduced.
    CHECK(slurp(dwl) == contents);

    std::error_code ec;
    std::filesystem::remove(dwl, ec);
}

TEST_CASE("lock write: release deletes both files and is idempotent") {
    const std::string drawing = clean_target("released.dxf");
    DrawingLockFile lock;
    DrawingLock existing;
    CHECK(acquire_drawing_lock(drawing, lock, existing) == LockResult::Taken);
    CHECK(path_exists(lock_path_for(drawing, ".dwl")));
    CHECK(path_exists(lock_path_for(drawing, ".dwl2")));

    lock.release();
    CHECK(!path_exists(lock_path_for(drawing, ".dwl")));
    CHECK(!path_exists(lock_path_for(drawing, ".dwl2")));
    lock.release();  // must not throw, must not resurrect anything
    CHECK(!lock.held());
}

TEST_CASE("lock write: destruction releases") {
    const std::string drawing = clean_target("scoped.dxf");
    {
        DrawingLockFile lock;
        DrawingLock existing;
        CHECK(acquire_drawing_lock(drawing, lock, existing) == LockResult::Taken);
        CHECK(path_exists(lock_path_for(drawing, ".dwl")));
    }
    CHECK(!path_exists(lock_path_for(drawing, ".dwl")));
}

TEST_CASE("lock write: an orphaned .dwl2 is adopted, not treated as a blocker") {
    // A `.dwl2` with no `.dwl` beside it is the wreckage of something that died.
    // The `.dwl` is the file that decides, and we won it.
    const std::string drawing = clean_target("orphan.dxf");
    write_file(lock_path_for(drawing, ".dwl2"), "<whprops><username>ghost</username></whprops>");

    DrawingLockFile lock;
    DrawingLock existing;
    CHECK(acquire_drawing_lock(drawing, lock, existing) == LockResult::Taken);
    CHECK(read_drawing_lock(drawing).owner == user_name());
}

TEST_CASE("lock write: the file name is never dot-prefixed") {
    // The invariant a future "let's hide these" patch would break. The name is
    // the entire interoperation channel: `.plan.dwl` is invisible to AutoCAD's
    // WHOHAS and to our own reader, which would disable the feature silently.
    const std::string drawing = clean_target("visible.dxf");
    DrawingLockFile lock;
    DrawingLock existing;
    CHECK(acquire_drawing_lock(drawing, lock, existing) == LockResult::Taken);

    CHECK(path_filename(lock.dwl_path()) == "visible.dwl");
    CHECK(path_filename(lock.dwl_path())[0] != '.');
}

// --- the one that matters most ------------------------------------------------

TEST_CASE("lock write: a lock that cannot be taken does not stop the save") {
    // THE regression to guard. Acquisition fails for plenty of reasons that are
    // not locks -- a read-only directory, a full disk, a share that will not
    // accept a sibling file -- and none of them may cost somebody their drawing.
    // A lock that can lose work is worse than no lock at all.
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ncad_lock_readonly";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    const std::string drawing = (dir / "held.dxf").string();

    // Scoped, so the writing session's own lock is released before the probe
    // below -- otherwise the honest answer really is HeldByAnother, by us.
    {
        Database db;
        db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 10, 0}));
        CommandEngine engine(db);
        engine.begin(make_command("SAVE"));
        engine.supply(InputValue::of_string(drawing));
        engine.supply(InputValue::none());
    }
    REQUIRE(path_exists(drawing));
    REQUIRE(!path_exists(lock_path_for(drawing, ".dwl")));

    // Now make the sibling impossible to create. Nobody holds the drawing; the
    // directory simply will not accept the file.
    std::filesystem::permissions(dir, std::filesystem::perms::owner_read |
                                          std::filesystem::perms::owner_exec,
                                 ec);

    // Root ignores directory permissions, so the setup cannot be made to fail
    // there -- skip rather than assert something untrue.
    const bool refused = !std::ofstream(lock_path_for(drawing, ".dwl")).good();
    if (!ec && refused) {
        DrawingLockHolder probe;
        DrawingLock existing;
        CHECK(probe.take(drawing, existing) == LockResult::Unavailable);

        // And the save still goes through, which is the whole point.
        Database db2;
        db2.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 1, 0}));
        db2.add(std::make_unique<Line>(Vec3{1, 1, 0}, Vec3{2, 2, 0}));
        CommandEngine engine2(db2);
        engine2.begin(make_command("SAVE"));
        engine2.supply(InputValue::of_string(drawing));
        engine2.supply(InputValue::none());
        CHECK(engine2.status() == EngineStatus::Finished);
    }

    std::filesystem::permissions(dir, std::filesystem::perms::owner_all, ec);
    std::filesystem::remove_all(dir, ec);
}

// --- through the engine -------------------------------------------------------

TEST_CASE("lock write: OPEN takes the lock and NEW gives it back") {
    const std::string drawing = clean_target("session.dxf");

    Database seed;
    seed.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{5, 5, 0}));
    {
        CommandEngine writer(seed);
        writer.begin(make_command("SAVE"));
        writer.supply(InputValue::of_string(drawing));
        writer.supply(InputValue::none());
    }
    // The writing session released on destruction.
    CHECK(!path_exists(lock_path_for(drawing, ".dwl")));

    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("OPEN"));
    engine.supply(InputValue::of_string(drawing));
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(path_exists(lock_path_for(drawing, ".dwl")));
    CHECK(engine.locks().holds(drawing));

    engine.begin(make_command("NEW"));
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(!path_exists(lock_path_for(drawing, ".dwl")));

    std::error_code ec;
    std::filesystem::remove(drawing, ec);
}

TEST_CASE("lock write: QSAVE stays silent over a lock this session holds") {
    // The narrowing. Our own lock is proof nobody took the drawing since we
    // opened it, so the question does not arise -- but only because we can tell
    // that the lock is ours.
    const std::string drawing = clean_target("quiet.dxf");

    Database seed;
    seed.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{5, 5, 0}));
    {
        CommandEngine writer(seed);
        writer.begin(make_command("SAVE"));
        writer.supply(InputValue::of_string(drawing));
        writer.supply(InputValue::none());
    }

    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("OPEN"));
    engine.supply(InputValue::of_string(drawing));
    REQUIRE(engine.locks().holds(drawing));

    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 1, 0}));
    engine.begin(make_command("QSAVE"));
    // Not Waiting: no question was asked about a lock we hold ourselves.
    CHECK(engine.status() == EngineStatus::Finished);

    std::error_code ec;
    std::filesystem::remove(drawing, ec);
    std::filesystem::remove(lock_path_for(drawing, ".dwl"), ec);
    std::filesystem::remove(lock_path_for(drawing, ".dwl2"), ec);
}

TEST_CASE("lock write: SAVEAS moves the lock to the new file") {
    const std::string first = clean_target("branch_from.dxf");
    const std::string second = clean_target("branch_to.dxf");

    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{5, 5, 0}));
    CommandEngine engine(db);

    engine.begin(make_command("SAVE"));
    engine.supply(InputValue::of_string(first));
    engine.supply(InputValue::none());
    CHECK(engine.locks().holds(first));

    engine.begin(make_command("SAVEAS"));
    engine.supply(InputValue::of_string(second));
    engine.supply(InputValue::none());

    // The drawing IS the new file now, so the old one is handed back.
    CHECK(engine.locks().holds(second));
    CHECK(!engine.locks().holds(first));
    CHECK(!path_exists(lock_path_for(first, ".dwl")));

    std::error_code ec;
    std::filesystem::remove(first, ec);
    std::filesystem::remove(second, ec);
    std::filesystem::remove(lock_path_for(second, ".dwl"), ec);
    std::filesystem::remove(lock_path_for(second, ".dwl2"), ec);
}
