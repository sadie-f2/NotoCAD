// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/input_text.hpp"

#include <cstdlib>
#include <string>

namespace noto {
namespace {

std::string upcase(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

// Full-consumption parse: "1.5" is a number, "1.5x" is not.
bool parse_double(std::string_view text, double& out) {
    if (text.empty()) return false;
    const std::string s(text);
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size()) return false;
    out = v;
    return true;
}

bool parse_long(std::string_view text, long& out) {
    if (text.empty()) return false;
    const std::string s(text);
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size()) return false;
    out = v;
    return true;
}

// "1,2,3" or "1,2". Z defaults to zero, as R12 does for 2D input.
bool parse_point(std::string_view text, Vec3& out) {
    double c[3] = {0.0, 0.0, 0.0};
    std::size_t count = 0;
    std::size_t start = 0;

    while (start <= text.size() && count < 3) {
        const std::size_t comma = text.find(',', start);
        const std::string_view field =
            text.substr(start, comma == std::string_view::npos ? std::string_view::npos
                                                              : comma - start);
        if (!parse_double(field, c[count])) return false;
        ++count;
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    if (count < 2) return false;
    out = Vec3{c[0], c[1], c[2]};
    return true;
}

// R12 accepts the shortest unambiguous prefix of a keyword, which is why the
// capitalised letters in "[Close/Undo]" matter.
bool match_keyword(const Prompt& prompt, std::string_view token, std::string& out) {
    const std::string upper = upcase(token);
    if (upper.empty()) return false;

    const std::string* match = nullptr;
    for (const std::string& keyword : prompt.keywords) {
        const std::string candidate = upcase(keyword);
        if (candidate.compare(0, upper.size(), upper) != 0) continue;
        if (match) return false;  // ambiguous prefix
        match = &keyword;
        if (candidate.size() == upper.size()) break;  // exact wins outright
    }
    if (!match) return false;
    out = upcase(*match);
    return true;
}

}  // namespace

bool parse_input(const Prompt& prompt, std::string_view token, InputValue& out,
                 std::string& error) {
    error.clear();

    // An empty token is Enter.
    if (token.empty()) {
        if (!prompt.allow_empty) {
            error = "this prompt requires a value";
            return false;
        }
        out = InputValue::none();
        return true;
    }

    if (token == "\x1b") {  // Escape
        out = InputValue::cancel();
        return true;
    }

    // Keywords are checked before values, so "D" at a distance prompt selects
    // Diameter rather than failing to parse as a number.
    std::string keyword;
    if (match_keyword(prompt, token, keyword)) {
        out = InputValue::of_keyword(keyword);
        return true;
    }

    switch (prompt.kind) {
        case PromptKind::Point: {
            Vec3 p;
            if (!parse_point(token, p)) {
                error = "expected a point like 1,2 or 1,2,3";
                return false;
            }
            out = InputValue::of_point(p);
            return true;
        }

        case PromptKind::Distance:
        case PromptKind::Angle:
        case PromptKind::Real: {
            // A distance prompt also takes a point, since dragging to a second
            // point is how a radius is given with a mouse.
            double d = 0.0;
            if (parse_double(token, d)) {
                out = InputValue::of_real(d);
                return true;
            }
            Vec3 p;
            if (prompt.kind == PromptKind::Distance && parse_point(token, p)) {
                out = InputValue::of_point(p);
                return true;
            }
            error = "expected a number";
            return false;
        }

        case PromptKind::Integer: {
            long v = 0;
            if (!parse_long(token, v)) {
                error = "expected an integer";
                return false;
            }
            out = InputValue::of_integer(static_cast<std::int32_t>(v));
            return true;
        }

        case PromptKind::Entity: {
            // Text selection is by handle. A viewport will select by pick point
            // instead, producing the same InputValue -- which is the point.
            long v = 0;
            if (!parse_long(token, v) || v <= 0) {
                error = "expected an entity handle";
                return false;
            }
            out = InputValue::of_entity(static_cast<Handle>(v));
            return true;
        }

        case PromptKind::String:
            out = InputValue::of_string(std::string(token));
            return true;
    }

    error = "unhandled prompt kind";
    return false;
}

std::vector<std::string> tokenize_script(std::string_view text) {
    std::vector<std::string> tokens;
    std::size_t i = 0;

    while (i < text.size()) {
        const char c = text[i];

        // A newline is a token boundary, and a blank line means Enter.
        if (c == '\n') {
            // Two newlines in a row, or a leading one, is an explicit Enter.
            const bool blank = tokens.empty() || i == 0 || text[i - 1] == '\n';
            if (blank) tokens.emplace_back();
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        if (c == ';') {  // comment to end of line
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }
        if (c == '"') {
            std::string quoted;
            ++i;
            while (i < text.size() && text[i] != '"') quoted += text[i++];
            if (i < text.size()) ++i;  // closing quote
            tokens.push_back(std::move(quoted));
            continue;
        }

        const std::size_t start = i;
        while (i < text.size() && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' &&
               text[i] != '\n' && text[i] != ';') {
            ++i;
        }
        tokens.emplace_back(text.substr(start, i - start));
    }
    return tokens;
}

bool TextInputSource::next_value(const Prompt& prompt, InputValue& out) {
    if (pos_ >= tokens_.size()) return false;

    const std::string& token = tokens_[pos_];
    if (!parse_input(prompt, token, out, error_)) {
        // Consume it anyway: leaving a token that cannot be parsed in place
        // would spin forever.
        ++pos_;
        return false;
    }
    ++pos_;
    return true;
}

}  // namespace noto
