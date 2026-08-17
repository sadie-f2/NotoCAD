// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "wildcard.hpp"

#include <cstddef>

namespace ncad::lisp {
namespace {

char fold(char c, bool on) {
    if (!on) return c;
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_alnum(char c) { return is_digit(c) || is_alpha(c); }

// A bracket set, starting at `p` on the '['. Returns whether `c` is in it and
// advances `p` past the closing ']'.
bool match_set(char c, std::string_view pat, std::size_t& p, bool fold_case) {
    ++p;  // past '['
    bool negate = false;
    if (p < pat.size() && pat[p] == '~') {
        negate = true;
        ++p;
    }

    bool hit = false;
    bool first = true;
    while (p < pat.size() && (pat[p] != ']' || first)) {
        first = false;

        // A range, but only when the '-' has something on both sides. A
        // trailing '-' is a literal, which is what "[A-]" has to mean.
        if (p + 2 < pat.size() && pat[p + 1] == '-' && pat[p + 2] != ']') {
            const char lo = fold(pat[p], fold_case);
            const char hi = fold(pat[p + 2], fold_case);
            const char f = fold(c, fold_case);
            if (f >= lo && f <= hi) hit = true;
            p += 3;
            continue;
        }
        if (fold(pat[p], fold_case) == fold(c, fold_case)) hit = true;
        ++p;
    }
    if (p < pat.size()) ++p;  // past ']'
    return negate ? !hit : hit;
}

// One pattern against one string, with no comma handling: the alternatives are
// split before this is called.
bool match_one(std::string_view s, std::string_view pat, bool fold_case) {
    std::size_t si = 0;
    std::size_t pi = 0;

    // Where to resume if a '*' has to swallow one more character. Recorded
    // rather than recursed, so a pattern of many stars cannot blow the stack.
    std::size_t star_p = std::string_view::npos;
    std::size_t star_s = 0;

    while (si < s.size()) {
        if (pi < pat.size()) {
            const char pc = pat[pi];

            if (pc == '`' && pi + 1 < pat.size()) {
                // Escaped: the next character is a literal, whatever it is.
                if (fold(pat[pi + 1], fold_case) == fold(s[si], fold_case)) {
                    pi += 2;
                    ++si;
                    continue;
                }
            } else if (pc == '*') {
                star_p = pi++;
                star_s = si;
                continue;
            } else if (pc == '[') {
                std::size_t probe = pi;
                if (match_set(s[si], pat, probe, fold_case)) {
                    pi = probe;
                    ++si;
                    continue;
                }
            } else {
                const bool ok = (pc == '?') || (pc == '#' && is_digit(s[si])) ||
                                (pc == '@' && is_alpha(s[si])) ||
                                (pc == '.' && !is_alnum(s[si])) ||
                                (fold(pc, fold_case) == fold(s[si], fold_case));
                if (ok) {
                    ++pi;
                    ++si;
                    continue;
                }
            }
        }

        // No match here. Back up to the last '*' and let it take one more.
        if (star_p != std::string_view::npos) {
            pi = star_p + 1;
            si = ++star_s;
            continue;
        }
        return false;
    }

    // The string is spent; the pattern may have trailing stars and nothing else.
    while (pi < pat.size() && pat[pi] == '*') ++pi;
    return pi == pat.size();
}

}  // namespace

bool wildcard_match(std::string_view text, std::string_view pattern, bool fold_case) {
    // A leading '~' negates the WHOLE pattern, alternatives and all. It is an
    // anchor at position zero rather than an operator, which is the part people
    // get wrong.
    if (!pattern.empty() && pattern[0] == '~') {
        // Counted, then ONE recursion. Recursing per tilde meant a pattern of
        // repeated tildes -- reachable from `(wcmatch "a" p)` with a string the
        // script built -- put a frame on the C stack for each one and overflowed
        // it. The parity is all the negations amount to, and stripping them
        // first means the call below cannot re-enter this branch.
        bool negate = false;
        while (!pattern.empty() && pattern[0] == '~') {
            negate = !negate;
            pattern.remove_prefix(1);
        }
        const bool matched = wildcard_match(text, pattern, fold_case);
        return negate ? !matched : matched;
    }

    // Comma-separated alternatives. Split here rather than inside the matcher so
    // that a comma inside a bracket set is still a comma, which is what R12
    // does -- brackets do not protect it.
    std::size_t start = 0;
    for (std::size_t i = 0; i <= pattern.size(); ++i) {
        const bool end = (i == pattern.size());
        if (!end && pattern[i] != ',') {
            // A backquote escapes the comma that follows it.
            if (pattern[i] == '`') ++i;
            continue;
        }
        if (match_one(text, pattern.substr(start, i - start), fold_case)) return true;
        start = i + 1;
    }
    return false;
}

}  // namespace ncad::lisp
