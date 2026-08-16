// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/drawing_lock.hpp"

#include "cp1252.hpp"
#include "ncad/host.hpp"

#include <cerrno>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ncad {
namespace {

// --- the platform seam --------------------------------------------------------
//
// THE WHOLE PLATFORM SURFACE OF THIS FEATURE, and it is deliberately five small
// functions in one place rather than #ifdefs sprinkled through the logic. A
// Windows port lands here and nowhere above it:
//
//   create_new -> CreateFileW(..., CREATE_NEW, dwShareMode = FILE_SHARE_READ)
//
// CREATE_NEW is exactly O_EXCL, and the share mode gives Windows' mandatory half
// for free -- which is the reason the descriptor is HELD rather than closed once
// the bytes are written. Unix cannot offer that half at all, and this file does
// not pretend otherwise.

constexpr long kNoHandle = -1;

// O_CREAT|O_EXCL: the create IS the check, in one syscall. Anything built out of
// stat-then-create has a window between them, and that window is the whole race
// this exists to close.
bool create_new(const std::string& path, long& out, int& err) {
#if defined(_WIN32)
    (void)path;
    (void)out;
    err = ENOSYS;
    return false;
#else
    const int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd < 0) {
        err = errno;
        return false;
    }
    out = fd;
    err = 0;
    return true;
#endif
}

bool create_or_replace(const std::string& path, long& out) {
#if defined(_WIN32)
    (void)path;
    (void)out;
    return false;
#else
    const int fd = ::open(path.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0666);
    if (fd < 0) return false;
    out = fd;
    return true;
#endif
}

bool write_all(long handle, const std::string& bytes) {
#if defined(_WIN32)
    (void)handle;
    (void)bytes;
    return false;
#else
    const char* p = bytes.data();
    std::size_t left = bytes.size();
    while (left > 0) {
        const ssize_t n = ::write(static_cast<int>(handle), p, left);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += n;
        left -= static_cast<std::size_t>(n);
    }
    return true;
#endif
}

void close_handle(long handle) {
    if (handle == kNoHandle) return;
#if !defined(_WIN32)
    ::close(static_cast<int>(handle));
#endif
}

void unlink_quiet(const std::string& path) {
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// --- the payloads -------------------------------------------------------------

// Anything below 0x20 would fabricate a line in `.dwl` or break a tag in
// `.dwl2`. A name should never contain one; a name that does must not be able to
// forge a lock file.
std::string sanitised(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (static_cast<unsigned char>(c) >= 0x20) out += c;
    }
    return out;
}

// `<`, `>` and `&` in a user or machine name would corrupt the tag they sit in.
// Escaped on the way out; paths.cpp's tag_value unescapes the same three.
std::string escaped(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else out += c;
    }
    return out;
}

// The long localised form AutoCAD writes, e.g.
// "Saturday, August 16, 2026  09:34:12 EDT".
//
// Not one strftime call: %d zero-pads where AutoCAD writes a bare day, so the
// day is spliced in. No setlocale -- the C locale gives the English month and
// weekday names AutoCAD wrote, and asking for the user's locale would produce a
// file that differs by machine.
//
// TWO SPACES before the time. That is measured, not a typo.
//
// %Z gives the abbreviated zone ("EDT") where AutoCAD spells it out ("Eastern
// Daylight Time"). A known cosmetic divergence, harmless because our own reader
// takes `since` from the filesystem mtime and never parses this line; settle it
// against WHOHAS if it turns out to matter.
std::string lock_datetime() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif

    char head[64];
    char tail[64];
    if (std::strftime(head, sizeof(head), "%A, %B ", &tm) == 0) return {};
    if (std::strftime(tail, sizeof(tail), ", %Y  %H:%M:%S %Z", &tm) == 0) return {};
    return std::string(head) + std::to_string(tm.tm_mday) + tail;
}

// Three CP1252 lines: user, machine, datetime. A TRAILING SPACE after the
// machine name and NO trailing newline on the file -- both measured from a pair
// AutoCAD wrote, and both pinned by tests.
std::string dwl_payload(const std::string& user, const std::string& machine,
                        const std::string& when) {
    const std::string utf8 = sanitised(user) + "\n" + sanitised(machine) + " \n" + when;
    return text::from_utf8(utf8);
}

// UTF-8, and the declaration is MALFORMED ON PURPOSE: AutoCAD writes
// `<?xml version="1.0" encoding="UTF-8">` with no closing `?`, so every real
// parser rejects the file. Do not "fix" it. Matching the bytes is the entire
// point of the exercise, our own reader is a scanner rather than a parser, and
// nothing says AutoCAD would accept a well-formed one.
//
// <fullname> is empty because the measured pair had it empty, and we have
// nothing honest to put there -- user_name() is the login name.
std::string dwl2_payload(const std::string& user, const std::string& machine,
                         const std::string& when) {
    std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\">\n";
    s += "<whprops>\n";
    s += "<username>" + escaped(sanitised(user)) + "</username>\n";
    s += "<machinename>" + escaped(sanitised(machine)) + " </machinename>\n";
    s += "<fullname></fullname>\n";
    s += "<datetime>" + escaped(when) + "</datetime>\n";
    s += "</whprops>";
    return s;
}

std::string read_whole(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

// --- DrawingLockFile ----------------------------------------------------------

DrawingLockFile::~DrawingLockFile() { release(); }

DrawingLockFile::DrawingLockFile(DrawingLockFile&& other) noexcept { *this = std::move(other); }

DrawingLockFile& DrawingLockFile::operator=(DrawingLockFile&& other) noexcept {
    if (this == &other) return *this;
    release();
    held_ = other.held_;
    drawing_ = std::move(other.drawing_);
    dwl_path_ = std::move(other.dwl_path_);
    dwl2_path_ = std::move(other.dwl2_path_);
    dwl_bytes_ = std::move(other.dwl_bytes_);
    dwl_fd_ = other.dwl_fd_;
    dwl2_fd_ = other.dwl2_fd_;

    other.held_ = false;
    other.dwl_fd_ = kNoHandle;
    other.dwl2_fd_ = kNoHandle;
    other.drawing_.clear();
    other.dwl_path_.clear();
    other.dwl2_path_.clear();
    other.dwl_bytes_.clear();
    return *this;
}

void DrawingLockFile::release() {
    if (!held_) return;
    close_handle(dwl_fd_);
    close_handle(dwl2_fd_);
    dwl_fd_ = kNoHandle;
    dwl2_fd_ = kNoHandle;
    unlink_quiet(dwl_path_);
    unlink_quiet(dwl2_path_);
    held_ = false;
}

bool DrawingLockFile::still_ours() const {
    if (!held_) return false;
    return read_whole(dwl_path_) == dwl_bytes_;
}

LockResult acquire_drawing_lock(const std::string& drawing, DrawingLockFile& out,
                                DrawingLock& existing) {
    out.release();
    if (drawing.empty()) return LockResult::Unavailable;

    const std::string full = normalised_path(drawing);
    const std::string dwl = lock_path_for(full, ".dwl");
    const std::string dwl2 = lock_path_for(full, ".dwl2");
    if (dwl.empty()) return LockResult::Unavailable;

    long dwl_fd = kNoHandle;
    int err = 0;
    if (!create_new(dwl, dwl_fd, err)) {
        if (err == EEXIST) {
            // Somebody has it. The only errno that means that.
            existing = read_drawing_lock(full);
            return LockResult::HeldByAnother;
        }
        // Everything else -- EROFS, EACCES, ENOSPC, a share that will not take a
        // sibling -- is the LOCK failing, not the drawing being held. The caller
        // writes anyway. A full disk must not cost somebody their drawing.
        return LockResult::Unavailable;
    }

    // An orphaned `.dwl2` beside a `.dwl` we just won is adopted rather than
    // agonised over: we hold the file that decides, and this is SF_todo's
    // "overwrite, do not agonise" at the one place it can bite.
    long dwl2_fd = kNoHandle;
    if (!create_or_replace(dwl2, dwl2_fd)) {
        close_handle(dwl_fd);
        unlink_quiet(dwl);
        return LockResult::Unavailable;
    }

    const std::string user = user_name();
    const std::string machine = host_name();
    const std::string when = lock_datetime();
    const std::string dwl_bytes = dwl_payload(user, machine, when);

    if (!write_all(dwl_fd, dwl_bytes) || !write_all(dwl2_fd, dwl2_payload(user, machine, when))) {
        // Never leave a half-lock: a `.dwl` with no content still blocks, and it
        // would block on nothing anybody can read.
        close_handle(dwl_fd);
        close_handle(dwl2_fd);
        unlink_quiet(dwl);
        unlink_quiet(dwl2);
        return LockResult::Unavailable;
    }

    // Deliberately NOT hidden, on any platform. SF_todo specifies UF_HIDDEN on
    // macOS; Sadie's call is that a lock you can see is a lock you can debug
    // while the feature is new. The filename stays exactly `<stem>.dwl` in any
    // case -- that name is the whole interoperation channel, so no dot-prefixing
    // ever, and a test pins it.

    out.held_ = true;
    out.drawing_ = full;
    out.dwl_path_ = dwl;
    out.dwl2_path_ = dwl2;
    out.dwl_bytes_ = dwl_bytes;
    out.dwl_fd_ = dwl_fd;
    out.dwl2_fd_ = dwl2_fd;
    return LockResult::Taken;
}

// --- DrawingLockHolder --------------------------------------------------------

LockResult DrawingLockHolder::take(const std::string& drawing, DrawingLock& existing) {
    if (holds(drawing)) return LockResult::AlreadyHeldByUs;
    return acquire_drawing_lock(drawing, lock_, existing);
}

void DrawingLockHolder::release() { lock_.release(); }

bool DrawingLockHolder::holds(const std::string& path) const {
    if (!lock_.held() || path.empty()) return false;
    if (!same_file(lock_.drawing(), normalised_path(path))) return false;
    // Held, and still holding what we put there. Something that replaced the
    // file took the lock from us whether it meant to or not.
    return lock_.still_ours();
}

// --- ScopedDrawingLock --------------------------------------------------------

ScopedDrawingLock::ScopedDrawingLock(const std::string& path, const DrawingLockHolder* session) {
    if (session != nullptr && session->holds(path)) {
        // Our own session drawing. Taking a second lock on it would fail against
        // ourselves and report the user as their own blocker.
        result_ = LockResult::AlreadyHeldByUs;
        return;
    }
    result_ = acquire_drawing_lock(path, lock_, existing_);
}

}  // namespace ncad
