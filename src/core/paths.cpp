// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/paths.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace ncad {
namespace {

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool ends_with_ci(const std::string& s, const std::string& tail) {
    if (tail.size() > s.size()) return false;
    for (std::size_t i = 0; i < tail.size(); ++i) {
        if (lower(s[s.size() - tail.size() + i]) != lower(tail[i])) return false;
    }
    return true;
}

}  // namespace

std::string expand_user_path(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;

    // `~user/...` is deliberately not handled -- see the header. Anything after
    // the tilde that is not a separator means this is one of those.
    if (path.size() > 1 && path[1] != '/') return path;

    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') return path;

    std::string out = home;
    // "~" alone is the home directory; "~/x" appends the rest, and the home
    // value may or may not already end in a separator.
    if (path.size() > 1) {
        if (!out.empty() && out.back() == '/') out.pop_back();
        out += path.substr(1);
    }
    return out;
}

std::string ensure_extension(const std::string& path, const std::string& ext) {
    if (path.empty() || ext.empty()) return path;
    if (ends_with_ci(path, ext)) return path;

    // Only a name with NO extension gets one added. A deliberate "notes.bak"
    // should stay that, and turning it into "notes.bak.dxf" would be the tool
    // arguing with the user about their own filename.
    const std::filesystem::path p(path);
    if (p.has_extension()) return path;

    return path + ext;
}

bool path_exists(const std::string& path) {
    std::error_code ec;  // the throwing overload is not wanted here
    return std::filesystem::exists(path, ec);
}

bool same_file(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    if (a == b) return true;

    std::error_code ec;
    // weakly_canonical rather than canonical: the target of a SAVEAS usually
    // does not exist yet, and canonical would simply fail on it.
    const std::filesystem::path ca = std::filesystem::weakly_canonical(a, ec);
    if (ec) return false;
    const std::filesystem::path cb = std::filesystem::weakly_canonical(b, ec);
    if (ec) return false;
    return ca == cb;
}

std::string normalised_path(const std::string& path) {
    std::error_code ec;
    const std::filesystem::path p = std::filesystem::weakly_canonical(path, ec);
    return ec ? path : p.string();
}

std::string path_directory(const std::string& path) {
    const std::filesystem::path p(path);
    if (!p.has_parent_path()) return {};
    std::string dir = p.parent_path().string();
    if (!dir.empty() && dir.back() != '/') dir += '/';
    return dir;
}

std::string path_filename(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

// --- drawing locks ------------------------------------------------------------

namespace {

// The first line of a lock file, with control characters and surrounding space
// removed.
//
// Defensive on purpose. `.dwl` is plain text holding a user name and that much
// is well established; what any given AutoCAD release puts after it is not, and
// neither is `.dwl2`'s layout. So this takes the first legible line and stops,
// rather than pretending to a parse it cannot justify -- a lock reported with
// no name is still a lock reported.
std::string first_text_line(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};

    std::string line;
    if (!std::getline(in, line)) return {};

    std::string out;
    for (const char c : line) {
        // Printable ASCII only. A UTF-16 `.dwl2` would otherwise come back as a
        // name with a null byte in it, which is worse than coming back empty.
        if (static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7f) {
            out += c;
        }
    }

    const std::size_t first = out.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const std::size_t last = out.find_last_not_of(" \t");
    return out.substr(first, last - first + 1);
}

// The file's modification time, formatted for a person.
//
// From the filesystem rather than from inside the file: `.dwl2` is said to
// carry a timestamp, but nothing documents its layout well enough to rely on,
// and the modification time is both certain and the same answer.
std::string modified_at(const std::filesystem::path& p) {
    std::error_code ec;
    const std::filesystem::file_time_type ft = std::filesystem::last_write_time(p, ec);
    if (ec) return {};

    // The portable conversion: file_clock and system_clock share no documented
    // epoch, so the offset between their two "now"s is what relates them.
    // std::chrono::clock_cast would say this directly and is not reliably
    // available on both standard libraries this builds against.
    const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t t = std::chrono::system_clock::to_time_t(sys);

    std::tm tm{};
#ifdef _WIN32
    if (localtime_s(&tm, &t) != 0) return {};
#else
    if (localtime_r(&t, &tm) == nullptr) return {};
#endif

    char buf[64];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm) == 0) return {};
    return buf;
}

}  // namespace

std::string lock_path_for(const std::string& drawing, const std::string& ext) {
    if (drawing.empty()) return {};
    std::filesystem::path p(drawing);
    // replace_extension rather than appending: the lock is named for the
    // DRAWING, so plan.dxf locks as plan.dwl exactly as plan.dwg does.
    p.replace_extension(ext);
    return p.string();
}

DrawingLock read_drawing_lock(const std::string& drawing) {
    DrawingLock lock;
    if (drawing.empty()) return lock;

    // `.dwl` first because it is the one whose contents are documented. Either
    // being present means somebody has the drawing open, so `.dwl2` alone is
    // still a lock -- just one we may be able to say less about.
    for (const char* ext : {".dwl", ".dwl2"}) {
        const std::string candidate = lock_path_for(drawing, ext);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(candidate, ec) || ec) continue;

        lock.present = true;
        lock.lock_path = candidate;
        lock.owner = first_text_line(candidate);
        lock.since = modified_at(candidate);
        if (!lock.owner.empty()) break;  // a named holder is the better answer
    }
    return lock;
}

std::string describe_lock(const DrawingLock& lock) {
    if (!lock.present) return {};

    std::string s = "open in another session";
    if (!lock.owner.empty()) s += " by " + lock.owner;
    if (!lock.since.empty()) s += " since " + lock.since;
    // Named so the user can go and look at it, and delete it themselves when
    // they know the session it belonged to is gone. We do not clear it for
    // them: deciding a lock is stale is exactly the question still open.
    s += " (" + path_filename(lock.lock_path) + ")";
    return s;
}

}  // namespace ncad
