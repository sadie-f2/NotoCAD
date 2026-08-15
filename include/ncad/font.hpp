// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Stroke fonts: glyphs as polylines, which is the only thing the render path
// speaks.
//
// R12's text was drawn from SHX shape definitions, and the good ones -- romans,
// romand, italicc -- descend from the Hershey set. A Hershey glyph is literally
// a list of polylines, so bundling one is not an approximation of what R12 did
// but a reimplementation of the same thing from the same ancestry. See
// SF_todo.md's "TEXT: bundle a Hershey font, and it is not a hack".
//
// This keeps the core headless. No Qt fonts are involved, so DXF-written TEXT
// and screen TEXT come from one source and CLAUDE.md's "rendering the same
// database two ways is a correctness check" survives.
//
// An SHX parser -- which would let a drawing use the user's own AutoCAD fonts --
// sits behind this same interface later. Same layering as DXF-first with DWG
// optional: what is in-tree is complete on its own and the compatibility path
// is an addition rather than a rewrite.
#pragma once

#include "ncad/vec3.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ncad {

// One glyph's strokes in FONT UNITS: the pen starts at x = 0, the baseline is
// y = 0, and the cap height is y = 1. Multiplying by the text height gives world
// units directly, which is what makes TEXT's height mean what R12 says it means
// -- cap height, not the em box.
//
// Points carry z = 0. The caller places them with the entity's own basis, so a
// glyph knows nothing about which plane it will be drawn in.
struct Glyph {
    // Stroke i spans points[stroke_begin[i]] up to points[stroke_begin[i + 1]],
    // so stroke_begin holds stroke_count + 1 entries. A stroke is a pen-down
    // run; the gaps between them are the pen-ups in the source data.
    const Vec3* points{nullptr};
    const std::uint32_t* stroke_begin{nullptr};
    std::uint32_t stroke_count{0};

    // Where the next glyph's origin sits. This is the side-bearing-to-side-
    // bearing distance, so it is wider than the inked extent -- which is what
    // makes text spacing look right rather than cramped.
    double advance{0.0};
};

// R12's text control codes name three symbols that have no byte in ASCII, so
// they get glyph codes above the table instead. `%%d` is the reason this exists
// at all -- every angular dimension's label ends in one.
//
// The glyphs are DRAWN rather than vendored. The Hershey set does contain them,
// but only in faces we do not carry, and adding a whole `.jhf` for three shapes
// would bring its own attribution and its own parse for no gain. This is the
// same argument `command_icons.cpp` makes: a symbol that is a few strokes costs
// less as strokes than as data.
inline constexpr std::uint16_t kSymbolDegree = 256;
inline constexpr std::uint16_t kSymbolDiameter = 257;
inline constexpr std::uint16_t kSymbolPlusMinus = 258;

// One character of a string after R12's control codes have been resolved: the
// glyph that draws it, and whether the overscore and underscore runs are open
// across it. `%%o` and `%%u` produce no cell of their own -- they are toggles,
// so what they change is the state of the cells that follow.
struct TextCell {
    std::uint16_t code{0};
    bool overscore{false};
    bool underscore{false};
};

// Expands `%%d`, `%%c`, `%%p`, `%%%`, `%%nnn`, `%%o` and `%%u`.
//
// **Decoding happens at LAYOUT time and never at read time.** The entity keeps
// the raw string, exactly as MTEXT keeps its inline codes, so opening a drawing
// and saving it cannot quietly rewrite what the author typed -- and so what we
// write to DXF stays the control code AutoCAD expects rather than a character
// R12 has no way to name.
//
// Shared by `StrokeFont::width` and `draw_text_line` rather than written twice.
// Two copies is how the measured width and the drawn width come to disagree,
// which shows up as justification that is subtly wrong for exactly the strings
// that contain a code -- that is, for every dimension label.
//
// An unrecognised `%%x` is left LITERAL. Unlike MTEXT's codes these carry no
// terminator, so there is no way to tell how much of what follows was meant as
// an argument; consuming it would be guessing, and guessing wrong deletes the
// user's text.
void decode_text(const std::string& text, std::vector<TextCell>& out);

class StrokeFont {
public:
    // Hershey Roman Simplex, the ancestor of R12's `romans`. Parsed once on
    // first use from data embedded in the binary: the core carries no runtime
    // data path, nothing has to be installed alongside the executable, and the
    // tests need no fixtures.
    static const StrokeFont& romans();

    // ASCII 32..126, plus the three `kSymbol*` codes above it. Anything else
    // draws nothing but still advances, so an unmappable code leaves a gap
    // rather than shifting the rest of the line -- the same choice DXF read
    // makes with Proxy, for the same reason: do not silently destroy what we do
    // not understand.
    Glyph glyph(std::uint16_t code) const;

    // Summed advances, over the string's DECODED cells -- so `%%d` measures one
    // degree sign wide and not three characters. EXACT, not a nominal, which is
    // why justification, the bounding box and TEXT's Aligned and Fit modes can
    // all rely on it.
    double width(const std::string& text) const;

    // How far below the baseline the deepest descender reaches, as a positive
    // fraction of cap height. Measured from the font data at parse time rather
    // than assumed, and it is what lets Bottom justification differ from
    // Baseline instead of being fudged together.
    double descender() const { return descender_; }

    StrokeFont(const StrokeFont&) = delete;
    StrokeFont& operator=(const StrokeFont&) = delete;

private:
    // Parses the James Hurt `.jhf` layout. See third_party/hershey/README.md for
    // the format and for the licence conditions that travel with the data.
    explicit StrokeFont(const char* jhf);

    struct Entry {
        std::uint32_t first_stroke{0};
        std::uint32_t stroke_count{0};
        double advance{0.0};
    };

    // The three `%%` symbols, appended to the same point pool the parse fills so
    // that a `Glyph` handed out for one is indistinguishable from a parsed one
    // and the render path needs no branch.
    void build_symbols();

    std::vector<Vec3> points_;
    std::vector<std::uint32_t> stroke_begin_;
    std::vector<Entry> glyphs_;
    Entry symbols_[3];
    double descender_{0.0};
};

class Renderer;

// Where `%%u` and `%%o` put their rules, in cap heights off the baseline. Both
// are chosen to clear the letterforms rather than taken from the R12 manual --
// the underscore does cross a descender, which is what SHX text does too.
inline constexpr double kUnderscoreY = -0.25;
inline constexpr double kOverscoreY = 1.18;

// Emits one line of text as world-space polylines through `r`.
//
// Shared by TEXT and MTEXT rather than written twice, because two copies of the
// glyph placement is how the two would come to disagree about what oblique or a
// width factor means -- and MTEXT's whole degrade rests on its layout matching
// what a run of TEXT entities would draw. `%%` control codes are resolved here
// for the same reason, through the one decoder `width()` also uses.
//
// `origin` is the baseline start, `along` and `up` the line's own axes.
void draw_text_line(const std::string& text, const Vec3& origin, const Vec3& along, const Vec3& up,
                    double height, double width_factor, double oblique, Renderer& r);

// The first character the tables cover. How many follow is the font's business,
// not a constant here -- rowmans holds 96, and a different face may not.
inline constexpr unsigned char kFontFirstChar = 32;

}  // namespace ncad
