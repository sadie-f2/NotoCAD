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
    Basis view_basis() const override { return Basis{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; }
    DrawContext draw_context() const override { return DrawContext{}; }

    std::vector<std::string> calls;
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
