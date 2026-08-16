// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Taking a drawing lock, as against paths.hpp's reading of one.
//
// MEASURED, not assumed: `lsof` against AutoCAD 2026 on macOS with drawings open
// shows every descriptor as plain `u` -- open read/write with NO lock character.
// AutoCAD takes no flock and no fcntl lock. It does hold both `.dwl` and `.dwl2`
// open for the whole session, and it writes them for DXF-opened drawings too.
//
// So on Unix these files are not a fallback for OS locking, they ARE the
// mechanism, and there is no OS lock to interoperate with even if we wanted one.
// Mandatory locking is not an option either: Linux removed it in 5.15 and the
// BSDs never had it. Windows is the exception, and the seam below is shaped so
// its share modes drop in without changing anything above this header.
//
// What this can and cannot promise, stated plainly because the difference
// matters:
//
//   - ncad against ncad: real exclusion. O_CREAT|O_EXCL makes check-and-take one
//     syscall, so two sessions cannot both believe they hold a drawing.
//   - ncad against AutoCAD: best effort. We will SEE AutoCAD's lock, which is
//     what the read side already does. Whether AutoCAD sees ours is unsettled --
//     a hand-written `.dwl` was ignored, and the untested hypothesis is that its
//     check keys on `.dwl2`, which that experiment did not provide. Writing both
//     is the case still to try. WHOHAS is the test that matters.
//
// The lock is ADVISORY throughout. Nothing here makes anything impossible; it
// makes a clobber something you were warned about.
#pragma once

#include "ncad/paths.hpp"

#include <string>

namespace ncad {

// Why a lock could not be taken.
//
// The distinction that matters is `HeldByAnother` against everything else.
// EEXIST is the ONLY errno that means somebody has the drawing. A read-only
// directory, a full disk, an NFS export that will not take a sibling file --
// none of those are locks, and none of them may stop a save. A lock that can
// cost you a drawing is worse than no lock.
enum class LockResult {
    Taken,
    AlreadyHeldByUs,
    HeldByAnother,
    Unavailable,
};

// One acquired `.dwl`/`.dwl2` pair, held open for as long as this object lives.
//
// Move-only and RAII: release is destruction, so no path out of a scope forgets
// it. The descriptors stay OPEN rather than being closed after the write, which
// matches what AutoCAD does and is load-bearing on Windows, where the share mode
// exists only while a handle does.
class DrawingLockFile {
public:
    DrawingLockFile() = default;
    ~DrawingLockFile();

    DrawingLockFile(DrawingLockFile&& other) noexcept;
    DrawingLockFile& operator=(DrawingLockFile&& other) noexcept;
    DrawingLockFile(const DrawingLockFile&) = delete;
    DrawingLockFile& operator=(const DrawingLockFile&) = delete;

    bool held() const { return held_; }

    // The drawing this locks, normalised.
    const std::string& drawing() const { return drawing_; }
    const std::string& dwl_path() const { return dwl_path_; }

    // Deletes both files and closes both descriptors. Idempotent.
    void release();

    // Whether the `.dwl` on disk still holds the bytes we wrote. False when
    // something replaced it underneath us, which means the lock is no longer
    // ours to reason about.
    bool still_ours() const;

private:
    friend LockResult acquire_drawing_lock(const std::string&, DrawingLockFile&, DrawingLock&);

    bool held_{false};
    std::string drawing_;
    std::string dwl_path_;
    std::string dwl2_path_;
    // Exactly what was written, so still_ours() can compare rather than trust.
    std::string dwl_bytes_;
    // int on POSIX; an intptr_t holding a HANDLE on Windows.
    long dwl_fd_{-1};
    long dwl2_fd_{-1};
};

// One atomic take.
//
// `drawing` is normalised internally, so callers need not agree on spelling.
// On HeldByAnother, `existing` is filled by read_drawing_lock so the caller can
// describe who has it.
LockResult acquire_drawing_lock(const std::string& drawing, DrawingLockFile& out,
                                DrawingLock& existing);

// The session's lock: at most one, on the drawing currently open.
//
// Owned by CommandEngine beside CommandMemory, for the same stated reason --
// state that spans commands belongs to whatever spans commands. AutoCAD holds
// from open to close and so do we.
class DrawingLockHolder {
public:
    // Releases any previous lock first: a session holds one drawing.
    LockResult take(const std::string& drawing, DrawingLock& existing);
    void release();

    bool holds_any() const { return lock_.held(); }
    const std::string& drawing() const { return lock_.drawing(); }

    // Whether THIS session holds `path`.
    //
    // Compared with std::filesystem::equivalent rather than by string, so a
    // SAVEAS spelled `./plan.dxf` is recognised as the `~/plan.dxf` we opened.
    // Also requires the file on disk to still hold our bytes: a lock replaced
    // underneath us is not ours, and QSAVE's silence depends on knowing that.
    bool holds(const std::string& path) const;

private:
    DrawingLockFile lock_;
};

// A momentary lock around a write to a path that is not the session drawing --
// DXFOUT to some other name, and eventually WBLOCK.
//
// Not to advertise a session, since there is no session for that file, but to
// make check-and-take one syscall and give the warning something authoritative
// to report. A no-op when `session` already holds this path, so writing the
// drawing's own name does not block on our own lock.
class ScopedDrawingLock {
public:
    ScopedDrawingLock(const std::string& path, const DrawingLockHolder* session);
    ~ScopedDrawingLock() = default;

    ScopedDrawingLock(const ScopedDrawingLock&) = delete;
    ScopedDrawingLock& operator=(const ScopedDrawingLock&) = delete;

    LockResult result() const { return result_; }
    const DrawingLock& existing() const { return existing_; }

private:
    DrawingLockFile lock_;
    LockResult result_{LockResult::Unavailable};
    DrawingLock existing_;
};

}  // namespace ncad
