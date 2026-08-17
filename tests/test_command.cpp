// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "ncad/command.hpp"
#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/entities.hpp"
#include "ncad/input_text.hpp"
#include "ncad/lisp/eval.hpp"
#include "ncad/sysvar.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace ncad;
using namespace ncad::lisp;

namespace {

#define CHECK_VEC3(a, b) CHECK_VEC((a), (b).x, (b).y, (b).z, 1e-9)

// Runs a script against a fresh drawing.
struct ScriptFixture {
    Database db;
    CommandEngine engine{db};

    EngineStatus run(const std::string& script) {
        TextInputSource src(tokenize_script(script));
        // The first token names the command.
        InputValue ignored;
        Prompt bare;
        bare.kind = PromptKind::String;
        bare.allow_empty = true;
        src.next_value(bare, ignored);
        engine.begin(make_command(ignored.text));
        return engine.run(src);
    }

    const Line* line_at(std::size_t i) const {
        return static_cast<const Line*>(db.get(db.order()[i]));
    }
};

// A source that never has anything, standing in for a GUI waiting on the user.
class BlockingSource final : public InputSource {
public:
    bool next_value(const Prompt&, InputValue&) override {
        ++polls;
        return false;
    }
    int polls{0};
};

}  // namespace

TEST_CASE("prompt: renders R12-style keyword lists") {
    Prompt p;
    p.message = "Specify next point";
    p.keywords = {"Close", "Undo"};
    CHECK(p.text() == "Specify next point or [Close/Undo]: ");

    Prompt plain;
    plain.message = "Specify first point";
    CHECK(plain.text() == "Specify first point: ");
}

TEST_CASE("input: text parsing is directed by the prompt") {
    std::string err;
    InputValue v;

    Prompt point;
    point.kind = PromptKind::Point;
    CHECK(parse_input(point, "1,2,3", v, err));
    CHECK(v.kind == InputKind::Point);
    CHECK_VEC3(v.point, Vec3(1.0, 2.0, 3.0));

    // Two coordinates means Z = 0.
    CHECK(parse_input(point, "4,5", v, err));
    CHECK_VEC3(v.point, Vec3(4.0, 5.0, 0.0));
    CHECK(!parse_input(point, "nonsense", v, err));
    CHECK(!err.empty());

    // The same token means different things at different prompts.
    Prompt distance;
    distance.kind = PromptKind::Distance;
    CHECK(parse_input(distance, "5", v, err));
    CHECK(v.kind == InputKind::Real);
    CHECK_NEAR(v.real, 5.0, 1e-12);

    Prompt text;
    text.kind = PromptKind::String;
    CHECK(parse_input(text, "5", v, err));
    CHECK(v.kind == InputKind::String);
    CHECK(v.text == "5");
}

TEST_CASE("input: keywords match on unambiguous prefixes") {
    std::string err;
    InputValue v;

    Prompt p;
    p.kind = PromptKind::Distance;
    p.keywords = {"Diameter"};
    // A keyword wins over a value parse, so "D" is not a failed number.
    CHECK(parse_input(p, "D", v, err));
    CHECK(v.kind == InputKind::Keyword);
    CHECK(v.text == "DIAMETER");
    CHECK(parse_input(p, "diameter", v, err));
    CHECK(v.text == "DIAMETER");

    // Ambiguity is a failure, not a coin toss.
    Prompt two;
    two.kind = PromptKind::Point;
    two.keywords = {"Close", "Center"};
    CHECK(!parse_input(two, "C", v, err));
    CHECK(parse_input(two, "CL", v, err));
    CHECK(v.text == "CLOSE");
}

TEST_CASE("input: Enter is only accepted where the prompt allows it") {
    std::string err;
    InputValue v;

    Prompt required;
    required.kind = PromptKind::Point;
    CHECK(!parse_input(required, "", v, err));

    Prompt optional;
    optional.kind = PromptKind::Point;
    optional.allow_empty = true;
    CHECK(parse_input(optional, "", v, err));
    CHECK(v.kind == InputKind::None);
}

TEST_CASE("engine: a command suspends when its source has nothing") {
    // The property the whole design exists for. The engine must hand control
    // back rather than blocking, or a GUI event loop would freeze here.
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LINE"));
    CHECK(engine.active());
    CHECK(engine.prompt().text() == "Specify first point: ");

    BlockingSource source;
    CHECK(engine.run(source) == EngineStatus::Waiting);
    CHECK(source.polls == 1);
    // Still suspended, still holding its state, nothing drawn.
    CHECK(engine.active());
    CHECK(db.empty());

    // And it resumes exactly where it left off.
    engine.supply(InputValue::of_point(Vec3(0.0, 0.0, 0.0)));
    engine.supply(InputValue::of_point(Vec3(10.0, 0.0, 0.0)));
    CHECK(db.size() == 1);
    engine.supply(InputValue::none());
    CHECK(engine.status() == EngineStatus::Finished);
}

TEST_CASE("engine: supplying values one at a time is the GUI path") {
    // A viewport calls supply() from an event handler; no InputSource involved.
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("CIRCLE"));
    CHECK(engine.prompt().kind == PromptKind::Point);

    engine.supply(InputValue::of_point(Vec3(1.0, 2.0, 0.0)));
    CHECK(engine.prompt().kind == PromptKind::Distance);
    // The prompt carries the rubber-band origin a viewport needs.
    CHECK(engine.prompt().has_base);
    CHECK_VEC3(engine.prompt().base, Vec3(1.0, 2.0, 0.0));

    CHECK(engine.supply(InputValue::of_real(5.0)) == EngineStatus::Finished);
    CHECK(db.size() == 1);
    const Circle* c = static_cast<const Circle*>(db.get(db.order()[0]));
    CHECK_VEC3(c->center(), Vec3(1.0, 2.0, 0.0));
    CHECK_NEAR(c->radius(), 5.0, 1e-12);
}

TEST_CASE("engine: escape cancels but keeps committed work") {
    // R12 behaviour: cancelling LINE after three segments keeps the three.
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_point(Vec3(0.0, 0.0, 0.0)));
    engine.supply(InputValue::of_point(Vec3(1.0, 0.0, 0.0)));
    engine.supply(InputValue::of_point(Vec3(2.0, 0.0, 0.0)));
    CHECK(db.size() == 2);

    CHECK(engine.supply(InputValue::cancel()) == EngineStatus::Cancelled);
    CHECK(!engine.active());
    CHECK(db.size() == 2);
}

TEST_CASE("engine: an unknown command fails rather than crashing") {
    Database db;
    CommandEngine engine(db);
    CHECK(engine.begin(make_command("NOSUCHCOMMAND")) == EngineStatus::Failed);
    CHECK(!engine.active());
    // Supplying input with nothing running is an error, not undefined behaviour.
    CHECK(engine.supply(InputValue::of_point(Vec3())) == EngineStatus::Failed);
}

TEST_CASE("engine: starting a command abandons the one in progress") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_point(Vec3(0.0, 0.0, 0.0)));
    engine.begin(make_command("CIRCLE"));
    CHECK(std::string(engine.command_name()) == "CIRCLE");
    CHECK(engine.prompt().text() == "Specify center point for circle: ");
}

TEST_CASE("script: LINE draws a chain and Enter ends it") {
    ScriptFixture f;
    CHECK(f.run("LINE 0,0 10,0 10,10\n\n") == EngineStatus::Finished);
    CHECK(f.db.size() == 2);
    CHECK_VEC3(f.line_at(0)->start(), Vec3(0.0, 0.0, 0.0));
    CHECK_VEC3(f.line_at(0)->end(), Vec3(10.0, 0.0, 0.0));
    CHECK_VEC3(f.line_at(1)->end(), Vec3(10.0, 10.0, 0.0));
}

TEST_CASE("script: LINE Close joins back to the first point") {
    ScriptFixture f;
    CHECK(f.run("LINE 0,0 10,0 10,10 C\n") == EngineStatus::Finished);
    CHECK(f.db.size() == 3);
    // The closing segment runs from the last vertex back to the first.
    CHECK_VEC3(f.line_at(2)->start(), Vec3(10.0, 10.0, 0.0));
    CHECK_VEC3(f.line_at(2)->end(), Vec3(0.0, 0.0, 0.0));
}

TEST_CASE("script: LINE Undo removes the last segment and backs up") {
    ScriptFixture f;
    f.run("LINE 0,0 10,0 10,10 U 20,0\n\n");
    CHECK(f.db.size() == 2);
    // After undoing the second segment, the next point continues from (10,0).
    CHECK_VEC3(f.line_at(1)->start(), Vec3(10.0, 0.0, 0.0));
    CHECK_VEC3(f.line_at(1)->end(), Vec3(20.0, 0.0, 0.0));
}

TEST_CASE("script: CIRCLE by radius and by diameter") {
    ScriptFixture f;
    f.run("CIRCLE 0,0 5\n");
    CHECK(f.db.size() == 1);
    CHECK_NEAR(static_cast<const Circle*>(f.db.get(f.db.order()[0]))->radius(), 5.0, 1e-12);

    ScriptFixture g;
    g.run("CIRCLE 0,0 D 10\n");
    CHECK_NEAR(static_cast<const Circle*>(g.db.get(g.db.order()[0]))->radius(), 5.0, 1e-12);
}

TEST_CASE("script: a distance prompt also accepts a second point") {
    // Which is how a radius is given by dragging.
    ScriptFixture f;
    f.run("CIRCLE 0,0 3,4\n");
    CHECK_NEAR(static_cast<const Circle*>(f.db.get(f.db.order()[0]))->radius(), 5.0, 1e-12);
}

TEST_CASE("script: ERASE selects until Enter") {
    ScriptFixture f;
    f.run("LINE 0,0 1,0\n\n");
    f.run("LINE 2,0 3,0\n\n");
    CHECK(f.db.size() == 2);

    const Handle first = f.db.order()[0];
    TextInputSource src(tokenize_script(std::to_string(first) + "\n\n"));
    f.engine.begin(make_command("ERASE"));
    CHECK(f.engine.run(src) == EngineStatus::Finished);
    CHECK(f.db.size() == 1);
    CHECK(f.db.get(first) == nullptr);
}

TEST_CASE("script: tokenizer handles comments, quotes and blank lines") {
    const auto tokens = tokenize_script("LINE 0,0 ; a comment\n1,1\n\n");
    CHECK(tokens.size() == 4);
    CHECK(tokens[0] == "LINE");
    CHECK(tokens[1] == "0,0");
    CHECK(tokens[2] == "1,1");
    CHECK(tokens[3].empty());  // the blank line is an Enter

    const auto quoted = tokenize_script("TEXT \"hello world\" 1,1");
    CHECK(quoted.size() == 3);
    CHECK(quoted[1] == "hello world");
}

TEST_CASE("script: a bad token stops the run without corrupting the command") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LINE"));
    TextInputSource src(tokenize_script("not-a-point"));
    CHECK(engine.run(src) == EngineStatus::Waiting);
    CHECK(src.failed());
    // The command is untouched and still asking the same question.
    CHECK(engine.active());
    CHECK(engine.prompt().text() == "Specify first point: ");
}

// --- the same commands, driven from AutoLISP --------------------------------

namespace {

struct LispFixture {
    Database db;
    CommandEngine engine{db};
    Context ctx;
    Interp in{ctx};
    std::ostringstream out;

    LispFixture() {
        in.set_output(&out);
        in.set_database(&db);
        in.set_command_engine(&engine);
    }

    std::string eval(const std::string& source) {
        in.clear_error();
        Value v;
        if (!in.eval_string(source, v)) return "<error: " + in.error().message() + ">";
        return prin1(v);
    }

    EvalStatus status(const std::string& source) {
        in.clear_error();
        Value v;
        in.eval_string(source, v);
        return in.error().status;
    }

    const Line* line_at(std::size_t i) const {
        return static_cast<const Line*>(db.get(db.order()[i]));
    }
};

}  // namespace

TEST_CASE("lisp: (command ...) drives the same state machines") {
    LispFixture f;
    CHECK(f.eval(R"((command "LINE" '(0 0 0) '(10 0 0) ""))") == "nil");
    CHECK(f.db.size() == 1);
    CHECK_VEC3(f.line_at(0)->start(), Vec3(0.0, 0.0, 0.0));
    CHECK_VEC3(f.line_at(0)->end(), Vec3(10.0, 0.0, 0.0));
}

TEST_CASE("lisp: a command started in one call finishes in a later one") {
    // The behaviour that makes blocking reads impossible: between these two
    // calls, control returned to the interpreter with LINE still suspended.
    LispFixture f;
    f.eval(R"((command "LINE" '(0 0 0) '(10 0 0)))");
    CHECK(f.engine.active());
    CHECK(f.db.size() == 1);

    f.eval(R"((command '(10 10 0)))");
    CHECK(f.engine.active());
    CHECK(f.db.size() == 2);

    f.eval(R"((command ""))");
    CHECK(!f.engine.active());
    CHECK(f.db.size() == 2);
    CHECK_VEC3(f.line_at(1)->end(), Vec3(10.0, 10.0, 0.0));
}

TEST_CASE("lisp: arbitrary LISP runs between the halves of a command") {
    LispFixture f;
    f.eval(R"((command "LINE" '(0 0 0)))");
    // Compute the next vertex while LINE sits suspended.
    f.eval("(setq p (list (* 2.0 5.0) 0.0 0.0))");
    f.eval("(command p \"\")");
    CHECK(f.db.size() == 1);
    CHECK_VEC3(f.line_at(0)->end(), Vec3(10.0, 0.0, 0.0));
}

TEST_CASE("lisp: (command) with no arguments cancels") {
    LispFixture f;
    f.eval(R"((command "LINE" '(0 0 0) '(1 0 0)))");
    CHECK(f.db.size() == 1);
    f.eval("(command)");
    CHECK(!f.engine.active());
    CHECK(f.db.size() == 1);  // committed work survives
}

TEST_CASE("lisp: several commands in one call") {
    LispFixture f;
    f.eval(R"((command "LINE" '(0 0 0) '(1 0 0) ""
                       "CIRCLE" '(5 5 0) 2.0))");
    CHECK(f.db.size() == 2);
    CHECK(f.db.get(f.db.order()[0])->type() == EntityType::Line);
    CHECK(f.db.get(f.db.order()[1])->type() == EntityType::Circle);
    CHECK(!f.engine.active());
}

TEST_CASE("lisp: keywords work as strings") {
    LispFixture f;
    f.eval(R"((command "CIRCLE" '(0 0 0) "D" 10.0))");
    CHECK_NEAR(static_cast<const Circle*>(f.db.get(f.db.order()[0]))->radius(), 5.0, 1e-12);

    LispFixture g;
    g.eval(R"((command "LINE" '(0 0 0) '(10 0 0) '(10 10 0) "C"))");
    CHECK(g.db.size() == 3);
}

TEST_CASE("lisp: enames select entities for ERASE") {
    LispFixture f;
    f.eval(R"((entmake '((0 . "LINE") (10 0.0 0.0 0.0) (11 1.0 0.0 0.0))))");
    f.eval(R"((entmake '((0 . "LINE") (10 2.0 0.0 0.0) (11 3.0 0.0 0.0))))");
    CHECK(f.db.size() == 2);
    // The ename from entlast feeds straight into a selection prompt.
    f.eval(R"((command "ERASE" (entlast) ""))");
    CHECK(f.db.size() == 1);
}

TEST_CASE("lisp: bad input to a command is reported") {
    LispFixture f;
    CHECK(f.status(R"((command "LINE" "not-a-point"))") == EvalStatus::BadArgumentType);
    // Values with no command running are an error rather than a silent no-op.
    LispFixture g;
    CHECK(g.status("(command '(0 0 0))") == EvalStatus::BadArgumentType);
}

TEST_CASE("lisp: a missing command engine is reported, not a crash") {
    Database db;
    Context ctx;
    Interp in(ctx);
    in.set_database(&db);
    Value v;
    CHECK(in.eval_string(R"((command "LINE" '(0 0 0)))", v) == false);
    CHECK(in.error().status == EvalStatus::BadArgumentType);
}

TEST_CASE("lisp: a LISP loop drives a command, which is the point") {
    // Geometry generated procedurally through the command layer rather than
    // through entmake, proving commands are reachable from the interpreter.
    LispFixture f;
    f.eval(R"(
        (defun staircase (n / i)
          (setq i 0)
          (while (< i n)
            (command "LINE" (list i i 0.0) (list (1+ i) i 0.0) "")
            (command "LINE" (list (1+ i) i 0.0) (list (1+ i) (1+ i) 0.0) "")
            (setq i (1+ i))))
        (staircase 10)
    )");
    CHECK(f.db.size() == 20);
    CHECK(!f.engine.active());
}

TEST_CASE("DXFOUT: writes from the command prompt, not only from LISP") {
    // It was a LISP function first only because the command layer did not exist.
    ScriptFixture f;
    f.run("LINE 0,0 10,0\n\n");
    CHECK(f.db.size() == 1);

    const std::string path = "ncad_test_cmd_dxfout.dxf";
    std::remove(path.c_str());

    // The trailing blank line is Enter at the DXF-version prompt: keep
    // DXFVERSION as it stands.
    TextInputSource src(tokenize_script(path + "\n\n"));
    f.engine.begin(make_command("DXFOUT"));
    CHECK(f.engine.run(src) == EngineStatus::Finished);
    CHECK(f.engine.message().find("written") != std::string::npos);

    std::ifstream check(path, std::ios::binary);
    CHECK(check.good());
    const std::string text((std::istreambuf_iterator<char>(check)),
                           std::istreambuf_iterator<char>());
    CHECK(text.find("LINE") != std::string::npos);
    CHECK(text.substr(text.size() - 5) == "EOF\r\n");
    check.close();
    std::remove(path.c_str());
}

TEST_CASE("DXFOUT: supplies the extension when it is left off") {
    ScriptFixture f;
    f.run("CIRCLE 0,0 1\n");

    const std::string stem = "ncad_test_cmd_extension";
    std::remove((stem + ".dxf").c_str());

    TextInputSource src(tokenize_script(stem + "\n\n"));
    f.engine.begin(make_command("DXFOUT"));
    CHECK(f.engine.run(src) == EngineStatus::Finished);

    std::ifstream check(stem + ".dxf", std::ios::binary);
    CHECK(check.good());  // ".dxf" was appended
    check.close();
    std::remove((stem + ".dxf").c_str());

    // And not doubled when it is already there.
    ScriptFixture g;
    g.run("CIRCLE 0,0 1\n");
    TextInputSource src2(tokenize_script(stem + ".dxf\n\n"));
    g.engine.begin(make_command("DXFOUT"));
    CHECK(g.engine.run(src2) == EngineStatus::Finished);
    CHECK(g.engine.message().find(".dxf.dxf") == std::string::npos);
    std::remove((stem + ".dxf").c_str());
}

TEST_CASE("DXFOUT: an unwritable path fails the command") {
    ScriptFixture f;
    f.run("CIRCLE 0,0 1\n");
    TextInputSource src(tokenize_script("/nonexistent-dir-xyzzy/out.dxf\n\n"));
    f.engine.begin(make_command("DXFOUT"));
    CHECK(f.engine.run(src) == EngineStatus::Failed);
    CHECK(f.engine.message().find("cannot write") != std::string::npos);
}

TEST_CASE("DXFOUT: asks which version rather than silently reading DXFVERSION") {
    ScriptFixture f;
    f.run("CIRCLE 0,0 1\n");
    const std::string path = "ncad_test_dxfout_asks.dxf";
    std::remove(path.c_str());

    f.engine.begin(make_command("DXFOUT"));
    f.engine.supply(InputValue::of_string(path));
    CHECK(f.engine.active());
    CHECK(f.engine.prompt().message.find("DXF version") != std::string::npos);

    f.engine.supply(InputValue::of_keyword("R2000"));
    CHECK(f.engine.message().find("R2000") != std::string::npos);

    std::remove(path.c_str());
}

TEST_CASE("DXFOUT: the chosen version is sticky, pushed back into DXFVERSION") {
    ScriptFixture f;
    f.run("CIRCLE 0,0 1\n");
    CHECK(f.db.sysvars().get_string(Sysvar::DxfVersionVar) == "R12");

    const std::string path = "ncad_test_dxfout_sticky.dxf";
    std::remove(path.c_str());
    f.engine.begin(make_command("DXFOUT"));
    f.engine.supply(InputValue::of_string(path));
    f.engine.supply(InputValue::of_keyword("R2000"));

    CHECK(f.db.sysvars().get_string(Sysvar::DxfVersionVar) == "R2000");
    std::remove(path.c_str());

    // And a second DXFOUT now offers R2000 as the default, Enter keeps it.
    const std::string path2 = "ncad_test_dxfout_sticky2.dxf";
    std::remove(path2.c_str());
    f.engine.begin(make_command("DXFOUT"));
    f.engine.supply(InputValue::of_string(path2));
    CHECK(f.engine.prompt().message.find("R2000") != std::string::npos);
    f.engine.supply(InputValue{});
    CHECK(f.engine.message().find("R2000") != std::string::npos);
    CHECK(f.db.sysvars().get_string(Sysvar::DxfVersionVar) == "R2000");
    std::remove(path2.c_str());
}

TEST_CASE("commands: abbreviations resolve, exact names win") {
    // What R12 users expect: type enough to be unambiguous and press Enter.
    CHECK(resolve_command_name("LINE").name == "LINE");
    CHECK(resolve_command_name("line").name == "LINE");
    // LI is LIST's acad.pgp alias, and an alias beats a prefix -- so it does
    // not resolve to LINE even though LINE is the older command.
    CHECK(resolve_command_name("LI").name == "LIST");
    CHECK(resolve_command_name("LIN").name == "LINE");

    // D used to mean DXFOUT, when that was the only command starting with it,
    // then DIST, and now DIM -- each time because a shorter name arrived and
    // shortest-wins handed the letter over. The fourth move of this
    // abbreviation, and still the rule working rather than failing.
    CHECK(resolve_command_name("D").name == "DIM");
    CHECK(resolve_command_name("DIS").name == "DIST");
    // DX used to mean DXFOUT. DXFIN made it ambiguous, and shortest-wins hands
    // it to the shorter name. This is the third abbreviation the growing
    // command set has moved, and it is the rule working rather than failing --
    // the point of the test is that the answer is defined, not that it never
    // changes.
    CHECK(resolve_command_name("DX").name == "DXFIN");
    CHECK(resolve_command_name("DXFO").name == "DXFOUT");
    CHECK(resolve_command_name("DIS").name == "DIST");
    // acad.pgp short forms.
    CHECK(resolve_command_name("C").name == "CIRCLE");
    CHECK(resolve_command_name("E").name == "ERASE");
    CHECK(resolve_command_name("L").name == "LINE");

    CHECK(!resolve_command_name("ZZZ").ok());
    CHECK(!resolve_command_name("").ok());
}

TEST_CASE("commands: an exact name is never shadowed by an abbreviation") {
    // If a command called C were ever added, C must mean it and not CIRCLE.
    const std::vector<std::string> names = {"C", "CIRCLE", "COPY"};
    const std::vector<CommandAlias> aliases = {{"C", "CIRCLE"}};
    CHECK(resolve_in("C", names, aliases).name == "C");
}

TEST_CASE("commands: an alias overrides prefix matching") {
    // The reason R12 has a table rather than deriving prefixes: once CIRCLE and
    // COPY both exist, "C" is ambiguous by prefix but must still mean CIRCLE.
    const std::vector<std::string> names = {"CIRCLE", "COPY"};
    const std::vector<CommandAlias> aliases = {{"C", "CIRCLE"}, {"CP", "COPY"}};
    CHECK(resolve_in("C", names, aliases).name == "CIRCLE");
    CHECK(resolve_in("CP", names, aliases).name == "COPY");
    // Without the alias it is ambiguous, but still commits: shortest wins, so
    // COPY beats CIRCLE. Pressing Enter always gets you a command.
    const CommandMatch bare = resolve_in("C", names, {});
    CHECK(bare.ambiguous);
    CHECK(bare.ok());
    CHECK(bare.name == "COPY");
    CHECK(bare.candidates.size() == 2);
}

TEST_CASE("commands: make_command stays exact, so keywords are safe") {
    // (command "LINE" p1 p2 "C") must close the polyline, not start CIRCLE.
    // That only holds while make_command refuses to resolve abbreviations.
    CHECK(make_command("C") == nullptr);
    CHECK(make_command("LI") == nullptr);
    CHECK(make_command("CIRCLE") != nullptr);
}

TEST_CASE("commands: an ambiguous prefix resolves shortest-first") {
    // The fundamental command should win over the elaborate one sharing its
    // prefix, which is what makes the rule feel right rather than arbitrary.
    const std::vector<std::string> names = {"ARC", "ARRAY", "LINE", "LINETYPE"};
    CHECK(resolve_in("AR", names, {}).name == "ARC");
    CHECK(resolve_in("LI", names, {}).name == "LINE");
    // Exact still wins outright.
    CHECK(resolve_in("ARRAY", names, {}).name == "ARRAY");

    // Equal lengths fall back to alphabetical, so the choice is still stable.
    const std::vector<std::string> tie = {"CONE", "COPY"};
    CHECK(resolve_in("CO", tie, {}).name == "CONE");
    // ...and an alias is how you override a tie-break you disagree with.
    CHECK(resolve_in("CP", tie, {{"CP", "COPY"}}).name == "COPY");
}

TEST_CASE("coordinates: @ is relative to the last point") {
    // Without this, drawing anything by hand is arithmetic homework.
    ScriptFixture f;
    CHECK(f.run("LINE 0,0 @10,0 @0,5\n\n") == EngineStatus::Finished);
    CHECK(f.db.size() == 2);
    CHECK_VEC3(f.line_at(0)->end(), Vec3(10.0, 0.0, 0.0));
    CHECK_VEC3(f.line_at(1)->end(), Vec3(10.0, 5.0, 0.0));
}

TEST_CASE("coordinates: polar input is degrees") {
    // The command line takes degrees, while AutoLISP takes radians and the DXF
    // file stores degrees. Three boundaries; this is the one a person types at.
    ScriptFixture f;
    f.run("LINE 0,0 @10<90\n\n");
    CHECK_VEC3(f.line_at(0)->end(), Vec3(0.0, 10.0, 0.0));

    ScriptFixture g;
    g.run("LINE 0,0 @10<0 @10<45\n\n");
    CHECK_VEC3(g.line_at(1)->end(),
               Vec3(10.0 + 10.0 * 0.70710678118654752, 10.0 * 0.70710678118654752, 0.0));

    // Absolute polar, measured from the origin.
    ScriptFixture h;
    h.run("LINE 0,0 20<180\n\n");
    CHECK_VEC3(h.line_at(0)->end(), Vec3(-20.0, 0.0, 0.0));
}

TEST_CASE("coordinates: bare @ is the last point itself") {
    ScriptFixture f;
    f.run("LINE 0,0 10,10\n\n");
    // A zero-length segment, but the parse is what is under test.
    f.run("LINE @ 20,20\n\n");
    CHECK_VEC3(f.line_at(1)->start(), Vec3(10.0, 10.0, 0.0));
}

TEST_CASE("coordinates: the last point outlives the command that set it") {
    // LASTPOINT is engine state, not command state, so @ works across commands.
    ScriptFixture f;
    f.run("CIRCLE 30,40 5\n");
    CHECK(f.engine.has_last_point());
    CHECK_VEC3(f.engine.last_point(), Vec3(30.0, 40.0, 0.0));

    f.run("LINE @ @10,0\n\n");
    const Line* line = static_cast<const Line*>(f.db.get(f.db.order()[1]));
    CHECK_VEC3(line->start(), Vec3(30.0, 40.0, 0.0));
    CHECK_VEC3(line->end(), Vec3(40.0, 40.0, 0.0));
}

TEST_CASE("coordinates: @ without a last point is reported") {
    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("LINE"));
    CHECK(!engine.has_last_point());

    TextInputSource src(tokenize_script("@5,0"));
    CHECK(engine.run(src) == EngineStatus::Waiting);
    CHECK(src.failed());
    CHECK(src.error().find("last point") != std::string::npos);
    // The command is untouched and still asking.
    CHECK(engine.active());
}

TEST_CASE("coordinates: a relative point answers a distance prompt") {
    // CIRCLE radius by dragging: @5,0 from the centre is a radius of 5.
    ScriptFixture f;
    f.run("CIRCLE 10,10 @5,0\n");
    CHECK_NEAR(static_cast<const Circle*>(f.db.get(f.db.order()[0]))->radius(), 5.0, 1e-12);
}

TEST_CASE("coordinates: malformed relative input is rejected") {
    ScriptFixture f;
    f.run("LINE 0,0 5,5\n\n");

    Database db;
    CommandEngine engine(db);
    engine.set_last_point(Vec3(1.0, 1.0, 0.0));
    engine.begin(make_command("LINE"));

    TextInputSource bad(tokenize_script("@nonsense"));
    CHECK(engine.run(bad) == EngineStatus::Waiting);
    CHECK(bad.failed());

    TextInputSource bad_polar(tokenize_script("@10<"));
    CHECK(engine.run(bad_polar) == EngineStatus::Waiting);
    CHECK(bad_polar.failed());
}

TEST_CASE("lisp: the prompt gets first refusal over the command registry") {
    // (command "DXFOUT" "LINE") writes LINE.dxf. It must not abandon DXFOUT to
    // start the LINE command: the running prompt wants a file name, and "LINE"
    // is a perfectly good one.
    LispFixture f;
    f.eval(R"((command "CIRCLE" '(0 0 0) 5.0))");
    CHECK(f.db.size() == 1);

    std::remove("LINE.dxf");
    // The trailing "" is Enter at the DXF-version prompt: keep DXFVERSION as
    // it stands.
    f.eval(R"((command "DXFOUT" "LINE" ""))");
    CHECK(!f.engine.active());  // DXFOUT ran to completion

    std::ifstream written("LINE.dxf", std::ios::binary);
    CHECK(written.good());
    written.close();
    std::remove("LINE.dxf");

    // Still only the one circle: no LINE command ever started.
    CHECK(f.db.size() == 1);
}

TEST_CASE("lisp: a keyword beats a command name at the same prompt") {
    // The general form of the rule. A string usable by the running prompt is
    // consumed there, whatever the command registry happens to contain.
    LispFixture f;
    f.eval(R"((command "LINE" '(0 0 0) '(10 0 0) '(10 10 0) "C"))");
    CHECK(f.db.size() == 3);  // closed, not three separate starts
    CHECK(!f.engine.active());
}

TEST_CASE("lisp: a command name still starts a command where the prompt cannot use it") {
    // The fallback has to keep working, or several commands in one call breaks.
    LispFixture f;
    f.eval(R"((command "LINE" '(0 0 0) '(1 0 0) "" "CIRCLE" '(5 5 0) 2.0))");
    CHECK(f.db.size() == 2);
    CHECK(f.db.get(f.db.order()[0])->type() == EntityType::Line);
    CHECK(f.db.get(f.db.order()[1])->type() == EntityType::Circle);

    // And a command name arriving mid-command abandons it, as R12 does.
    LispFixture g;
    g.eval(R"((command "LINE" '(0 0 0) "CIRCLE" '(0 0 0) 1.0))");
    CHECK(g.db.size() == 1);
    CHECK(g.db.get(g.db.order()[0])->type() == EntityType::Circle);
}

TEST_CASE("input: which prompts can be answered by pointing") {
    // One rule, asked by the text parser and the viewport alike. A magnitude and
    // a direction are both things you show rather than type; a row count and a
    // file name are not.
    CHECK(prompt_takes_point(PromptKind::Point));
    CHECK(prompt_takes_point(PromptKind::Distance));
    CHECK(prompt_takes_point(PromptKind::Angle));

    CHECK(!prompt_takes_point(PromptKind::Real));
    CHECK(!prompt_takes_point(PromptKind::Integer));
    CHECK(!prompt_takes_point(PromptKind::String));
    // An entity prompt takes a handle, not a location.
    CHECK(!prompt_takes_point(PromptKind::Entity));
}

TEST_CASE("input: a distance or an angle can be given as a coordinate") {
    std::string err;
    InputValue v;

    Prompt d;
    d.kind = PromptKind::Distance;
    d.base = Vec3{1, 1, 0};
    d.has_base = true;

    // A number is still a number.
    CHECK(parse_input(d, "5", v, err));
    CHECK(v.kind == InputKind::Real);

    // And a coordinate is a point, which the command turns into a distance from
    // its own base -- the same value a click would have produced.
    CHECK(parse_input(d, "4,3", v, err));
    CHECK(v.kind == InputKind::Point);
    CHECK_VEC(v.point, 4.0, 3.0, 0.0, 1e-12);

    Prompt a;
    a.kind = PromptKind::Angle;
    a.base = Vec3{0, 0, 0};
    a.has_base = true;
    CHECK(parse_input(a, "45", v, err));
    CHECK(v.kind == InputKind::Real);
    CHECK(parse_input(a, "10,10", v, err));
    CHECK(v.kind == InputKind::Point);
}

TEST_CASE("circle: the radius can be given by pointing, and D means diameter") {
    Database db;
    CommandEngine engine(db);

    // Radius by picking a point the circle should pass through.
    engine.begin(make_command("CIRCLE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    CHECK(engine.prompt().kind == PromptKind::Distance);
    CHECK(engine.prompt().has_base);
    CHECK_VEC(engine.prompt().base, 0.0, 0.0, 0.0, 1e-12);
    engine.supply(InputValue::of_point({3, 4, 0}));
    CHECK(engine.status() == EngineStatus::Finished);

    const Entity* e = db.get(db.order().back());
    CHECK(e->type() == EntityType::Circle);
    CHECK_NEAR(static_cast<const Circle*>(e)->radius(), 5.0, 1e-12);

    // Radius is the default; D switches to diameter, and halves it.
    engine.begin(make_command("CIRCLE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(7.0));
    CHECK_NEAR(static_cast<const Circle*>(db.get(db.order().back()))->radius(), 7.0, 1e-12);

    engine.begin(make_command("CIRCLE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_keyword("DIAMETER"));
    engine.supply(InputValue::of_real(10.0));
    CHECK_NEAR(static_cast<const Circle*>(db.get(db.order().back()))->radius(), 5.0, 1e-12);

    // And a picked diameter is measured across, not from the centre.
    engine.begin(make_command("CIRCLE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_keyword("DIAMETER"));
    engine.supply(InputValue::of_point({3, 4, 0}));
    CHECK_NEAR(static_cast<const Circle*>(db.get(db.order().back()))->radius(), 2.5, 1e-12);
}

TEST_CASE("engine: a transparent command does not outlive the command that hosted it") {
    // The wedge. supply() routes to `transparent_` whenever it is set and the
    // status is Waiting, so a transparent command left installed silently eats
    // the next value meant for whatever came after -- and apply_transparent
    // then restores `outer_prompt_`, the question asked by a command that no
    // longer exists. The prompt and the engine disagree about what is running,
    // and every command after that answers the wrong question.
    //
    // Reachable from LISP, which is how it was found: start a command, run a
    // transparent one at its prompt, then have (command ...) begin something
    // else. a684f9f left "a command stuck at Waiting swallowing input" as one
    // of two open explanations for a command line that had started failing; this
    // is that mechanism.
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_point({1, 1, 0}));
    REQUIRE(engine.status() == EngineStatus::Waiting);

    // ID is transparent and needs no view, which is what makes this testable
    // without a display.
    engine.begin_transparent(make_command("ID"));
    REQUIRE(engine.in_transparent());

    // Something else begins while the transparent command is still up.
    engine.begin(make_command("CIRCLE"));
    CHECK(!engine.in_transparent());

    // And CIRCLE gets its own values, rather than ID eating the first one.
    engine.supply(InputValue::of_point({5, 5, 0}));
    engine.supply(InputValue::of_real(3.0));
    CHECK(engine.status() == EngineStatus::Finished);
    REQUIRE(db.size() == 1);
    CHECK(db.get(db.order().back())->type() == EntityType::Circle);
}

TEST_CASE("engine: a value with nothing to receive it disarms the transparent command") {
    // The other way it leaked. When supply() cannot route -- no command, or a
    // status that is not Waiting -- it reported the failure and returned with
    // `transparent_` still installed, so the next thing to put the engine back
    // into Waiting had its first value stolen.
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("LINE"));
    engine.supply(InputValue::of_point({1, 1, 0}));
    engine.begin_transparent(make_command("ID"));
    REQUIRE(engine.in_transparent());

    engine.cancel();
    CHECK(!engine.in_transparent());

    // Nothing is running, so this is a plain error -- and it must not leave
    // anything armed behind it.
    engine.supply(InputValue::of_point({2, 2, 0}));
    CHECK(engine.status() == EngineStatus::Failed);
    CHECK(!engine.in_transparent());

    // The engine still works afterwards, which is the whole point.
    engine.begin(make_command("CIRCLE"));
    engine.supply(InputValue::of_point({0, 0, 0}));
    engine.supply(InputValue::of_real(2.0));
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(db.size() == 1);
}
