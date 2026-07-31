// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/paths.hpp"

#include <cstdlib>
#include <filesystem>

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

}  // namespace ncad
