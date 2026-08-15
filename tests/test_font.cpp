// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The bundled Hershey stroke font: the parse, the metrics, and the two
// conventions everything else depends on -- baseline at y = 0, cap height at
// y = 1.

#include "test.hpp"

#include "ncad/entities.hpp"
#include "ncad/render.hpp"
#include "ncad/font.hpp"

#include <string>
#include <vector>

using namespace ncad;

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

// Counts the polylines one line of text emits, which is how the %%u and %%o
// rules are observed: they are strokes like any other.
struct CountingRenderer final : Renderer {
    int polys{0};
    std::vector<Vec3> last;

    void begin_entity(const EntityProps&) override {}
    void polyline(const Vec3* pts, std::size_t count, bool) override {
        ++polys;
        last.assign(pts, pts + count);
    }
};

int stroke_count_of(const std::string& s) {
    CountingRenderer r;
    draw_text_line(s, Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}, 1.0, 1.0, 0.0, r);
    return r.polys;
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

TEST_CASE("control codes: the three symbols decode to one glyph each") {
    std::vector<TextCell> cells;

    decode_text("%%d", cells);
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].code == kSymbolDegree);

    decode_text("%%c", cells);
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].code == kSymbolDiameter);

    decode_text("%%p", cells);
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].code == kSymbolPlusMinus);
}

TEST_CASE("control codes: case does not matter") {
    // R12 accepts either, and dimension labels are written upper case while
    // hand-typed text is usually not.
    std::vector<TextCell> lower, upper;
    decode_text("%%d", lower);
    decode_text("%%D", upper);
    REQUIRE(lower.size() == 1);
    REQUIRE(upper.size() == 1);
    CHECK(lower[0].code == upper[0].code);
}

TEST_CASE("control codes: a dimension label decodes to what it draws") {
    // The bug this whole thing exists for: `90.0000%%D` read as ten characters
    // on screen, escape and all.
    std::vector<TextCell> cells;
    decode_text("90.0000%%D", cells);
    REQUIRE(cells.size() == 8);
    CHECK(cells[0].code == '9');
    CHECK(cells[7].code == kSymbolDegree);

    decode_text("%%C50.0000", cells);
    REQUIRE(cells.size() == 8);
    CHECK(cells[0].code == kSymbolDiameter);
}

TEST_CASE("control codes: %%%% is a per-cent sign") {
    std::vector<TextCell> cells;
    decode_text("50%%%", cells);
    REQUIRE(cells.size() == 3);
    CHECK(cells[2].code == '%');
}

TEST_CASE("control codes: %%nnn names a byte") {
    std::vector<TextCell> cells;
    decode_text("%%065", cells);
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].code == 'A');

    // Fewer than three digits is taken as written rather than swallowing what
    // follows while it hunts for a third.
    decode_text("%%65x", cells);
    REQUIRE(cells.size() == 2);
    CHECK(cells[0].code == 'A');
    CHECK(cells[1].code == 'x');

    // Past 255 names no byte at all, so it draws the same honest gap any
    // unmappable code does.
    decode_text("%%999", cells);
    REQUIRE(cells.size() == 1);
    CHECK(StrokeFont::romans().glyph(cells[0].code).stroke_count == 0);
}

TEST_CASE("control codes: an unknown code is left literal") {
    // No terminator means no way to know what was meant, and deleting the
    // user's text on a guess is worse than showing it.
    std::vector<TextCell> cells;
    decode_text("%%x", cells);
    REQUIRE(cells.size() == 3);
    CHECK(cells[0].code == '%');
    CHECK(cells[1].code == '%');
    CHECK(cells[2].code == 'x');

    // A bare trailing `%%` has no third character to be a code at all.
    decode_text("A%%", cells);
    REQUIRE(cells.size() == 3);
}

TEST_CASE("control codes: %%o and %%u are toggles, not characters") {
    std::vector<TextCell> cells;
    decode_text("a%%ub%%uc", cells);
    REQUIRE(cells.size() == 3);
    CHECK(cells[0].code == 'a');
    CHECK(cells[1].code == 'b');
    CHECK(cells[2].code == 'c');

    // Only what sits between the toggles is underscored.
    CHECK(!cells[0].underscore);
    CHECK(cells[1].underscore);
    CHECK(!cells[2].underscore);

    decode_text("%%oAB", cells);
    REQUIRE(cells.size() == 2);
    CHECK(cells[0].overscore);
    CHECK(cells[1].overscore);
    CHECK(!cells[0].underscore);
}

TEST_CASE("control codes: width measures the glyphs, not the escape") {
    const StrokeFont& f = StrokeFont::romans();

    // This is what makes a centred dimension label actually centred.
    CHECK_NEAR(f.width("90%%D"), f.width("90") + f.glyph(kSymbolDegree).advance, kTol);
    CHECK(f.width("90%%D") < f.width("90%%X"));

    // The toggles are not characters, so they take no width.
    CHECK_NEAR(f.width("%%uAB%%u"), f.width("AB"), kTol);
}

TEST_CASE("control codes: the symbols have ink, inside their advance") {
    const StrokeFont& f = StrokeFont::romans();
    for (const std::uint16_t code : {kSymbolDegree, kSymbolDiameter, kSymbolPlusMinus}) {
        const Glyph g = f.glyph(code);
        CHECK(g.stroke_count > 0);
        CHECK(g.advance > 0.0);
        for (std::uint32_t s = 0; s < g.stroke_count; ++s) {
            CHECK(g.stroke_begin[s] < g.stroke_begin[s + 1]);
            for (std::uint32_t k = g.stroke_begin[s]; k < g.stroke_begin[s + 1]; ++k) {
                const Vec3& p = g.points[k];
                CHECK(p.x >= -1e-9);
                CHECK(p.x <= g.advance + 1e-9);
                // On or above the baseline, which is what keeps them out of
                // the measured descender and every Bottom-justified string.
                CHECK(p.y >= -1e-9);
                CHECK_NEAR(p.z, 0.0, kTol);
            }
        }
    }
}

TEST_CASE("control codes: a rule is one polyline per run, not one per glyph") {
    // A seam at every side bearing is what drawing it per character would
    // give, and hit-testing would then see a row of stubs.
    const int plain = stroke_count_of("ABC");
    CHECK(stroke_count_of("%%uABC%%u") == plain + 1);
    CHECK(stroke_count_of("%%oABC%%o") == plain + 1);
    CHECK(stroke_count_of("%%o%%uABC") == plain + 2);

    // Left open, it runs to the end of the line rather than being dropped.
    CHECK(stroke_count_of("%%uABC") == plain + 1);
}

TEST_CASE("control codes: the escape never reaches the strokes") {
    // The end-to-end property. `%%D` drew a per-cent, a per-cent and a D
    // before; now it draws one ring.
    CHECK(stroke_count_of("%%D") ==
          static_cast<int>(StrokeFont::romans().glyph(kSymbolDegree).stroke_count));
}

TEST_CASE("control codes: TEXT measures and justifies by the decoded string") {
    // Text::text_width feeds justification and the bounding box, so a label
    // that measured its escape sat off-centre by two characters.
    Text t(Vec3{0, 0, 0}, "90%%D", 1.0);
    const StrokeFont& f = StrokeFont::romans();
    CHECK_NEAR(t.text_width(), f.width("90") + f.glyph(kSymbolDegree).advance, kTol);
}

TEST_CASE("font: strokes are disjoint runs, in order, over one point pool") {
    // The pen-up encoding is what separates the two strokes of an 'i'. If the
    // ranges overlapped or ran backwards, glyphs would draw joined-up garbage.
    const Glyph g = StrokeFont::romans().glyph('i');
    REQUIRE(g.stroke_count == 2);
    CHECK(g.stroke_begin[0] < g.stroke_begin[1]);
    CHECK(g.stroke_begin[1] < g.stroke_begin[2]);
}
