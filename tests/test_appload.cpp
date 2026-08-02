// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// APPLOAD and (load ...): getting a .lsp file into a session that is already
// running, rather than only at startup. Both go through the same
// load_lisp_file underneath (file_subrs.hpp) -- these tests are as much about
// that sharing as about either surface alone.

#include "test.hpp"

#include "ncad/command.hpp"
#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/lisp/eval.hpp"
#include "ncad/lisp/interp_script_loader.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using namespace ncad;
using namespace ncad::lisp;

namespace {

std::string temp_dir() {
    const std::filesystem::path p =
        std::filesystem::temp_directory_path() / "ncad_appload_tests";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p.string();
}

std::string temp_path(const char* leaf) { return temp_dir() + "/" + leaf; }

void write_file(const std::string& path, const std::string& contents) {
    std::ofstream f(path, std::ios::binary);
    f << contents;
}

// Database, engine and a real interpreter wired together the way `ncad` wires
// them in main() -- the fixture this feature exists to make possible.
struct SessionFixture {
    Database db;
    CommandEngine engine{db};
    Context ctx;
    Interp interp{ctx};
    InterpScriptLoader loader{interp};

    SessionFixture() {
        interp.set_database(&db);
        interp.set_command_engine(&engine);
        engine.set_script_loader(&loader);
    }
};

}  // namespace

TEST_CASE("appload: loading a file makes its defun visible in the session") {
    const std::string path = temp_path("defines.lsp");
    write_file(path, "(defun triple (x) (* 3 x))");

    SessionFixture s;
    s.engine.begin(make_command("APPLOAD"));
    const EngineStatus status = s.engine.supply(InputValue::of_string(path));

    CHECK(status == EngineStatus::Finished);

    Value result;
    REQUIRE(s.interp.eval_string("(triple 7)", result));
    CHECK(result.type == Type::Int);
    CHECK(result.i == 21);
}

TEST_CASE("appload: the .lsp extension is added when missing, like DXFIN/DXFOUT") {
    const std::string path = temp_path("noext");
    write_file(path + ".lsp", "(setq appload-marker 1)");

    SessionFixture s;
    s.engine.begin(make_command("APPLOAD"));
    const EngineStatus status = s.engine.supply(InputValue::of_string(path));

    CHECK(status == EngineStatus::Finished);

    Value result;
    REQUIRE(s.interp.eval_string("appload-marker", result));
    CHECK(result.type == Type::Int);
    CHECK(result.i == 1);
}

TEST_CASE("appload: a missing file fails the command rather than the session") {
    SessionFixture s;
    s.engine.begin(make_command("APPLOAD"));
    const EngineStatus status = s.engine.supply(InputValue::of_string(temp_path("nope.lsp")));

    CHECK(status == EngineStatus::Failed);
}

TEST_CASE("appload: a bad form inside the file fails too, not just a missing one") {
    const std::string path = temp_path("broken.lsp");
    write_file(path, "(this-function-does-not-exist)");

    SessionFixture s;
    s.engine.begin(make_command("APPLOAD"));
    const EngineStatus status = s.engine.supply(InputValue::of_string(path));

    CHECK(status == EngineStatus::Failed);
}

TEST_CASE("appload: with no interpreter attached, fails honestly rather than crashing") {
    // Mirrors ViewControl: a null ScriptLoader is the truth for a CommandEngine
    // nobody wired one into, same as `view` for one nobody gave a viewport.
    Database db;
    CommandEngine engine(db);

    engine.begin(make_command("APPLOAD"));
    const EngineStatus status = engine.supply(InputValue::of_string(temp_path("whatever.lsp")));

    CHECK(status == EngineStatus::Failed);
}

TEST_CASE("(load ...): T on success, and the file's defun is callable after") {
    const std::string path = temp_path("load-fn.lsp");
    write_file(path, "(defun doubled (x) (* 2 x))");

    Context ctx;
    Interp in(ctx);

    Value result;
    REQUIRE(in.eval_string("(load \"" + path + "\")", result));
    CHECK(result.type == Type::True);

    REQUIRE(in.eval_string("(doubled 5)", result));
    CHECK(result.type == Type::Int);
    CHECK(result.i == 10);
}

TEST_CASE("(load ...): nil, not an error, for a file that cannot be opened") {
    Context ctx;
    Interp in(ctx);

    Value result;
    REQUIRE(in.eval_string("(load \"" + temp_path("missing.lsp") + "\")", result));
    CHECK(result.type == Type::Nil);
}

TEST_CASE("(load ...): an error inside the file is a real error, not nil") {
    const std::string path = temp_path("load-broken.lsp");
    write_file(path, "(this-function-does-not-exist)");

    Context ctx;
    Interp in(ctx);

    Value result;
    CHECK(!in.eval_string("(load \"" + path + "\")", result));
}
