// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/commands.hpp"
#include "noto/database.hpp"
#include "noto/entities.hpp"
#include "noto/sysvar.hpp"

#include <memory>
#include <string>

using namespace noto;

namespace {

bool is_option(const std::string& s) {
    return s == "?" || s == "MAKE" || s == "SET" || s == "NEW" || s == "ON" || s == "OFF" ||
           s == "COLOR" || s == "LTYPE" || s == "FREEZE" || s == "THAW";
}

// Runs LAYER with a script of answers, ending with Enter.
//
// Each answer is supplied as whatever the prompt it lands on asks for, which is
// what the text front end does for real. Dispatching on the prompt rather than
// on the text matters here: layer "0" is a perfectly good name that happens to
// be a digit, so guessing from the string alone turns a name into a number.
void layer(CommandEngine& engine, std::initializer_list<const char*> answers) {
    engine.begin(make_command("LAYER"));
    for (const char* a : answers) {
        const std::string s = a;
        if (s.empty()) {
            engine.supply(InputValue::none());
            continue;
        }
        if (engine.prompt().kind == PromptKind::Integer) {
            engine.supply(InputValue::of_integer(std::stoi(s)));
            continue;
        }
        engine.supply(is_option(s) ? InputValue::of_keyword(s) : InputValue::of_string(s));
    }
    engine.supply(InputValue::none());
}

}  // namespace

TEST_CASE("layer: New creates without changing the current layer") {
    Database db;
    CommandEngine engine(db);
    CHECK(db.sysvars().get_string(Sysvar::CLayer) == "0");

    layer(engine, {"NEW", "WALLS"});
    CHECK(db.find_layer("WALLS") != kInvalidLayer);
    CHECK(db.sysvars().get_string(Sysvar::CLayer) == "0");
}

TEST_CASE("layer: Set changes the current layer, Make creates and sets") {
    Database db;
    CommandEngine engine(db);

    layer(engine, {"NEW", "WALLS"});
    layer(engine, {"SET", "WALLS"});
    CHECK(db.sysvars().get_string(Sysvar::CLayer) == "WALLS");

    // Make does both in one, which is why it exists alongside New.
    layer(engine, {"MAKE", "ROOF"});
    CHECK(db.find_layer("ROOF") != kInvalidLayer);
    CHECK(db.sysvars().get_string(Sysvar::CLayer) == "ROOF");
}

TEST_CASE("layer: names are case-insensitive and stored upper") {
    Database db;
    CommandEngine engine(db);
    layer(engine, {"NEW", "walls"});
    CHECK(db.find_layer("WALLS") != kInvalidLayer);
    CHECK(db.layer(db.find_layer("WALLS")).name == "WALLS");
}

TEST_CASE("layer: several names at once") {
    Database db;
    CommandEngine engine(db);
    layer(engine, {"NEW", "A,B,C"});
    CHECK(db.find_layer("A") != kInvalidLayer);
    CHECK(db.find_layer("B") != kInvalidLayer);
    CHECK(db.find_layer("C") != kInvalidLayer);
}

TEST_CASE("layer: Off is a negative colour, so the colour survives") {
    Database db;
    CommandEngine engine(db);
    layer(engine, {"NEW", "WALLS", "COLOR", "3", "WALLS"});
    const LayerId walls = db.find_layer("WALLS");
    CHECK(db.layer(walls).color == 3);

    layer(engine, {"OFF", "WALLS"});
    CHECK(db.layer(walls).off());
    CHECK(db.layer(walls).visible_color() == 3);  // remembered, not lost

    layer(engine, {"ON", "WALLS"});
    CHECK(!db.layer(walls).off());
    CHECK(db.layer(walls).color == 3);
}

TEST_CASE("layer: recolouring a layer that is off leaves it off") {
    Database db;
    CommandEngine engine(db);
    layer(engine, {"NEW", "WALLS", "OFF", "WALLS"});
    const LayerId walls = db.find_layer("WALLS");
    CHECK(db.layer(walls).off());

    // Otherwise changing a colour would silently turn layers back on.
    layer(engine, {"COLOR", "5", "WALLS"});
    CHECK(db.layer(walls).off());
    CHECK(db.layer(walls).visible_color() == 5);
}

TEST_CASE("layer: freeze and thaw, but never the current layer") {
    Database db;
    CommandEngine engine(db);
    layer(engine, {"NEW", "WALLS"});
    const LayerId walls = db.find_layer("WALLS");

    layer(engine, {"FREEZE", "WALLS"});
    CHECK(db.layer(walls).frozen);
    layer(engine, {"THAW", "WALLS"});
    CHECK(!db.layer(walls).frozen);

    // R12 refuses: you would be drawing onto something you cannot see.
    layer(engine, {"FREEZE", "0"});
    CHECK(!db.layer(kLayerZero).frozen);
    CHECK(engine.message().find("Cannot freeze") != std::string::npos);
}

TEST_CASE("layer: Set refuses a frozen layer") {
    Database db;
    CommandEngine engine(db);
    layer(engine, {"NEW", "WALLS", "FREEZE", "WALLS"});
    layer(engine, {"SET", "WALLS"});
    CHECK(db.sysvars().get_string(Sysvar::CLayer) == "0");
    CHECK(engine.message().find("frozen") != std::string::npos);
}

TEST_CASE("layer: options other than Make and New do not create") {
    Database db;
    CommandEngine engine(db);
    const std::size_t before = db.layers().size();

    // A mistyped name must not become a new layer as a side effect.
    layer(engine, {"FREEZE", "TYPOO"});
    CHECK(db.layers().size() == before);
    CHECK(engine.message().find("not found") != std::string::npos);
}

TEST_CASE("layer: a colour outside R12's range is refused") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LAYER"));
    engine.supply(InputValue::of_keyword("COLOR"));
    engine.supply(InputValue::of_integer(300));
    CHECK(engine.status() == EngineStatus::Failed);
}

TEST_CASE("layer: an unloaded linetype is reported, not invented") {
    Database db;
    CommandEngine engine(db);
    layer(engine, {"NEW", "WALLS", "LTYPE", "DASHED", "WALLS"});
    CHECK(db.find_linetype("DASHED") == kInvalidLinetype);
    CHECK(engine.message().find("not loaded") != std::string::npos);
}

TEST_CASE("layer: a whole LAYER command is one undo step") {
    Database db;
    CommandEngine engine(db);
    const std::size_t before = db.journal().undo_depth();

    // Create, colour and set: three table changes in one command.
    layer(engine, {"MAKE", "WALLS", "COLOR", "3", "WALLS"});
    CHECK(db.journal().undo_depth() == before + 1);

    CHECK(db.journal().undo(db));
    CHECK(db.find_layer("WALLS") == kInvalidLayer);
    CHECK(db.sysvars().get_string(Sysvar::CLayer) == "0");
}

TEST_CASE("layer: new geometry takes the current layer and colour") {
    Database db;
    CommandEngine engine(db);
    layer(engine, {"MAKE", "WALLS"});

    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({1, 1, 0}));
    engine.supply(InputValue::none());

    const Entity* e = db.get(db.order().back());
    CHECK(db.layer(e->props().layer).name == "WALLS");
    CHECK(e->props().color == kColorByLayer);  // CECOLOR defaults to BYLAYER

    // CECOLOR overrides for new entities without touching the layer.
    CHECK(db.sysvars().set_int(Sysvar::CEColor, 1) == Sysvars::SetStatus::Ok);
    engine.begin(make_command("CIRCLE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(5.0));
    const Entity* c = db.get(db.order().back());
    CHECK(c->props().color == 1);
    CHECK(db.layer(c->props().layer).name == "WALLS");
}

TEST_CASE("layer: a circle keeps its extrusion through current properties") {
    Database db;
    CommandEngine engine(db);
    // An extrusion is geometry, not a current setting, so stamping the current
    // layer must not flatten a tilted circle back to world XY.
    auto c = std::make_unique<Circle>(Vec3{0, 0, 0}, 5.0, Vec3{0, 1, 0});
    const Vec3 normal = c->props().normal;
    db.add(std::move(c));
    CHECK_VEC(db.get(db.order().back())->props().normal, normal.x, normal.y, normal.z, 1e-12);
}

TEST_CASE("layer: CLAYER naming a layer that has gone falls back to 0") {
    Database db;
    CHECK(db.sysvars().set_string(Sysvar::CLayer, "NOSUCHLAYER") == Sysvars::SetStatus::Ok);
    // Rather than creating one as a side effect of drawing, or indexing off the
    // end of the table.
    CHECK(db.current_layer() == kLayerZero);
}

TEST_CASE("color: sets CECOLOR, and BYLAYER goes back to the default") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("COLOR"));
    engine.supply(InputValue::of_integer(1));
    CHECK(db.sysvars().get_int(Sysvar::CEColor) == 1);

    engine.begin(make_command("COLOR"));
    engine.supply(InputValue::of_keyword("BYLAYER"));
    CHECK(db.sysvars().get_int(Sysvar::CEColor) == kColorByLayer);

    engine.begin(make_command("COLOR"));
    engine.supply(InputValue::of_keyword("BYBLOCK"));
    CHECK(db.sysvars().get_int(Sysvar::CEColor) == kColorByBlock);
}

TEST_CASE("color: Enter keeps what is there, and the range is checked") {
    Database db;
    CommandEngine engine(db);
    db.sysvars().set_int(Sysvar::CEColor, 5);

    engine.begin(make_command("COLOR"));
    engine.supply(InputValue::none());
    CHECK(db.sysvars().get_int(Sysvar::CEColor) == 5);

    engine.begin(make_command("COLOR"));
    engine.supply(InputValue::of_integer(300));
    CHECK(engine.status() == EngineStatus::Failed);
    CHECK(db.sysvars().get_int(Sysvar::CEColor) == 5);
}

TEST_CASE("ltscale: positive only") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("LTSCALE"));
    engine.supply(InputValue::of_real(2.0));
    CHECK_NEAR(db.sysvars().get_real(Sysvar::LtScale), 2.0, 1e-12);

    // Zero would make every dash zero-length, which draws nothing and looks
    // like the linetype having been lost rather than the scale being wrong.
    engine.begin(make_command("LTSCALE"));
    engine.supply(InputValue::of_real(0.0));
    CHECK(engine.status() == EngineStatus::Failed);
    CHECK_NEAR(db.sysvars().get_real(Sysvar::LtScale), 2.0, 1e-12);
}

TEST_CASE("limits: two corners, in either order") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("LIMITS"));
    engine.supply(InputValue::of_point({100, 50, 0}));  // upper right given first
    engine.supply(InputValue::of_point({0, 0, 0}));
    CHECK(engine.status() == EngineStatus::Finished);

    // Ordered on the way in, so a pair given the other way round still
    // describes a rectangle rather than an inside-out one.
    CHECK_VEC(db.sysvars().get_point(Sysvar::LimMin), 0.0, 0.0, 0.0, 1e-12);
    CHECK_VEC(db.sysvars().get_point(Sysvar::LimMax), 100.0, 50.0, 0.0, 1e-12);
}

TEST_CASE("limits: ON and OFF are about checking, not about the corners") {
    Database db;
    CommandEngine engine(db);
    const Vec3 before = db.sysvars().get_point(Sysvar::LimMax);

    engine.begin(make_command("LIMITS"));
    engine.supply(InputValue::of_keyword("ON"));
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(db.sysvars().get_int(Sysvar::LimCheck) == 1);
    CHECK_VEC(db.sysvars().get_point(Sysvar::LimMax), before.x, before.y, before.z, 1e-12);

    engine.begin(make_command("LIMITS"));
    engine.supply(InputValue::of_keyword("OFF"));
    CHECK(db.sysvars().get_int(Sysvar::LimCheck) == 0);
}

TEST_CASE("limits: a degenerate rectangle is refused") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LIMITS"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({0, 50, 0}));
    CHECK(engine.status() == EngineStatus::Failed);
}

TEST_CASE("ltype: Load, Set, and refusing what is not loaded") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("LTYPE"));
    engine.supply(InputValue::of_keyword("LOAD"));
    engine.supply(InputValue::of_string("DASHED,HIDDEN"));
    engine.supply(InputValue::none());
    CHECK(db.find_linetype("DASHED") != kInvalidLinetype);
    CHECK(db.find_linetype("HIDDEN") != kInvalidLinetype);

    engine.begin(make_command("LTYPE"));
    engine.supply(InputValue::of_keyword("SET"));
    engine.supply(InputValue::of_string("DASHED"));
    engine.supply(InputValue::none());
    CHECK(db.sysvars().get_string(Sysvar::CELtype) == "DASHED");

    // Setting something that was never loaded is reported rather than invented.
    engine.begin(make_command("LTYPE"));
    engine.supply(InputValue::of_keyword("SET"));
    engine.supply(InputValue::of_string("NOSUCHTYPE"));
    engine.supply(InputValue::none());
    CHECK(db.sysvars().get_string(Sysvar::CELtype) == "DASHED");
    CHECK(engine.message().find("not loaded") != std::string::npos);
}

TEST_CASE("ltype: Create defines a pattern inline") {
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("LTYPE"));
    engine.supply(InputValue::of_keyword("CREATE"));
    engine.supply(InputValue::of_string("MYDASH"));
    engine.supply(InputValue::of_string(".5,-.25,0,-.25"));
    engine.supply(InputValue::none());

    const LinetypeId id = db.find_linetype("MYDASH");
    CHECK(id != kInvalidLinetype);
    CHECK(db.linetype(id).pattern.size() == 4);
    CHECK_NEAR(db.linetype(id).pattern_length(), 1.0, 1e-12);
}

TEST_CASE("ltype: a pattern that is not numbers is refused") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LTYPE"));
    engine.supply(InputValue::of_keyword("CREATE"));
    engine.supply(InputValue::of_string("BAD"));
    engine.supply(InputValue::of_string("dash,gap"));
    CHECK(engine.status() == EngineStatus::Failed);
    CHECK(db.find_linetype("BAD") == kInvalidLinetype);
}

TEST_CASE("ltype: new entities take CELTYPE") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LTYPE"));
    engine.supply(InputValue::of_keyword("LOAD"));
    engine.supply(InputValue::of_string("DASHED"));
    engine.supply(InputValue::of_keyword("SET"));
    engine.supply(InputValue::of_string("DASHED"));
    engine.supply(InputValue::none());

    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_point({1, 1, 0}));
    engine.supply(InputValue::none());

    const Entity* e = db.get(db.order().back());
    CHECK(db.linetype(e->props().linetype).name == "DASHED");
}
