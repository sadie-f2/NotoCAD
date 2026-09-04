// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// PLOT: the drawing on paper.
//
// The properties worth pinning are the ones a reader cannot check for you. A
// PDF that opens but has the drawing off the page is not obviously broken, and
// neither is one scaled unevenly -- both look like a plot until measured.

#include "test.hpp"

#include "ncad/command.hpp"
#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/entities.hpp"
#include "ncad/plot.hpp"
#include "ncad/printer_output.hpp"
#include "ncad/sysvar.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace ncad;

namespace {

constexpr double kTol = 1e-9;
constexpr double kPointsPerMm = 72.0 / 25.4;

std::string temp_path(const char* leaf) {
    const std::filesystem::path p = std::filesystem::temp_directory_path() / "ncad_plot_tests";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return (p / leaf).string();
}

void fill_square(Database& db) {
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{100, 0, 0}, Vec3{100, 50, 0}));
    db.add(std::make_unique<Line>(Vec3{100, 50, 0}, Vec3{0, 50, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 50, 0}, Vec3{0, 0, 0}));
}

// Every coordinate in the content stream, as page points. The content is
// `x y m` / `x y l`, one per line, so the pairs can be read straight off.
void stream_points(const std::string& pdf, std::vector<double>& xs, std::vector<double>& ys) {
    xs.clear();
    ys.clear();
    std::size_t at = 0;
    while (true) {
        const std::size_t nl = pdf.find('\n', at);
        if (nl == std::string::npos) break;
        const std::string line = pdf.substr(at, nl - at);
        at = nl + 1;
        if (line.size() < 4) continue;
        const char op = line.back();
        if (op != 'm' && op != 'l') continue;
        // "<x> <y> <op>"
        const std::size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        xs.push_back(std::atof(line.substr(0, sp).c_str()));
        ys.push_back(std::atof(line.substr(sp + 1).c_str()));
    }
}

// The two numbers in `/MediaBox [0 0 w h]`, in points. The sheet a reader will
// actually print on, read back out of the file rather than assumed.
void media_box(const std::string& pdf, double& w, double& h) {
    w = 0.0;
    h = 0.0;
    const std::size_t at = pdf.find("/MediaBox [0 0 ");
    if (at == std::string::npos) return;
    const char* p = pdf.c_str() + at + 15;
    char* end = nullptr;
    w = std::strtod(p, &end);
    h = std::strtod(end, nullptr);
}

// FNV-1a, so a test can pin a whole document to one number. Any hash would do;
// this one is four lines and needs no library.
std::uint64_t fnv1a(const std::string& s) {
    std::uint64_t h = 1469598103934665603ull;
    for (const char raw : s) {
        // Widened through unsigned char explicitly: iterating `s` yields
        // `char`, whose signedness is implementation-defined, and letting the
        // loop variable convert would both warn and make the hash differ
        // between platforms for any byte over 127.
        const auto c = static_cast<unsigned char>(raw);
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

// The drawing the golden hash below is taken over. Fixed on purpose: the hash
// means nothing unless the input is the same every time.
void fill_golden_drawing(Database& db) {
    fill_square(db);
    db.add(std::make_unique<Circle>(Vec3{50, 25, 0}, 20.0));
}

}  // namespace

TEST_CASE("plot: a plan view of a box spans the box") {
    BBox box;
    box.expand(Vec3{0, 0, 0});
    box.expand(Vec3{100, 50, 0});

    const PlotView v = plot_view_for_box(box, kWorldZ);
    CHECK(v.valid());
    CHECK_NEAR(v.width(), 100.0, kTol);
    CHECK_NEAR(v.height(), 50.0, kTol);
}

TEST_CASE("plot: a tilted normal is seen through that plane, not through world XY") {
    // The same arbitrary-axis basis every entity uses for its own plane, so a
    // plot of a tilted UCS comes out the way that plane is drawn.
    BBox box;
    box.expand(Vec3{0, 0, 0});
    box.expand(Vec3{100, 50, 20});

    const PlotView v = plot_view_for_box(box, Vec3{1, 0, 0});
    CHECK(v.valid());
    // Looking down world X, the box's own X extent is depth and contributes
    // nothing to either page axis.
    CHECK(std::abs(v.width() - 100.0) > 1.0);
}

TEST_CASE("plot: a degenerate area is refused rather than divided by") {
    // An empty drawing plotted to Extents, or LIMITS never set. Dividing by
    // zero here would put every point in one place and still write a file.
    CHECK(!plot_view_for_box(BBox{}, kWorldZ).valid());

    BBox flat;
    flat.expand(Vec3{5, 5, 0});
    CHECK(!plot_view_for_box(flat, kWorldZ).valid());

    Database db;
    fill_square(db);
    CHECK(plot_pdf_text(db, PlotView{}, PlotPaper{}).empty());
}

TEST_CASE("plot: paper with no room left is refused") {
    PlotPaper p;
    p.margin_mm = 200.0;  // wider than the sheet
    CHECK(!p.valid());

    Database db;
    fill_square(db);
    BBox box;
    box.expand(Vec3{0, 0, 0});
    box.expand(Vec3{100, 50, 0});
    CHECK(plot_pdf_text(db, plot_view_for_box(box, kWorldZ), p).empty());
}

TEST_CASE("plot: the document is a PDF a reader will accept") {
    Database db;
    fill_square(db);
    BBox box;
    box.expand(Vec3{0, 0, 0});
    box.expand(Vec3{100, 50, 0});
    const std::string pdf = plot_pdf_text(db, plot_view_for_box(box, kWorldZ), PlotPaper{});

    REQUIRE(!pdf.empty());
    CHECK(pdf.compare(0, 8, "%PDF-1.4") == 0);
    CHECK(pdf.find("/Type /Catalog") != std::string::npos);
    CHECK(pdf.find("/Type /Pages") != std::string::npos);
    CHECK(pdf.find("/MediaBox") != std::string::npos);
    CHECK(pdf.find("stream\n") != std::string::npos);
    CHECK(pdf.find("endstream") != std::string::npos);
    // The xref offset must point AT the xref table, or a reader rejects the
    // whole file -- and it is the one part that cannot be right by accident.
    const std::size_t sx = pdf.rfind("startxref\n");
    REQUIRE(sx != std::string::npos);
    const std::size_t declared = std::strtoul(pdf.c_str() + sx + 10, nullptr, 10);
    CHECK(pdf.compare(declared, 4, "xref") == 0);
    CHECK(pdf.find("%%EOF") != std::string::npos);
}

TEST_CASE("plot: everything lands inside the margins") {
    // A plot with the drawing off the page is not obviously broken -- it opens,
    // it just has nothing on it. This is the check a reader cannot do for you.
    Database db;
    fill_square(db);
    BBox box;
    box.expand(Vec3{0, 0, 0});
    box.expand(Vec3{100, 50, 0});

    PlotPaper paper;
    const std::string pdf = plot_pdf_text(db, plot_view_for_box(box, kWorldZ), paper);

    std::vector<double> xs, ys;
    stream_points(pdf, xs, ys);
    REQUIRE(xs.size() >= 8);  // four lines, two points each

    const double lo_x = paper.margin_mm * kPointsPerMm;
    const double hi_x = (paper.width_mm - paper.margin_mm) * kPointsPerMm;
    const double lo_y = paper.margin_mm * kPointsPerMm;
    const double hi_y = (paper.height_mm - paper.margin_mm) * kPointsPerMm;

    for (const double x : xs) {
        CHECK(x >= lo_x - 0.02);
        CHECK(x <= hi_x + 0.02);
    }
    for (const double y : ys) {
        CHECK(y >= lo_y - 0.02);
        CHECK(y <= hi_y + 0.02);
    }
}

TEST_CASE("plot: the fit is uniform, so a square does not come out oblong") {
    // Scaling the axes independently would fill the page and silently lie about
    // every angle and every circle in the drawing.
    Database db;
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{100, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{100, 0, 0}, Vec3{100, 100, 0}));

    BBox box;
    box.expand(Vec3{0, 0, 0});
    box.expand(Vec3{100, 100, 0});
    const std::string pdf = plot_pdf_text(db, plot_view_for_box(box, kWorldZ), PlotPaper{});

    std::vector<double> xs, ys;
    stream_points(pdf, xs, ys);
    REQUIRE(xs.size() == 4);

    // Two equal-length legs of a right angle must plot equal.
    const double leg_a = std::abs(xs[1] - xs[0]);
    const double leg_b = std::abs(ys[3] - ys[2]);
    CHECK_NEAR(leg_a, leg_b, 0.05);
}

TEST_CASE("plot: a landscape drawing on landscape paper is limited by width") {
    // Which of the two ratios binds is worth pinning, because getting it
    // backwards crops rather than shrinks -- and a cropped plot looks fine
    // until the missing edge is noticed.
    Database db;
    // Geometry that actually spans the wide area -- plotting a wide VIEW over a
    // small drawing would leave the geometry in one corner, which is correct
    // and tests nothing.
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1000, 0, 0}));
    db.add(std::make_unique<Line>(Vec3{0, 10, 0}, Vec3{1000, 10, 0}));

    BBox wide;
    wide.expand(Vec3{0, 0, 0});
    wide.expand(Vec3{1000, 10, 0});

    PlotPaper paper;
    const std::string pdf = plot_pdf_text(db, plot_view_for_box(wide, kWorldZ), paper);
    std::vector<double> xs, ys;
    stream_points(pdf, xs, ys);
    REQUIRE(!xs.empty());

    // The drawing is far wider than tall, so it should touch the side margins
    // and leave room top and bottom.
    double max_x = xs[0];
    for (const double x : xs) max_x = std::max(max_x, x);
    CHECK(max_x > (paper.width_mm - paper.margin_mm) * kPointsPerMm - 1.0);
}

// --- the command --------------------------------------------------------------

TEST_CASE("plot command: Extents writes a file") {
    const std::string path = temp_path("extents.pdf");
    std::filesystem::remove(path);

    Database db;
    fill_square(db);
    CommandEngine engine(db);
    engine.begin(make_command("PLOT"));
    engine.supply(InputValue::of_keyword("EXTENTS"));
    engine.supply(InputValue::of_string(path));

    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(std::filesystem::exists(path));
    CHECK(std::filesystem::file_size(path) > 200);
}

TEST_CASE("plot command: Display without a view says so and falls back") {
    // `ncad` has no ViewControl at all. Plotting something else silently would
    // be the lie ViewControl exists to avoid -- PLAN already refuses this way.
    const std::string path = temp_path("display.pdf");
    std::filesystem::remove(path);

    Database db;
    fill_square(db);
    CommandEngine engine(db);
    engine.begin(make_command("PLOT"));
    engine.supply(InputValue::none());  // Enter takes Display
    engine.supply(InputValue::of_string(path));

    REQUIRE(engine.status() == EngineStatus::Finished);
    CHECK(engine.message().find("No display here") != std::string::npos);
    CHECK(std::filesystem::exists(path));
}

TEST_CASE("plot command: Window plots the rectangle that was picked") {
    const std::string path = temp_path("window.pdf");
    std::filesystem::remove(path);

    Database db;
    fill_square(db);
    CommandEngine engine(db);
    engine.begin(make_command("PLOT"));
    engine.supply(InputValue::of_keyword("WINDOW"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({50, 50, 0}));
    engine.supply(InputValue::of_string(path));

    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(std::filesystem::exists(path));
}

TEST_CASE("plot command: an empty drawing is refused, not written") {
    // A blank page is a plausible thing to produce and a useless thing to be
    // handed. Failing beats a file that opens onto nothing.
    const std::string path = temp_path("empty.pdf");
    std::filesystem::remove(path);

    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("PLOT"));
    engine.supply(InputValue::of_keyword("EXTENTS"));

    CHECK(engine.status() == EngineStatus::Failed);
    CHECK(!std::filesystem::exists(path));
}

TEST_CASE("plot command: the name gains .pdf when it has no extension") {
    const std::string path = temp_path("noext");
    std::filesystem::remove(path + ".pdf");

    Database db;
    fill_square(db);
    CommandEngine engine(db);
    engine.begin(make_command("PLOT"));
    engine.supply(InputValue::of_keyword("EXTENTS"));
    engine.supply(InputValue::of_string(path));

    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(std::filesystem::exists(path + ".pdf"));
}

// --- paper, as a setting ------------------------------------------------------

TEST_CASE("plot paper: the named sizes are the millimetres they claim") {
    // Asserted, not trusted. A table of paper sizes is exactly the kind of
    // thing that is right when it is typed and wrong three edits later, and
    // nothing about a plot on a sheet 10 mm out looks wrong on screen.
    std::size_t count = 0;
    const PlotPaperSize* t = plot_paper_sizes(count);
    CHECK(count == 12);

    struct Expected {
        const char* name;
        double w;
        double h;
    };
    // ISO 216 for the A series; 8.5x11 and 11x17 inches exactly, in mm, for
    // the two American ones.
    const Expected want[] = {
        {"A4", 297.0, 210.0},      {"A3", 420.0, 297.0},     {"A2", 594.0, 420.0},
        {"A1", 841.0, 594.0},      {"LETTER", 279.4, 215.9}, {"TABLOID", 431.8, 279.4},
    };
    for (const Expected& e : want) {
        const PlotPaperSize* s = find_plot_paper_size(e.name);
        REQUIRE(s != nullptr);
        CHECK_NEAR(s->width_mm, e.w, kTol);
        CHECK_NEAR(s->height_mm, e.h, kTol);

        // The portrait row of the same sheet is the same paper turned round --
        // a P row with its own numbers would be a second chance to be wrong.
        const PlotPaperSize* portrait = find_plot_paper_size(std::string(e.name) + "P");
        REQUIRE(portrait != nullptr);
        CHECK_NEAR(portrait->width_mm, e.h, kTol);
        CHECK_NEAR(portrait->height_mm, e.w, kTol);
        CHECK(portrait->height_mm > portrait->width_mm);
    }

    // Every landscape row really is landscape, which is what the bare-name
    // convention promises.
    for (std::size_t i = 0; i < count; i += 2) CHECK(t[i].width_mm > t[i].height_mm);

    CHECK(find_plot_paper_size("a3") == find_plot_paper_size("A3"));  // as SETVAR is
    CHECK(find_plot_paper_size("A5") == nullptr);
    CHECK(find_plot_paper_size("A") == nullptr);
    CHECK(find_plot_paper_size("") == nullptr);
    // USER is a sentinel, not a sheet: it has no dimensions of its own.
    CHECK(find_plot_paper_size("USER") == nullptr);
}

TEST_CASE("plot paper: the sysvar defaults ARE the A4 landscape sheet, in both places") {
    // Three copies of one sheet exist -- PlotPaper's member initialisers, the
    // sysvar table's defaults, and the A4 row -- and a plot is only unchanged
    // while all three agree. This is the check that keeps them agreeing.
    const PlotPaperSize* a4 = find_plot_paper_size("A4");
    REQUIRE(a4 != nullptr);

    const PlotPaper hard_coded;
    CHECK_NEAR(hard_coded.width_mm, 297.0, kTol);
    CHECK_NEAR(hard_coded.height_mm, 210.0, kTol);
    CHECK_NEAR(hard_coded.margin_mm, 10.0, kTol);
    CHECK_NEAR(a4->width_mm, hard_coded.width_mm, kTol);
    CHECK_NEAR(a4->height_mm, hard_coded.height_mm, kTol);

    Database db;
    CHECK(db.sysvars().get_string(Sysvar::PaperSize) == "A4");
    const PlotPaper from_vars = plot_paper_from_sysvars(db.sysvars());
    CHECK_NEAR(from_vars.width_mm, hard_coded.width_mm, kTol);
    CHECK_NEAR(from_vars.height_mm, hard_coded.height_mm, kTol);
    CHECK_NEAR(from_vars.margin_mm, hard_coded.margin_mm, kTol);
}

TEST_CASE("plot paper: the A4 default writes the bytes it wrote before paper was settable") {
    // The point of the whole brief: making paper a setting must not change what
    // an unchanged setting produces. `plot.hpp` promises no clock reaches the
    // file, so the document is a pure function of the drawing and the paper and
    // one number can stand for all of it.
    //
    // How the golden was established, since a hash nobody can re-derive is just
    // a number: the pre-change build plotted a fixed drawing to a PDF and it
    // was kept; the post-change build plotted the same drawing and the two
    // files compared byte-for-byte equal. The number below is that document's
    // FNV-1a, and it locks the A4 default from here on. If it moves, the
    // DEFAULT moved -- which is a bug in whatever moved it, not a number to
    // re-take.
    Database db;
    fill_golden_drawing(db);
    const PlotView view = plot_view_for_box(db.extents(), kWorldZ);
    REQUIRE(view.valid());

    const std::string by_default = plot_pdf_text(db, view, PlotPaper{});
    REQUIRE(!by_default.empty());
    CHECK(by_default.size() == 2200);
    CHECK(fnv1a(by_default) == 432629931310244180ull);

    // And the route PLOT now takes -- the sysvars -- lands on the same bytes,
    // which is the actual claim: the setting's default is the old constant.
    const std::string by_sysvars = plot_pdf_text(db, view, plot_paper_from_sysvars(db.sysvars()));
    CHECK(by_sysvars == by_default);
}

TEST_CASE("plot paper: a sheet smaller than its own margins refuses with words") {
    // Reachable by typing now: PAPERMARGIN is a real, and reals here take any
    // value. Before, the only way here was a caller building a PlotPaper by
    // hand. The fit divides by what the margins leave, so this must refuse.
    PlotPaper p;
    p.margin_mm = 200.0;
    CHECK(!p.valid());
    const std::string words = plot_paper_problem(p);
    CHECK(!words.empty());
    // Words, not a shrug: the numbers the user typed have to be IN the message,
    // or they cannot tell which of the four they got wrong.
    CHECK(words.find("297.00") != std::string::npos);
    CHECK(words.find("400.00") != std::string::npos);
    CHECK(words.find("nothing to plot on") != std::string::npos);

    PlotPaper zero;
    zero.width_mm = 0.0;
    CHECK(plot_paper_problem(zero).find("not a sheet") != std::string::npos);

    PlotPaper negative;
    negative.margin_mm = -5.0;
    CHECK(plot_paper_problem(negative).find("negative") != std::string::npos);

    // A good sheet says nothing at all, which is what valid() is asking.
    CHECK(plot_paper_problem(PlotPaper{}).empty());
    CHECK(PlotPaper{}.valid());
}

TEST_CASE("plot paper: write_plot_pdf hands the paper's own words back") {
    Database db;
    fill_square(db);
    PlotPaper p;
    p.margin_mm = 200.0;

    std::string error;
    CHECK(!write_plot_pdf(db, plot_view_for_box(db.extents(), kWorldZ), p, temp_path("bad.pdf"),
                          error));
    CHECK(error.find("margins") != std::string::npos);
}

TEST_CASE("sysvar: PAPERSIZE names a sheet and writes its millimetres") {
    Sysvars sv;
    CHECK(sv.set_string(Sysvar::PaperSize, "A3") == Sysvars::SetStatus::Ok);
    CHECK_NEAR(sv.get_real(Sysvar::PaperWidth), 420.0, kTol);
    CHECK_NEAR(sv.get_real(Sysvar::PaperHeight), 297.0, kTol);

    // Lower case in, canonical out, so (getvar "PAPERSIZE") answers one spelling.
    CHECK(sv.set_string(Sysvar::PaperSize, "letterp") == Sysvars::SetStatus::Ok);
    CHECK(sv.get_string(Sysvar::PaperSize) == "LETTERP");
    CHECK_NEAR(sv.get_real(Sysvar::PaperWidth), 215.9, kTol);
    CHECK_NEAR(sv.get_real(Sysvar::PaperHeight), 279.4, kTol);

    // The margin is nobody's business but its own -- a named size says nothing
    // about how much white the drawing wants round it.
    CHECK_NEAR(sv.get_real(Sysvar::PaperMargin), 10.0, kTol);
}

TEST_CASE("sysvar: an unknown paper size is refused, not quietly turned into A4") {
    // DXFVERSION falls back on a name it does not know, and can afford to: an
    // unrecognised version still writes a valid DXF. A misheard paper size
    // writes the plot on the wrong sheet and says nothing about it.
    Sysvars sv;
    CHECK(sv.set_string(Sysvar::PaperSize, "A5") == Sysvars::SetStatus::OutOfRange);
    CHECK(sv.get_string(Sysvar::PaperSize) == "A4");
    CHECK_NEAR(sv.get_real(Sysvar::PaperWidth), 297.0, kTol);
}

TEST_CASE("sysvar: typing a paper dimension means USER paper") {
    // Otherwise PAPERWIDTH 500 beside PAPERSIZE A3 makes one of the two a lie,
    // whichever one PLOT believed.
    Sysvars sv;
    CHECK(sv.set_string(Sysvar::PaperSize, "A3") == Sysvars::SetStatus::Ok);
    CHECK(sv.set_real(Sysvar::PaperWidth, 500.0) == Sysvars::SetStatus::Ok);
    CHECK(sv.get_string(Sysvar::PaperSize) == "USER");
    CHECK_NEAR(sv.get_real(Sysvar::PaperWidth), 500.0, kTol);
    // The dimension that was NOT typed keeps A3's, because that is the sheet
    // the user was on -- USER means "these two numbers", not "no numbers".
    CHECK_NEAR(sv.get_real(Sysvar::PaperHeight), 297.0, kTol);

    // USER is settable by name too, and changes nothing when it is.
    CHECK(sv.set_string(Sysvar::PaperSize, "user") == Sysvars::SetStatus::Ok);
    CHECK(sv.get_string(Sysvar::PaperSize) == "USER");
    CHECK_NEAR(sv.get_real(Sysvar::PaperWidth), 500.0, kTol);

    // The margin is not a sheet dimension and does not touch the name.
    CHECK(sv.set_string(Sysvar::PaperSize, "A3") == Sysvars::SetStatus::Ok);
    CHECK(sv.set_real(Sysvar::PaperMargin, 25.0) == Sysvars::SetStatus::Ok);
    CHECK(sv.get_string(Sysvar::PaperSize) == "A3");
}

TEST_CASE("setvar: PAPERSIZE is typed at the terminal and PLOT is on that sheet") {
    // The whole path, as a person drives it: SETVAR, then PLOT, then the sheet
    // the PDF declares. Reading the MediaBox back is the only check that spans
    // all of it -- everything short of it can pass with PLOT still on A4.
    const std::string path = temp_path("a3.pdf");
    std::filesystem::remove(path);

    Database db;
    fill_square(db);
    CommandEngine engine(db);

    engine.begin(make_command("SETVAR"));
    engine.supply(InputValue::of_string("PAPERSIZE"));
    engine.supply(InputValue::of_string("A3"));
    REQUIRE(engine.status() == EngineStatus::Finished);

    engine.begin(make_command("PLOT"));
    engine.supply(InputValue::of_keyword("EXTENTS"));
    engine.supply(InputValue::of_string(path));
    REQUIRE(engine.status() == EngineStatus::Finished);
    // The report names the sheet, so a plot on the wrong paper is visible in
    // the transcript rather than only in the file.
    CHECK(engine.message().find("A3") != std::string::npos);

    std::ifstream in(path, std::ios::binary);
    const std::string pdf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    double w = 0.0;
    double h = 0.0;
    media_box(pdf, w, h);
    CHECK_NEAR(w, 420.0 * kPointsPerMm, 0.01);
    CHECK_NEAR(h, 297.0 * kPointsPerMm, 0.01);
}

TEST_CASE("setvar: LETTER is 792 by 612 points, which is 11 by 8.5 inches") {
    // The American sheet, and the one whose numbers are a conversion rather
    // than a definition -- so it is the one worth checking in the unit the PDF
    // is actually written in.
    Database db;
    fill_square(db);
    REQUIRE(db.sysvars().set_string(Sysvar::PaperSize, "LETTER") == Sysvars::SetStatus::Ok);

    const std::string pdf = plot_pdf_text(db, plot_view_for_box(db.extents(), kWorldZ),
                                          plot_paper_from_sysvars(db.sysvars()));
    double w = 0.0;
    double h = 0.0;
    media_box(pdf, w, h);
    CHECK_NEAR(w, 792.0, 0.01);
    CHECK_NEAR(h, 612.0, 0.01);
}

TEST_CASE("plot command: impossible paper is refused BEFORE a file name is asked for") {
    // The same courtesy the empty-area check already gets: being asked for a
    // file name and then told no is the shape that wastes the typing.
    Database db;
    fill_square(db);
    REQUIRE(db.sysvars().set_real(Sysvar::PaperMargin, 400.0) == Sysvars::SetStatus::Ok);

    CommandEngine engine(db);
    engine.begin(make_command("PLOT"));
    engine.supply(InputValue::of_keyword("EXTENTS"));

    CHECK(engine.status() == EngineStatus::Failed);
    CHECK(engine.message().find("nothing to plot on") != std::string::npos);
}

// --- the printer destination --------------------------------------------------

TEST_CASE("plot command: a printer asked for in ncad is refused, not written to a file") {
    // THE case this test exists for, and it is not hypothetical: before the
    // Printer keyword was checked, `(command "PLOT" "E" "Printer")` on a
    // headless session wrote a file called Printer.pdf and reported success.
    // printer_output.hpp says a missing printer must refuse in words rather
    // than fall back to a file, and a silent fallback is worse than a refusal
    // precisely because nobody goes looking for the file it wrote.
    //
    // CommandContext::printer is null here, which is what `ncad` always has.
    Database db;
    fill_square(db);

    CommandEngine engine(db);
    engine.begin(make_command("PLOT"));
    engine.supply(InputValue::of_keyword("EXTENTS"));
    REQUIRE(engine.status() == EngineStatus::Waiting);
    engine.supply(InputValue::of_string("Printer"));

    CHECK(engine.status() == EngineStatus::Failed);
    CHECK(engine.message().find("no printer here") != std::string::npos);
}

TEST_CASE("plot command: the KEYWORD route reaches the printer, not just a string") {
    // The gap that shipped a broken window. `match_keyword` upcases what it
    // returns, so a matched keyword arrives as "PRINTER" -- and the check
    // compared against "Printer", matched nothing, and fell through to "a file
    // name is required". Every test here supplied a STRING, which takes the
    // other branch, and the terminal declares no keyword to match, so nothing
    // in the suite or the CLI ever exercised the path the GUI uses.
    //
    // Supplied the way the parser really produces it.
    struct StubPrinter final : PrinterOutput {
        bool print(const Database&, const PlotView&, PlotPaper&, std::string& error) override {
            error = "stub printer reached";
            return false;
        }
    } printer;

    Database db;
    fill_square(db);
    CommandEngine engine(db);
    engine.set_printer(&printer);
    engine.begin(make_command("PLOT"));
    engine.supply(InputValue::of_keyword("EXTENTS"));
    REQUIRE(engine.status() == EngineStatus::Waiting);

    engine.supply(InputValue::of_keyword("PRINTER"));
    // It reached the printer: the stub's own words come back, NOT the file
    // prompt's complaint.
    CHECK(engine.message().find("stub printer reached") != std::string::npos);
    CHECK(engine.message().find("a file name is required") == std::string::npos);
    if (engine.message().find("a file name is required") != std::string::npos) {
        std::printf("       the keyword route fell through to the file prompt again\n");
    }
}

TEST_CASE("plot command: the printer keyword is matched whatever its case") {
    // A LISP routine writes whatever it writes. Matching only "Printer" would
    // send "printer" off to be a file name, which is the very fallback the
    // test above exists to prevent.
    for (const char* spelling : {"printer", "PRINTER", "Printer", "PrInTeR"}) {
        Database db;
        fill_square(db);
        CommandEngine engine(db);
        engine.begin(make_command("PLOT"));
        engine.supply(InputValue::of_keyword("EXTENTS"));
        REQUIRE(engine.status() == EngineStatus::Waiting);
        engine.supply(InputValue::of_string(spelling));
        CHECK(engine.status() == EngineStatus::Failed);
        if (engine.status() != EngineStatus::Failed) {
            std::printf("       \"%s\" was taken as a file name\n", spelling);
        }
    }
}

TEST_CASE("plot command: ncad's prompt sequence is unchanged by the printer existing") {
    // The compatibility promise. Every script and LISP routine written before
    // printers says (command "PLOT" "E" "out.pdf"), and a destination menu in
    // front of the name would break all of them. With no printer the prompt
    // does not even mention one -- ncad must not advertise what it cannot do.
    Database db;
    fill_square(db);

    CommandEngine engine(db);
    engine.begin(make_command("PLOT"));
    engine.supply(InputValue::of_keyword("EXTENTS"));

    REQUIRE(engine.status() == EngineStatus::Waiting);
    CHECK(engine.prompt().message == "Plot to file");
    CHECK(engine.prompt().keywords.empty());
    CHECK(engine.prompt().file == FileIntent::Save);
}

TEST_CASE("plot: a sheet chosen elsewhere comes back through the sysvars by name") {
    // plot_paper_to_sysvars is how a print dialog's page setup reaches the
    // rest of the program: the choice is made once, where SETVAR and the next
    // plot already read it. A known sheet is NAMED rather than left as USER
    // with the right numbers under it.
    Database db;
    PlotPaper paper;
    paper.width_mm = 420.0;
    paper.height_mm = 297.0;
    paper.margin_mm = 12.0;
    plot_paper_to_sysvars(db.sysvars(), paper);

    CHECK(db.sysvars().get_string(Sysvar::PaperSize) == "A3");
    CHECK_NEAR(db.sysvars().get_real(Sysvar::PaperWidth), 420.0, kTol);
    CHECK_NEAR(db.sysvars().get_real(Sysvar::PaperMargin), 12.0, kTol);

    // And a sheet the table does not hold keeps its numbers and says USER,
    // because the numbers are the truth -- the pairing rule in sysvar.hpp.
    PlotPaper odd;
    odd.width_mm = 333.0;
    odd.height_mm = 222.0;
    odd.margin_mm = 5.0;
    plot_paper_to_sysvars(db.sysvars(), odd);
    CHECK(db.sysvars().get_string(Sysvar::PaperSize) == "USER");
    CHECK_NEAR(db.sysvars().get_real(Sysvar::PaperWidth), 333.0, kTol);
}

TEST_CASE("plot command: with a printer, the prompt carries the keyword the window needs") {
    // The contract the GUI's destination dialog rests on. It decides whether
    // to ask "printer or file?" by looking for `Printer` among the standing
    // prompt's keywords -- so if this list ever stops carrying it, the window
    // silently loses its only route to a printer while the terminal keeps
    // working. That failure would not show up in any GUI-free test, which is
    // exactly why it is pinned here.
    struct StubPrinter final : PrinterOutput {
        bool print(const Database&, const PlotView&, PlotPaper&, std::string&) override {
            return true;
        }
    } printer;

    Database db;
    fill_square(db);
    CommandEngine engine(db);
    engine.set_printer(&printer);
    engine.begin(make_command("PLOT"));
    engine.supply(InputValue::of_keyword("EXTENTS"));

    REQUIRE(engine.status() == EngineStatus::Waiting);
    CHECK(engine.prompt().file == FileIntent::Save);  // still a file prompt
    bool offers_printer = false;
    for (const std::string& k : engine.prompt().keywords) {
        if (k == "Printer") offers_printer = true;
    }
    CHECK(offers_printer);
    if (!offers_printer) {
        std::printf("       the PLOT prompt no longer offers Printer; the GUI cannot reach one\n");
    }
}
