// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/paths.hpp"

#include "cp1252.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>

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

std::string read_whole(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string trimmed(std::string s) {
    // Control characters go, which also removes the CR of a CRLF and the
    // trailing space `.dwl`'s machine-name line actually carries.
    std::string out;
    for (const char c : s) {
        if (static_cast<unsigned char>(c) >= 0x20 || c == '\t') out += c;
    }
    const std::size_t first = out.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    return out.substr(first, out.find_last_not_of(" \t") - first + 1);
}

// The three XML entities a name can legitimately contain. Symmetric with the
// escaping the writer does -- a machine called "R&D box" has to survive the
// round trip, and unescaping what we never escape would corrupt a literal
// "&amp;" that AutoCAD wrote as text.
std::string unescaped(const std::string& s) {
    if (s.find('&') == std::string::npos) return s;

    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        if (s.compare(i, 4, "&lt;") == 0) {
            out += '<';
            i += 4;
        } else if (s.compare(i, 4, "&gt;") == 0) {
            out += '>';
            i += 4;
        } else if (s.compare(i, 5, "&amp;") == 0) {
            out += '&';
            i += 5;
        } else {
            out += s[i++];
        }
    }
    return out;
}

// The text between `<tag>` and `</tag>`.
//
// A scan and not a parse, deliberately: `.dwl2`'s declaration is malformed --
// `<?xml version="1.0" encoding="UTF-8">` with no closing `?` -- so every real
// XML parser rejects the file. See examples/acad-locks.
std::string tag_value(const std::string& xml, const std::string& tag) {
    const std::string open = "<" + tag + ">";
    const std::string close = "</" + tag + ">";
    const std::size_t a = xml.find(open);
    if (a == std::string::npos) return {};
    const std::size_t b = xml.find(close, a + open.size());
    if (b == std::string::npos) return {};
    return unescaped(trimmed(text::to_utf8(xml.substr(a + open.size(), b - a - open.size()))));
}

// `.dwl`'s three lines: user, machine, datetime. The third is a long localised
// form we do not read -- see modified_at for why.
void read_dwl(const std::string& content, std::string& owner, std::string& machine) {
    std::size_t start = 0;
    for (int line = 0; line < 2 && start <= content.size(); ++line) {
        std::size_t end = content.find('\n', start);
        if (end == std::string::npos) end = content.size();
        std::string value = trimmed(text::to_utf8(content.substr(start, end - start)));
        if (line == 0) owner = value;
        else machine = value;
        start = end + 1;
    }
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

    // Either file present means somebody has the drawing open. AutoCAD writes
    // both, but a lock is a lock whichever survives -- so presence is taken
    // from either and the FIELDS are read from whichever can supply them.
    const std::string dwl = lock_path_for(drawing, ".dwl");
    const std::string dwl2 = lock_path_for(drawing, ".dwl2");

    std::error_code ec;
    const bool has_dwl = std::filesystem::is_regular_file(dwl, ec) && !ec;
    ec.clear();
    const bool has_dwl2 = std::filesystem::is_regular_file(dwl2, ec) && !ec;
    if (!has_dwl && !has_dwl2) return lock;

    lock.present = true;
    // `.dwl` is the one named, because it is the one people know and the one a
    // user reaching for a stale lock will look for. We never delete either.
    lock.lock_path = has_dwl ? dwl : dwl2;
    lock.since = modified_at(lock.lock_path);

    // `.dwl2` is read FIRST despite being the newer file, because it is UTF-8
    // and tagged: what it gives is unambiguous, where `.dwl` has to be sniffed
    // for its encoding and counted by line.
    if (has_dwl2) {
        const std::string xml = read_whole(dwl2);
        lock.owner = tag_value(xml, "username");
        lock.machine = tag_value(xml, "machinename");
    }
    if (has_dwl && (lock.owner.empty() || lock.machine.empty())) {
        std::string owner, machine;
        read_dwl(read_whole(dwl), owner, machine);
        if (lock.owner.empty()) lock.owner = owner;
        if (lock.machine.empty()) lock.machine = machine;
    }
    return lock;
}

std::string describe_lock(const DrawingLock& lock) {
    if (!lock.present) return {};

    std::string s = "open in another session";
    if (!lock.owner.empty()) s += " by " + lock.owner;
    // The machine matters over a shared folder: "sadie" on this box and "sadie"
    // on another are very different situations and the name alone cannot tell
    // them apart.
    if (!lock.machine.empty()) s += " on " + lock.machine;
    if (!lock.since.empty()) s += " since " + lock.since;
    // Named so the user can go and look at it, and delete it themselves when
    // they know the session it belonged to is gone. We do not clear it for
    // them: deciding a lock is stale is exactly the question still open.
    s += " (" + path_filename(lock.lock_path) + ")";
    return s;
}

}  // namespace ncad
