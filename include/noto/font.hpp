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

#include "noto/vec3.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace noto {

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

class StrokeFont {
public:
    // Hershey Roman Simplex, the ancestor of R12's `romans`. Parsed once on
    // first use from data embedded in the binary: the core carries no runtime
    // data path, nothing has to be installed alongside the executable, and the
    // tests need no fixtures.
    static const StrokeFont& romans();

    // ASCII 32..126. Anything outside the table draws nothing but still
    // advances, so an unmappable byte leaves a gap rather than shifting the rest
    // of the line -- the same choice DXF read makes with Proxy, for the same
    // reason: do not silently destroy what we do not understand.
    Glyph glyph(unsigned char c) const;

    // Summed advances. EXACT, not a nominal -- which is why justification, the
    // bounding box and TEXT's Aligned and Fit modes can all rely on it.
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

    std::vector<Vec3> points_;
    std::vector<std::uint32_t> stroke_begin_;
    std::vector<Entry> glyphs_;
    double descender_{0.0};
};

class Renderer;

// Emits one line of text as world-space polylines through `r`.
//
// Shared by TEXT and MTEXT rather than written twice, because two copies of the
// glyph placement is how the two would come to disagree about what oblique or a
// width factor means -- and MTEXT's whole degrade rests on its layout matching
// what a run of TEXT entities would draw.
//
// `origin` is the baseline start, `along` and `up` the line's own axes.
void draw_text_line(const std::string& text, const Vec3& origin, const Vec3& along, const Vec3& up,
                    double height, double width_factor, double oblique, Renderer& r);

// The first character the tables cover. How many follow is the font's business,
// not a constant here -- rowmans holds 96, and a different face may not.
inline constexpr unsigned char kFontFirstChar = 32;

}  // namespace noto
