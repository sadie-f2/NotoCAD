// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/units.hpp"

#include <cmath>
#include <cstdlib>
#include <string>

namespace ncad {
namespace {

struct UnitRow {
    std::int32_t code;
    const char* name;
    double mm;  // millimetres in one of these
};

// Exact by definition where the standard says so: an inch is 25.4 mm exactly,
// and a foot is twelve of them. Written out rather than derived so the numbers
// can be read against the definition.
constexpr UnitRow kUnits[] = {
    {kInsUnitless, "unitless", 0.0},   {kInsInches, "inches", 25.4},
    {kInsFeet, "feet", 304.8},         {kInsMillimetres, "millimetres", 1.0},
    {kInsCentimetres, "centimetres", 10.0}, {kInsMetres, "metres", 1000.0},
};

// What may be typed after a number, and what it is worth in millimetres. The
// symbols matter as much as the words: inch work is written `1/4"`, and a
// designer typing that should not have to learn `in`.
struct SuffixRow {
    const char* text;
    double mm;
};

// Longest first, so `mm` is not read as `m` with a stray character after it.
constexpr SuffixRow kSuffixes[] = {
    {"mm", 1.0},  {"cm", 10.0}, {"ft", 304.8}, {"in", 25.4},
    {"m", 1000.0}, {"\"", 25.4}, {"'", 304.8},
};

bool iequals_ascii(std::string_view a, const char* b) {
    std::size_t i = 0;
    for (; i < a.size(); ++i) {
        if (b[i] == '\0') return false;
        const char x = (a[i] >= 'a' && a[i] <= 'z') ? static_cast<char>(a[i] - 32) : a[i];
        const char y = (b[i] >= 'a' && b[i] <= 'z') ? static_cast<char>(b[i] - 32) : b[i];
        if (x != y) return false;
    }
    return b[i] == '\0';
}

// Full-consumption, as the existing parse_double is: `1.5` is a number and
// `1.5x` is not. That rule is what makes a typo an error rather than a
// silently truncated value, and it must survive units.
bool plain_number(std::string_view text, double& out) {
    if (text.empty()) return false;
    const std::string s(text);
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size()) return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

// `1/4`, and `1-1/4` for the mixed form.
//
// The mixed form is spelled with a hyphen rather than the space AutoCAD also
// accepts, because a typed line is TOKENISED on whitespace before it reaches
// any parser -- `1 1/4` arrives as two tokens and the second one is a
// separate answer. The hyphen is unambiguous here since coordinates carry no
// arithmetic.
bool fraction(std::string_view text, double& out) {
    const std::size_t slash = text.find('/');
    if (slash == std::string_view::npos) return false;

    std::string_view whole;
    std::string_view num = text.substr(0, slash);
    const std::string_view den = text.substr(slash + 1);

    const std::size_t hyphen = num.rfind('-');
    // A leading '-' is a sign, not a separator: `-1/4` is negative a quarter.
    if (hyphen != std::string_view::npos && hyphen > 0) {
        whole = num.substr(0, hyphen);
        num = num.substr(hyphen + 1);
    }

    double n = 0.0;
    double d = 0.0;
    if (!plain_number(num, n) || !plain_number(den, d)) return false;
    if (d == 0.0) return false;

    double w = 0.0;
    if (!whole.empty()) {
        if (!plain_number(whole, w)) return false;
        // `1-1/4` is one and a quarter; a negative whole keeps the fraction on
        // the same side of zero rather than cancelling it.
        out = w < 0.0 ? w - n / d : w + n / d;
        return true;
    }
    out = n / d;
    return true;
}

bool number_or_fraction(std::string_view text, double& out) {
    if (plain_number(text, out)) return true;
    return fraction(text, out);
}

}  // namespace

bool insunits_valid(std::int32_t code) {
    for (const UnitRow& r : kUnits) {
        if (r.code == code) return true;
    }
    return false;
}

const char* insunits_name(std::int32_t code) {
    for (const UnitRow& r : kUnits) {
        if (r.code == code) return r.name;
    }
    return "unknown";
}

std::string insunits_choices() {
    std::string s;
    for (const UnitRow& r : kUnits) {
        if (!s.empty()) s += ", ";
        s += std::to_string(r.code);
        s += ' ';
        s += r.name;
    }
    return s;
}

double mm_per_insunit(std::int32_t code) {
    for (const UnitRow& r : kUnits) {
        if (r.code == code) return r.mm;
    }
    return 0.0;
}

bool parse_length(std::string_view text, std::int32_t insunits, double& out,
                  std::string& error) {
    if (text.empty()) return false;

    // The suffix, if there is one. Trailing spaces are allowed between the
    // number and its unit -- `6.35 mm` is what a person writes.
    for (const SuffixRow& s : kSuffixes) {
        const std::size_t n = std::string_view(s.text).size();
        if (text.size() <= n) continue;
        if (!iequals_ascii(text.substr(text.size() - n), s.text)) continue;

        std::string_view head = text.substr(0, text.size() - n);
        while (!head.empty() && (head.back() == ' ' || head.back() == '\t')) {
            head.remove_suffix(1);
        }
        double value = 0.0;
        if (!number_or_fraction(head, value)) return false;

        const double mm_per_workspace = mm_per_insunit(insunits);
        if (mm_per_workspace <= 0.0) {
            // No answer to "a quarter inch of what". Refusing names the
            // setting, because the user can fix it in one command and a guess
            // they cannot see is what sends a part out at the wrong size.
            error = std::string("this drawing has no unit, so \"") + std::string(text) +
                    "\" has nothing to convert into. Set INSUNITS first (" +
                    insunits_choices() + ")";
            return false;
        }
        out = value * s.mm / mm_per_workspace;
        return true;
    }

    // No suffix: the workspace unit, exactly as before this existed. Byte for
    // byte, which is what keeps every existing drawing and script unchanged.
    return number_or_fraction(text, out);
}

}  // namespace ncad
