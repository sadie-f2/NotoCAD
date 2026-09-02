// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/input_text.hpp"

#include "ncad/units.hpp"

#include "ncad/osnap.hpp"

#include <cmath>
#include <cstdlib>
#include <string>

namespace ncad {
namespace {

bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

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

// Command-line angles are DEGREES, while AutoLISP angles are radians and the
// DXF file stores degrees for arcs. Three conventions, one boundary each; this
// is the one a person types at.
inline constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

// "1,2,3" or "1,2". Z defaults to zero, as R12 does for 2D input.
bool parse_point(std::string_view text, Vec3& out, std::int32_t insunits,
                 std::string& why) {
    double c[3] = {0.0, 0.0, 0.0};
    std::size_t count = 0;
    std::size_t start = 0;

    while (start <= text.size() && count < 3) {
        const std::size_t comma = text.find(',', start);
        const std::string_view field =
            text.substr(start, comma == std::string_view::npos ? std::string_view::npos
                                                              : comma - start);
        // Per coordinate, so `1/4",0` and `10mm,5mm` both work and a mixed
        // pair does too -- which is the whole point of converting at the
        // parser rather than at one command.
        if (!parse_length(field, insunits, c[count], why)) return false;
        ++count;
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    if (count < 2) return false;
    out = Vec3{c[0], c[1], c[2]};
    return true;
}

// "30<45" -- a distance and a bearing in degrees, in the XY plane. Z comes from
// the base point, since polar input is planar.
bool parse_polar(std::string_view text, const Vec3& base, Vec3& out, std::int32_t insunits,
                 std::string& why) {
    const std::size_t bracket = text.find('<');
    if (bracket == std::string_view::npos) return false;

    double distance = 0.0;
    double degrees = 0.0;
    // The DISTANCE is a length and may carry a unit; the BEARING is an angle
    // and may not. `@1/4"<45` is a quarter inch at 45 degrees; `@10<45mm` is
    // not a thing and must stay an error.
    if (!parse_length(text.substr(0, bracket), insunits, distance, why)) return false;
    if (!parse_double(text.substr(bracket + 1), degrees)) return false;

    const double radians = degrees * kDegToRad;
    out = Vec3{base.x + distance * std::cos(radians), base.y + distance * std::sin(radians),
               base.z};
    return true;
}

// The R12 coordinate grammar:
//
//     10,20     absolute
//     @5,0      relative to the last point
//     @         the last point itself
//     @30<45    polar, relative to the last point
//     30<45     polar, from the origin
//
// Without the relative forms, drawing anything by hand is arithmetic homework.
bool parse_coordinate(const Prompt& prompt, std::string_view text, Vec3& out,
                      std::string& error, const Mat4* ucs_to_world,
                      std::int32_t insunits) {
    const bool relative = !text.empty() && text.front() == '@';
    if (relative && !prompt.has_last_point) {
        error = "no last point to measure from";
        return false;
    }
    const Vec3 base = relative ? prompt.last_point : Vec3{0.0, 0.0, 0.0};

    // "@" on its own is the last point.
    if (relative && text.size() == 1) {
        out = base;
        return true;
    }
    const std::string_view body = relative ? text.substr(1) : text;

    if (body.find('<') != std::string_view::npos) {
        // Polar is solved about the origin, then placed, so that the angle is
        // measured in the UCS's own plane rather than in world XY.
        Vec3 polar;
        if (!parse_polar(body, Vec3{0.0, 0.0, 0.0}, polar, insunits, error)) {
            // A unit the drawing cannot honour explains itself; anything else
            // gets the grammar's own message.
            if (!error.empty()) return false;
            error = "expected a distance and angle like 30<45";
            return false;
        }
        const Vec3 offset = ucs_to_world ? ucs_to_world->transform_vector(polar) : polar;
        out = relative ? base + offset
                       : (ucs_to_world ? ucs_to_world->transform_point(polar) : polar);
        return true;
    }

    Vec3 coordinates;
    if (!parse_point(body, coordinates, insunits, error)) {
        if (!error.empty()) return false;
        error = relative ? "expected an offset like @5,0 or @30<45"
                         : "expected a point like 1,2 or 1,2,3";
        return false;
    }
    // The UCS is applied here, at the one place a typed coordinate becomes a
    // point. An absolute coordinate is a location and transforms as one; a
    // relative one is a displacement from a base that is already in world, so
    // only its direction and scale are the UCS's business.
    if (relative) {
        out = base + (ucs_to_world ? ucs_to_world->transform_vector(coordinates) : coordinates);
    } else {
        out = ucs_to_world ? ucs_to_world->transform_point(coordinates) : coordinates;
    }
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
                 std::string& error, const Mat4* ucs_to_world, std::int32_t insunits) {
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
            // An osnap override is tried before coordinates. There is no
            // ambiguity to worry about: R12 coordinates start with a digit,
            // sign, decimal point or '@', and every snap name starts with a
            // letter -- so no input can be read both ways.
            OsnapMask mask = kOsnapNone;
            if (!token.empty() && is_alpha(token.front()) && parse_osnap_mask(token, &mask)) {
                out = InputValue::of_osnap_override(mask);
                return true;
            }
            Vec3 p;
            if (!parse_coordinate(prompt, token, p, error, ucs_to_world, insunits)) return false;
            out = InputValue::of_point(p);
            return true;
        }

        case PromptKind::Distance:
        case PromptKind::Angle:
        case PromptKind::Real: {
            // A distance prompt also takes a point, since dragging to a second
            // point is how a radius is given with a mouse.
            double d = 0.0;
            // Distance and Real are LENGTHS and may carry a unit; an Angle
            // never can. `30<45` must not start accepting `30<45mm`, and a
            // bearing has no unit to convert into anyway. A `Real` is included
            // because a Real prompt is often a length in practice -- EXTRUDE's
            // height is one.
            if (prompt.kind == PromptKind::Angle) {
                if (parse_double(token, d)) {
                    out = InputValue::of_real(d);
                    return true;
                }
            } else {
                std::string why;
                if (parse_length(token, insunits, d, why)) {
                    out = InputValue::of_real(d);
                    return true;
                }
                // A unit suffix the drawing cannot honour is a REFUSAL with a
                // reason, not a fall-through to "expected a number" -- the
                // user asked for something specific and deserves to know why
                // it was not given.
                if (!why.empty()) {
                    error = why;
                    return false;
                }
            }
            std::string ignored;
            Vec3 p;
            if (prompt_takes_point(prompt.kind) &&
                parse_coordinate(prompt, token, p, ignored, ucs_to_world, insunits)) {
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
    // Read fresh each time: a script that runs UCS half way through must have
    // everything after it read in the new frame.
    Mat4 frame = Mat4::identity();
    const Mat4* ucs = nullptr;
    std::int32_t insunits = 0;
    if (db_) {
        frame = db_->current_ucs().to_world();
        ucs = &frame;
        // Read fresh for the same reason the UCS is: a script may SETVAR
        // INSUNITS half way through, and everything after it is in the new
        // unit.
        insunits = db_->sysvars().get_int(Sysvar::InsUnits);
    }

    if (!parse_input(prompt, token, out, error_, ucs, insunits)) {
        // Consume it anyway: leaving a token that cannot be parsed in place
        // would spin forever.
        ++pos_;
        return false;
    }
    ++pos_;
    return true;
}

}  // namespace ncad
