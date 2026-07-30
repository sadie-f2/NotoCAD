// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// MTEXT: the paragraph entity.
//
// The design rule under test throughout: the RAW string is what is held, and
// everything visible is derived from it. Stripping the inline codes at read
// time would be cheaper and would destroy the entity -- a file opened and saved
// would lose its formatting for good, which is the round trip that already bit
// us on ellipses.

#include "test.hpp"

#include "noto/database.hpp"
#include "noto/dxf.hpp"
#include "noto/dxf_read.hpp"
#include "noto/entities.hpp"
#include "noto/font.hpp"
#include "noto/render.hpp"

#include <cmath>
#include <memory>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

using namespace noto;

namespace {

constexpr double kTol = 1e-9;

class Capture final : public Renderer {
public:
    void begin_entity(const EntityProps&) override {}
    void polyline(const Vec3* pts, std::size_t count, bool closed) override {
        runs.push_back(std::vector<Vec3>(pts, pts + count));
        closed_flags.push_back(closed);
    }
    std::vector<std::vector<Vec3>> runs;
    std::vector<bool> closed_flags;
};

std::vector<std::string> lines_of(const MText& m) {
    std::vector<std::string> out;
    m.layout(out);
    return out;
}

// DXF is written with CRLF line endings, which is correct and which every
// search here would otherwise miss. Normalised once, so the tests below can be
// read as the file reads.
std::string dxf_of(const Database& db) {
    std::ostringstream out;
    DxfWriter w(out, db);
    w.write_document();

    std::string s = out.str();
    std::string flat;
    flat.reserve(s.size());
    for (const char c : s) {
        if (c != '\r') flat.push_back(c);
    }
    return flat;
}

std::size_t count_of(const std::string& hay, const std::string& needle) {
    std::size_t n = 0;
    for (std::size_t i = hay.find(needle); i != std::string::npos; i = hay.find(needle, i + 1)) ++n;
    return n;
}

}  // namespace

// --- Layout -----------------------------------------------------------------

TEST_CASE("mtext: inline codes are consumed with their arguments") {
    // The failure that would actually look broken is an argument leaking into
    // the visible text -- "2.5xbig" rather than "big".
    const MText m({0, 0, 0}, "{\\fArial|b1;Bold} start \\H2.5x;big", 1.0);
    const std::vector<std::string> lines = lines_of(m);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "Bold start big");
}

TEST_CASE("mtext: a paragraph break makes a line and the raw string keeps it") {
    const MText m({0, 0, 0}, "one\\Ptwo\\Pthree", 1.0);
    const std::vector<std::string> lines = lines_of(m);

    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "one");
    CHECK(lines[2] == "three");

    // The point of the whole design: what is held is unchanged.
    CHECK(m.text() == "one\\Ptwo\\Pthree");
}

TEST_CASE("mtext: escapes and a stacked fraction survive as readable text") {
    const MText m({0, 0, 0}, "\\S1^2; and a literal \\\\ and \\{brace\\}", 1.0);
    const std::vector<std::string> lines = lines_of(m);

    REQUIRE(lines.size() == 1);
    // Flat rather than stacked, because there is one font at one height -- but
    // deleting it would silently lose a dimension.
    CHECK(lines[0] == "1/2 and a literal \\ and {brace}");
}

TEST_CASE("mtext: wrapping uses the real font metrics and honours the width") {
    const MText m({0, 0, 0}, "aaaa bbbb cccc dddd eeee ffff", 1.0, 10.0);
    const std::vector<std::string> lines = lines_of(m);

    CHECK(lines.size() > 1);

    const StrokeFont& font = StrokeFont::romans();
    for (const std::string& l : lines) {
        // Every line fits, which is the property wrapping has to have.
        CHECK(font.width(l) * m.height() <= 10.0 + 1e-9);
        // And no line begins with the space it was broken at.
        CHECK(l.empty() || l[0] != ' ');
    }
}

TEST_CASE("mtext: a zero reference width means no wrapping at all") {
    const MText m({0, 0, 0}, "a very long line that would certainly wrap if it could", 1.0, 0.0);
    CHECK(lines_of(m).size() == 1);
}

TEST_CASE("mtext: a word longer than the width still gets its own line") {
    // It cannot fit and there is nowhere to break it, so the honest answer is to
    // overflow rather than to loop or to drop it.
    const MText m({0, 0, 0}, "supercalifragilistic", 1.0, 2.0);
    const std::vector<std::string> lines = lines_of(m);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "supercalifragilistic");
}

// --- Placement --------------------------------------------------------------

TEST_CASE("mtext: the attachment point decides where the block hangs") {
    const std::string body = "one\\Ptwo";

    MText top({0, 0, 0}, body, 1.0);
    top.set_attach(MTextAttach::TopLeft);
    MText bottom({0, 0, 0}, body, 1.0);
    bottom.set_attach(MTextAttach::BottomLeft);

    // TopLeft hangs the block below the insertion point, BottomLeft above it.
    CHECK(top.baseline_origin().y < 0.0);
    CHECK(bottom.baseline_origin().y > 0.0);

    MText right({0, 0, 0}, body, 1.0);
    right.set_attach(MTextAttach::TopRight);
    CHECK(right.baseline_origin().x < 0.0);
}

TEST_CASE("mtext: the bounding box covers every line") {
    MText m({0, 0, 0}, "one\\Ptwo\\Pthree", 2.0);
    const BBox box = m.bbox();
    CHECK(box.valid());

    // Three lines at 2.0 with 5/3 spacing span two line heights plus the cap
    // height of the last one, and then some descender below it.
    CHECK(box.max.y - box.min.y > 2.0 * m.line_height());
    CHECK(box.max.x - box.min.x > 0.0);
}

TEST_CASE("mtext: transform scales the reference width with the text") {
    MText m({0, 0, 0}, "wrap me please", 1.0, 10.0);
    const std::size_t before = lines_of(m).size();

    m.transform(Mat4::uniform_scaling(3.0));

    CHECK_NEAR(m.height(), 3.0, kTol);
    CHECK_NEAR(m.reference_width(), 30.0, kTol);
    // Scaling a paragraph must not rewrap it, or a scaled drawing changes shape.
    CHECK(lines_of(m).size() == before);
}

TEST_CASE("mtext: clone carries everything that is not geometry") {
    MText m({1, 2, 3}, "a\\Pb", 2.5, 12.0);
    m.set_attach(MTextAttach::MiddleCenter);
    m.set_line_spacing(1.5);
    m.set_rotation(0.4);

    const EntityPtr c = m.clone();
    REQUIRE(c->type() == EntityType::MText);
    const MText& copy = static_cast<const MText&>(*c);

    CHECK(copy.text() == "a\\Pb");
    CHECK_NEAR(copy.height(), 2.5, kTol);
    CHECK_NEAR(copy.reference_width(), 12.0, kTol);
    CHECK(copy.attach() == MTextAttach::MiddleCenter);
    CHECK_NEAR(copy.line_spacing(), 1.5, kTol);
    CHECK_NEAR(copy.rotation(), 0.4, kTol);
}

// --- Drawing ----------------------------------------------------------------

TEST_CASE("mtext: every line is drawn, each lower than the last") {
    const MText m({0, 0, 0}, "AAA\\PBBB\\PCCC", 2.0);
    Capture cap;
    m.draw(DrawContext{}, cap);

    CHECK(cap.runs.size() >= 9);  // three letters a line, several strokes each
    for (std::size_t i = 0; i < cap.runs.size(); ++i) CHECK(!cap.closed_flags[i]);

    // Three distinct bands of y, one per line.
    double lo = 1e30, hi = -1e30;
    for (const auto& run : cap.runs) {
        for (const Vec3& p : run) {
            lo = std::min(lo, p.y);
            hi = std::max(hi, p.y);
        }
    }
    CHECK(hi - lo > 2.0 * m.line_height() - 1e-6);
}

TEST_CASE("mtext: an empty paragraph draws nothing but does not misbehave") {
    const MText m({0, 0, 0}, "", 2.0);
    Capture cap;
    m.draw(DrawContext{}, cap);
    CHECK(cap.runs.empty());
    CHECK(lines_of(m).size() == 1);
}

// --- DXF --------------------------------------------------------------------

TEST_CASE("mtext: DXF read takes group 3 chunks before group 1") {
    // Text over 250 characters is split across repeated group 3 records with the
    // tail in group 1. Reading only group 1 silently truncates every long
    // paragraph -- a loss that looks like the file was wrong rather than us.
    Database db;
    const DxfReadResult r = read_dxf_text(db,
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nMTEXT\n100\nAcDbEntity\n  8\n0\n100\nAcDbMText\n"
        " 10\n0.0\n 20\n0.0\n 30\n0.0\n 40\n1.0\n 41\n0.0\n 71\n1\n"
        "  3\nfirst part \n  3\nsecond part \n  1\nand the tail\n"
        "  0\nENDSEC\n  0\nEOF\n");

    CHECK(r.ok);
    CHECK(r.proxies == 0);
    REQUIRE(db.order().size() == 1);

    const Entity* e = db.get(db.order()[0]);
    REQUIRE(e->type() == EntityType::MText);
    CHECK(static_cast<const MText&>(*e).text() == "first part second part and the tail");
}

TEST_CASE("mtext: DXF read carries height, width, attachment and spacing") {
    Database db;
    read_dxf_text(db,
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nMTEXT\n100\nAcDbEntity\n  8\n0\n100\nAcDbMText\n"
        " 10\n3.0\n 20\n4.0\n 30\n0.0\n 40\n2.5\n 41\n30.0\n 71\n5\n 44\n1.5\n"
        " 50\n90.0\n  1\nhello\n"
        "  0\nENDSEC\n  0\nEOF\n");

    REQUIRE(db.order().size() == 1);
    const MText& m = static_cast<const MText&>(*db.get(db.order()[0]));

    CHECK_VEC(m.position(), 3.0, 4.0, 0.0, 1e-12);
    CHECK_NEAR(m.height(), 2.5, 1e-12);
    CHECK_NEAR(m.reference_width(), 30.0, 1e-12);
    CHECK(m.attach() == MTextAttach::MiddleCenter);
    CHECK_NEAR(m.line_spacing(), 1.5, 1e-12);
    CHECK_NEAR(m.rotation(), std::numbers::pi * 0.5, 1e-12);
}

TEST_CASE("mtext: DXF write degrades to one TEXT record per laid-out line") {
    Database db;
    db.add(std::make_unique<MText>(Vec3{0, 0, 0}, "alpha\\Pbeta\\Pgamma", 1.0));

    const std::string out = dxf_of(db);
    // AC1009 has no MTEXT, so nothing may claim to be one.
    CHECK(out.find("MTEXT") == std::string::npos);
    // Three lines, three records, each carrying its own string.
    CHECK(count_of(out, "\nTEXT\n") == 3);
    CHECK(out.find("alpha") != std::string::npos);
    CHECK(out.find("beta") != std::string::npos);
    CHECK(out.find("gamma") != std::string::npos);
}

TEST_CASE("mtext: what we write, we read back as separate lines -- and that is honest") {
    // The bargain, pinned rather than assumed: a round trip through OUR DXF is
    // lossy, because R12 cannot name the entity. What AutoCAD writes we read as
    // one MText; what we write comes back as the lines it degraded to.
    Database db;
    db.add(std::make_unique<MText>(Vec3{0, 0, 0}, "one\\Ptwo", 1.0));
    const std::string out = dxf_of(db);

    Database back;
    const DxfReadResult r = read_dxf_text(back, out);
    CHECK(r.ok);

    std::size_t texts = 0;
    for (const Handle h : back.order()) {
        if (back.get(h)->type() == EntityType::Text) ++texts;
        CHECK(back.get(h)->type() != EntityType::MText);
    }
    CHECK(texts == 2);
}
