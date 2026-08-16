// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "cp1252.hpp"

namespace ncad::text {
namespace {

// CP1252's 0x80..0x9F, which is the whole of where it differs from Latin-1 --
// and where the character that actually turns up lives: macOS names a machine
// "Sadie's MacBook Pro" with a curly apostrophe, byte 0x92.
constexpr char32_t kCp1252High[32] = {
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
};

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

}  // namespace

bool is_utf8(const std::string& s) {
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t extra = 0;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return false;

        if (i + extra >= s.size()) return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

std::string to_utf8(const std::string& s) {
    if (is_utf8(s)) return s;

    std::string out;
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x80) out += ch;
        else if (c < 0xA0) append_utf8(out, kCp1252High[c - 0x80]);
        else append_utf8(out, c);  // 0xA0..0xFF is Latin-1 in both
    }
    return out;
}

std::string from_utf8(const std::string& utf8) {
    // Not valid UTF-8, so it is already CP1252 or something we should not touch.
    // Guessing twice is how a name gets mangled by the round trip that was meant
    // to preserve it.
    if (!is_utf8(utf8)) return utf8;

    std::string out;
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);

        char32_t cp = 0;
        std::size_t extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1Fu;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0Fu;
            extra = 2;
        } else {
            cp = c & 0x07u;
            extra = 3;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3Fu);
        }
        i += extra + 1;

        if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) {
            // Identical in both encodings, which is most of any real name.
            out += static_cast<char>(cp);
            continue;
        }

        // The 32 that are not. A linear scan rather than a reverse table: it
        // runs over a handful of characters in a filename-length string, and a
        // second table is a second thing to keep in step with the first.
        bool found = false;
        for (std::size_t k = 0; k < 32; ++k) {
            if (kCp1252High[k] == cp && kCp1252High[k] != 0xFFFD) {
                out += static_cast<char>(0x80 + k);
                found = true;
                break;
            }
        }
        // No CP1252 spelling. '?' is what every legacy encoder does, and it is
        // honest: the character was not representable, and pretending otherwise
        // would put a wrong byte in a file AutoCAD reads.
        if (!found) out += '?';
    }
    return out;
}

}  // namespace ncad::text
