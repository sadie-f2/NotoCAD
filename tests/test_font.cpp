// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The bundled Hershey stroke font: the parse, the metrics, and the two
// conventions everything else depends on -- baseline at y = 0, cap height at
// y = 1.

#include "test.hpp"

#include "noto/font.hpp"

#include <string>

using namespace noto;

namespace {

constexpr double kTol = 1e-12;

// The vertical extent of a glyph's ink, which is the thing the conventions are
// actually about.
void ink_extent(unsigned char c, double& lo, double& hi, double& right) {
    const Glyph g = StrokeFont::romans().glyph(c);
    lo = 1e9;
    hi = -1e9;
    right = -1e9;
    for (std::uint32_t s = 0; s < g.stroke_count; ++s) {
        for (std::uint32_t k = g.stroke_begin[s]; k < g.stroke_begin[s + 1]; ++k) {
            const Vec3& p = g.points[k];
            if (p.y < lo) lo = p.y;
            if (p.y > hi) hi = p.y;
            if (p.x > right) right = p.x;
        }
    }
}

}  // namespace

TEST_CASE("font: the parse produced glyphs where letters are") {
    const StrokeFont& f = StrokeFont::romans();
    // A handful across the table, because an off-by-one in the index would
    // still return *a* glyph -- just the wrong one.
    CHECK(f.glyph('A').stroke_count == 3);
    CHECK(f.glyph('H').stroke_count == 3);
    CHECK(f.glyph('O').stroke_count == 1);
    CHECK(f.glyph('i').stroke_count == 2);  // stem and tittle
}

TEST_CASE("font: cap height is exactly one and the baseline is exactly zero") {
    // This is the convention the whole entity relies on: Text scales glyphs by
    // its height, so if cap height were not 1 every drawing's text would be the
    // wrong size in a way that still looked plausible.
    double lo = 0.0, hi = 0.0, right = 0.0;
    for (const char c : std::string("HEIXZ")) {
        ink_extent(static_cast<unsigned char>(c), lo, hi, right);
        CHECK_NEAR(hi, 1.0, 1e-9);
        CHECK_NEAR(lo, 0.0, 1e-9);
    }
}

TEST_CASE("font: descenders go below the baseline, and only they do") {
    double lo = 0.0, hi = 0.0, right = 0.0;
    for (const char c : std::string("gjpqy")) {
        ink_extent(static_cast<unsigned char>(c), lo, hi, right);
        CHECK(lo < -0.05);
    }
    for (const char c : std::string("acemnorsuvwxz")) {
        ink_extent(static_cast<unsigned char>(c), lo, hi, right);
        CHECK(lo > -1e-9);
    }
}

TEST_CASE("font: the declared descender is the deepest one there is") {
    const StrokeFont& f = StrokeFont::romans();
    CHECK(f.descender() > 0.0);
    CHECK(f.descender() < 0.5);

    double deepest = 0.0;
    for (unsigned c = kFontFirstChar; c < 127; ++c) {
        double lo = 0.0, hi = 0.0, right = 0.0;
        ink_extent(static_cast<unsigned char>(c), lo, hi, right);
        if (lo < 1e8 && -lo > deepest) deepest = -lo;
    }
    // Measured from the data at parse time rather than written down, so it
    // cannot drift away from the glyphs it describes.
    CHECK_NEAR(f.descender(), deepest, 1e-12);
}

TEST_CASE("font: a space has an advance and no ink") {
    const Glyph g = StrokeFont::romans().glyph(' ');
    CHECK(g.stroke_count == 0);
    CHECK(g.advance > 0.0);
}

TEST_CASE("font: width is the sum of advances") {
    const StrokeFont& f = StrokeFont::romans();
    CHECK_NEAR(f.width(""), 0.0, kTol);
    CHECK_NEAR(f.width("A"), f.glyph('A').advance, kTol);
    CHECK_NEAR(f.width("AB"), f.glyph('A').advance + f.glyph('B').advance, kTol);
    CHECK(f.width("MMMM") > f.width("iiii"));  // a real font, not a monospace one
}

TEST_CASE("font: an unmappable byte leaves a gap rather than shifting the line") {
    const StrokeFont& f = StrokeFont::romans();
    const Glyph g = f.glyph(0x01);
    CHECK(g.stroke_count == 0);
    // Silently dropping it would slide the rest of the string left, which is a
    // worse lie than a visible gap.
    CHECK_NEAR(g.advance, f.glyph(' ').advance, kTol);
    CHECK_NEAR(f.width("A\x01"), f.width("A ") , kTol);
}

TEST_CASE("font: ink starts at the pen and stays inside the advance") {
    const StrokeFont& f = StrokeFont::romans();
    for (const char c : std::string("AWjgo0")) {
        double lo = 0.0, hi = 0.0, right = 0.0;
        ink_extent(static_cast<unsigned char>(c), lo, hi, right);
        const Glyph g = f.glyph(static_cast<unsigned char>(c));
        // The side bearings are what stop letters touching; ink inside the
        // advance is what makes that true.
        CHECK(right <= g.advance + 1e-9);
        for (std::uint32_t s = 0; s < g.stroke_count; ++s) {
            for (std::uint32_t k = g.stroke_begin[s]; k < g.stroke_begin[s + 1]; ++k) {
                CHECK(g.points[k].x >= -1e-9);
                CHECK_NEAR(g.points[k].z, 0.0, kTol);
            }
        }
    }
}

TEST_CASE("font: strokes are disjoint runs, in order, over one point pool") {
    // The pen-up encoding is what separates the two strokes of an 'i'. If the
    // ranges overlapped or ran backwards, glyphs would draw joined-up garbage.
    const Glyph g = StrokeFont::romans().glyph('i');
    REQUIRE(g.stroke_count == 2);
    CHECK(g.stroke_begin[0] < g.stroke_begin[1]);
    CHECK(g.stroke_begin[1] < g.stroke_begin[2]);
}
