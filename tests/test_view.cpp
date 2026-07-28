// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/commands.hpp"
#include "noto/database.hpp"
#include "noto/entities.hpp"
#include "noto/view_control.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace noto;

namespace {

// A ViewControl that records what it was asked to do. The whole point of the
// interface being abstract: the view commands are testable with no display,
// exactly as the rest of the core is.
class RecordingView final : public ViewControl {
public:
    void set_plan_view(const Vec3& normal) override {
        calls.push_back("plan");
        last_normal = normal;
    }
    void zoom_extents() override { calls.push_back("extents"); }
    void zoom_window(const Vec3& a, const Vec3& b) override {
        calls.push_back("window");
        first = a;
        second = b;
    }
    void zoom_scale(double f) override {
        calls.push_back("scale");
        factor = f;
    }
    bool zoom_previous() override {
        calls.push_back("previous");
        return has_previous;
    }
    void pan(const Vec3& a, const Vec3& b) override {
        calls.push_back("pan");
        first = a;
        second = b;
    }
    void set_view_direction(const Vec3& d) override {
        calls.push_back("vpoint");
        direction = d;
    }
    Vec3 view_direction() const override { return direction; }
    Basis view_basis() const override { return Basis{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; }
    DrawContext draw_context() const override { return DrawContext{}; }

    std::vector<std::string> calls;
    Vec3 direction{0, 0, 1};
    Vec3 last_normal{};
    Vec3 first{};
    Vec3 second{};
    double factor{0.0};
    bool has_previous{true};
};

}  // namespace

TEST_CASE("plan: sets the view down the construction plane normal") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    engine.begin(make_command("PLAN"));
    engine.supply(InputValue::none());  // Enter accepts the default
    CHECK(engine.status() == EngineStatus::Finished);

    CHECK(view.calls.size() == 1);
    CHECK(view.calls[0] == "plan");
    // World Z until UCS exists.
    CHECK_VEC(view.last_normal, 0.0, 0.0, 1.0, 1e-12);
}

TEST_CASE("plan: all three answers mean the same thing for now") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    for (const char* answer : {"CURRENT", "UCS", "WORLD"}) {
        engine.begin(make_command("PLAN"));
        engine.supply(InputValue::of_keyword(answer));
        CHECK(engine.status() == EngineStatus::Finished);
    }
    // The prompt is right from the start; phase 12 fills in the difference
    // rather than adding a question that was not there before.
    CHECK(view.calls.size() == 3);
}

TEST_CASE("plan: without a view it says so rather than reporting success") {
    Database db;
    CommandEngine engine(db);
    // No view control set, which is exactly `ncad`'s situation.
    CHECK(engine.view_control() == nullptr);

    engine.begin(make_command("PLAN"));
    engine.supply(InputValue::none());

    // Reporting "Regenerating drawing." with no drawing regenerated would be a
    // lie that a script could not detect.
    CHECK(engine.status() == EngineStatus::Failed);
    CHECK(engine.message().find("no view") != std::string::npos);
}

TEST_CASE("plan: rejects an answer that is not one of the three") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    engine.begin(make_command("PLAN"));
    engine.supply(InputValue::of_point({1, 2, 3}));
    CHECK(engine.status() == EngineStatus::Failed);
    CHECK(view.calls.empty());
}

TEST_CASE("plan: changing the view is not an undo step") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);
    db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{1, 0, 0}));
    const std::size_t before = db.journal().undo_depth();

    engine.begin(make_command("PLAN"));
    engine.supply(InputValue::none());

    // Looking at a drawing differently does not change it. R12 has ZOOM
    // Previous for the view, which is a separate history from UNDO.
    CHECK(db.journal().undo_depth() == before);
}

TEST_CASE("view: the context carries the control, and it is optional") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;

    CHECK(engine.view_control() == nullptr);
    engine.set_view_control(&view);
    CHECK(engine.view_control() == &view);
    engine.set_view_control(nullptr);
    CHECK(engine.view_control() == nullptr);
}

TEST_CASE("zoom: extents, scale and previous") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    engine.begin(make_command("ZOOM"));
    engine.supply(InputValue::of_keyword("EXTENTS"));
    CHECK(view.calls.back() == "extents");

    engine.begin(make_command("ZOOM"));
    engine.supply(InputValue::of_real(2.0));
    CHECK(view.calls.back() == "scale");
    CHECK_NEAR(view.factor, 2.0, 1e-12);

    engine.begin(make_command("ZOOM"));
    engine.supply(InputValue::of_keyword("PREVIOUS"));
    CHECK(view.calls.back() == "previous");
}

TEST_CASE("zoom: previous reports when there is nothing to go back to") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    view.has_previous = false;
    engine.set_view_control(&view);

    engine.begin(make_command("ZOOM"));
    engine.supply(InputValue::of_keyword("PREVIOUS"));
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(engine.message() == "No previous view");
}

TEST_CASE("zoom: a window takes two corners") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    engine.begin(make_command("ZOOM"));
    engine.supply(InputValue::of_keyword("WINDOW"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    CHECK(engine.prompt().has_base);  // rubber-bands from the first corner
    engine.supply(InputValue::of_point({10, 5, 0}));

    CHECK(view.calls.back() == "window");
    CHECK_VEC(view.first, 0.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(view.second, 10.0, 5.0, 0.0, 1e-12);
}

TEST_CASE("zoom: a negative or zero scale is refused") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    engine.begin(make_command("ZOOM"));
    engine.supply(InputValue::of_real(0.0));
    CHECK(engine.status() == EngineStatus::Failed);
    CHECK(view.calls.empty());
}

TEST_CASE("pan: two points give a displacement") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    engine.begin(make_command("PAN"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({5, 5, 0}));

    CHECK(view.calls.back() == "pan");
    CHECK_VEC(view.first, 0.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(view.second, 5.0, 5.0, 0.0, 1e-12);
}

TEST_CASE("transparent: 'ZOOM inside LINE hands the prompt back") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    const std::string outer = engine.prompt().text();
    CHECK(engine.prompt().has_base);  // LINE is rubber-banding from the first point

    engine.begin_transparent(make_command("ZOOM"));
    CHECK(engine.in_transparent());
    CHECK(engine.active());
    CHECK(engine.prompt().text() != outer);  // ZOOM is asking now

    engine.supply(InputValue::of_keyword("EXTENTS"));
    CHECK(!engine.in_transparent());
    CHECK(engine.active());

    // The original question comes back word for word, still rubber-banding.
    CHECK(engine.prompt().text() == outer);
    CHECK(engine.prompt().has_base);

    // And LINE finishes as if nothing had happened.
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::none());
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(db.size() == 1);
}

TEST_CASE("transparent: escaping a transparent command spares the outer one") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    const std::string outer = engine.prompt().text();

    engine.begin_transparent(make_command("ZOOM"));
    engine.supply(InputValue::cancel());

    // Escape was about the view, so it must not cost the line.
    CHECK(!engine.in_transparent());
    CHECK(engine.active());
    CHECK(engine.prompt().text() == outer);
}

TEST_CASE("transparent: a failing transparent command does not take the outer one down") {
    Database db;
    CommandEngine engine(db);
    // No view: ZOOM will fail.
    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    const std::string outer = engine.prompt().text();

    engine.begin_transparent(make_command("ZOOM"));
    engine.supply(InputValue::of_keyword("EXTENTS"));

    CHECK(engine.active());
    CHECK(engine.prompt().text() == outer);
}

TEST_CASE("transparent: with nothing running it is just a command") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    engine.begin_transparent(make_command("ZOOM"));
    CHECK(!engine.in_transparent());
    CHECK(engine.active());
    engine.supply(InputValue::of_keyword("EXTENTS"));
    CHECK(engine.status() == EngineStatus::Finished);
}

TEST_CASE("transparent: only commands that change nothing qualify") {
    CHECK(command_is_transparent("ZOOM"));
    CHECK(command_is_transparent("PAN"));
    CHECK(command_is_transparent("PLAN"));
    CHECK(command_is_transparent("zoom"));

    // ERASE would be very useful mid-command and must never be transparent:
    // the outer command may be holding handles it would invalidate.
    CHECK(!command_is_transparent("ERASE"));
    CHECK(!command_is_transparent("LINE"));
    CHECK(!command_is_transparent("MOVE"));
    CHECK(!command_is_transparent("UNDO"));
}

TEST_CASE("transparent: a transparent command adds no undo step") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.begin_transparent(make_command("ZOOM"));
    engine.supply(InputValue::of_keyword("EXTENTS"));
    engine.supply(InputValue::of_point({10, 0, 0}));
    engine.supply(InputValue::none());

    // One LINE, one undo step -- the 'ZOOM sits inside it and records nothing.
    CHECK(db.journal().undo_depth() == 1);
    CHECK(db.journal().undo(db));
    CHECK(db.size() == 0);
}

TEST_CASE("transparent: nesting is refused rather than stacked") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.begin_transparent(make_command("ZOOM"));

    const std::string zoom_prompt = engine.prompt().text();
    engine.begin_transparent(make_command("PAN"));
    // Still ZOOM: one level covers what this is for, and a stack would need an
    // answer for what Escape means at depth three.
    CHECK(engine.prompt().text() == zoom_prompt);
}

// --- VPOINT -----------------------------------------------------------------
//
// Held back until UCS existed, because the answer is read in the current
// coordinate system. That is the whole reason it is here rather than in phase 6,
// and it is what most of these pin.

TEST_CASE("vpoint: a typed direction sets the view") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    engine.begin(make_command("VPOINT"));
    engine.supply(InputValue::of_point({1, 0, 0}));

    CHECK(view.calls.size() == 1);
    CHECK(view.calls[0] == "vpoint");
    CHECK(near_equal(normalize(view.direction), Vec3{1, 0, 0}, 1e-9));
}

TEST_CASE("vpoint: the same numbers mean a different view under a different UCS") {
    // The reason this command waited for UCS. `1,0,0` is a direction in the
    // current system, so standing the construction plane up rotates what it
    // names -- and a version written against WCS would have had to be written
    // again.
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    // World first, as the baseline.
    engine.begin(make_command("VPOINT"));
    engine.supply(InputValue::of_point({1, 0, 0}));
    CHECK(near_equal(normalize(view.direction), Vec3{1, 0, 0}, 1e-9));

    // Now a UCS whose X axis is world Y. Supplied the way the parser would
    // supply it -- a typed coordinate reaches the command already mapped into
    // world, and driving the command with raw numbers would test a pipeline
    // that does not exist.
    Ucs u;
    u.xdir = {0, 1, 0};
    u.ydir = {-1, 0, 0};
    db.set_current_ucs(u);

    engine.begin(make_command("VPOINT"));
    engine.supply(InputValue::of_point(db.current_ucs().to_world().transform_point({1, 0, 0})));
    CHECK(near_equal(normalize(view.direction), Vec3{0, 1, 0}, 1e-9));
}

TEST_CASE("vpoint: a direction is rotated, not translated") {
    // A UCS origin far from world must not move a view DIRECTION, which has no
    // origin to be moved from.
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    Ucs u;
    u.origin = {1000, 2000, 3000};
    db.set_current_ucs(u);

    engine.begin(make_command("VPOINT"));
    engine.supply(InputValue::of_point(db.current_ucs().to_world().transform_point({0, 0, 1})));
    CHECK(near_equal(normalize(view.direction), Vec3{0, 0, 1}, 1e-9));
}

TEST_CASE("vpoint: Rotate takes two angles instead of a vector") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    engine.begin(make_command("VPOINT"));
    engine.supply(InputValue::of_keyword("ROTATE"));
    engine.supply(InputValue::of_real(90.0));  // round from the X axis
    engine.supply(InputValue::of_real(0.0));   // level with the XY plane

    CHECK(near_equal(normalize(view.direction), Vec3{0, 1, 0}, 1e-9));
}

TEST_CASE("vpoint: Rotate straight up is the plan view") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    engine.begin(make_command("VPOINT"));
    engine.supply(InputValue::of_keyword("ROTATE"));
    engine.supply(InputValue::of_real(0.0));
    engine.supply(InputValue::of_real(90.0));

    CHECK(near_equal(normalize(view.direction), Vec3{0, 0, 1}, 1e-9));
}

TEST_CASE("vpoint: Enter reports rather than moving the camera") {
    // R12 shows a compass and axis tripod here. There is not one, and inventing
    // a default would silently change the view.
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    engine.begin(make_command("VPOINT"));
    engine.supply(InputValue::none());

    CHECK(view.calls.empty());
    CHECK(!engine.message().empty());
}

TEST_CASE("vpoint: with no view it says so rather than reporting success") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("VPOINT"));
    const EngineStatus status = engine.supply(InputValue::of_point({1, 1, 1}));
    CHECK(status == EngineStatus::Failed);
}

TEST_CASE("vpoint: a zero direction is refused") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    engine.begin(make_command("VPOINT"));
    const EngineStatus status = engine.supply(InputValue::of_point({0, 0, 0}));
    CHECK(status == EngineStatus::Failed);
}

TEST_CASE("plan: a tilted UCS gets a plan view of its own plane, not of world XY") {
    // set_plan_view() ignored its argument while every construction plane was
    // world XY. Now that they are not, honouring it is the whole of what PLAN
    // in a UCS means.
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    Ucs u;
    u.xdir = {1, 0, 0};
    u.ydir = {0, 0, 1};  // the XZ plane, facing -Y
    db.set_current_ucs(u);

    engine.begin(make_command("PLAN"));
    engine.supply(InputValue::of_keyword("CURRENT"));

    CHECK(near_equal(view.last_normal, Vec3{0, -1, 0}, 1e-9));
}

TEST_CASE("plan: World still means world, whatever the UCS is") {
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    Ucs u;
    u.xdir = {1, 0, 0};
    u.ydir = {0, 0, 1};
    db.set_current_ucs(u);

    engine.begin(make_command("PLAN"));
    engine.supply(InputValue::of_keyword("WORLD"));

    CHECK(near_equal(view.last_normal, kWorldZ, 1e-9));
}

TEST_CASE("vpoint: a picked point is read as its UCS coordinates") {
    // A click arrives in world, not in the UCS. R12 reads the picked point's
    // UCS coordinates as the direction, which is the same arithmetic a typed
    // answer gets -- and is why the command does not simply use the world
    // point as the direction.
    Database db;
    CommandEngine engine(db);
    RecordingView view;
    engine.set_view_control(&view);

    Ucs u;
    u.origin = {10, 0, 0};
    db.set_current_ucs(u);

    engine.begin(make_command("VPOINT"));
    // A world point one unit up from the UCS origin.
    engine.supply(InputValue::of_point({10, 0, 1}));
    CHECK(near_equal(normalize(view.direction), Vec3{0, 0, 1}, 1e-9));
}
