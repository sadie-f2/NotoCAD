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

#include <cmath>
#include <cstdlib>
#include <filesystem>
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
