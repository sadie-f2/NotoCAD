// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The seams a module outside ncad_core uses: registered commands and registered
// system variables. Nothing here names a real extension -- the point is that the
// core offers the mechanism without knowing who uses it.

#include "test.hpp"

#include "ncad/command.hpp"
#include "ncad/commands.hpp"
#include "ncad/database.hpp"
#include "ncad/input_text.hpp"
#include "ncad/lisp/eval.hpp"
#include "ncad/sysvar.hpp"

#include <memory>
#include <sstream>
#include <string>

using namespace ncad;
using namespace ncad::lisp;

namespace {

// Registrations are process-global, so each case cleans up after itself or the
// registry test (whose count must equal the version) would see leftovers.
struct Cleanup {
    ~Cleanup() {
        clear_registered_commands();
        clear_registered_sysvars();
    }
};

// Asks for one string and reports it back. `finished` counts completions: the
// factory count would not do, because (command ...) probes a name by making the
// command.
class EchoCommand final : public Command {
public:
    explicit EchoCommand(int* finished = nullptr) : finished_(finished) {}
    const char* name() const override { return "ECHOEXT"; }
    Step start(CommandContext&) override {
        Prompt p;
        p.kind = PromptKind::String;
        p.message = "Text";
        return Step::ask(p);
    }
    Step next(CommandContext&, const InputValue& v) override {
        if (finished_) ++*finished_;
        return Step::done("echo " + v.text);
    }

private:
    int* finished_;
};

}  // namespace

TEST_CASE("extension: a registered command is made, listed and resolved") {
    Cleanup cleanup;
    CHECK(make_command("ECHOEXT") == nullptr);

    CHECK(register_command("echoext", [] { return std::make_unique<EchoCommand>(); }));
    CommandPtr c = make_command("echoext");
    CHECK(c != nullptr);
    CHECK(std::string(c->name()) == "ECHOEXT");

    bool listed = false;
    for (const std::string& n : command_names()) listed = listed || n == "ECHOEXT";
    CHECK(listed);
    CHECK(resolve_command_name("ECHO").name == "ECHOEXT");
    CHECK(resolve_command_name("ECHOEXT").name == "ECHOEXT");

    clear_registered_commands();
    CHECK(make_command("ECHOEXT") == nullptr);
    CHECK(resolve_command_name("ECHOEXT").name.empty());
}

TEST_CASE("extension: a built-in name cannot be taken, and a repeat replaces") {
    Cleanup cleanup;
    CHECK(!register_command("LINE", [] { return std::make_unique<EchoCommand>(); }));
    CHECK(!register_command("", [] { return std::make_unique<EchoCommand>(); }));
    CHECK(!register_command("NOFACTORY", nullptr));
    CHECK(std::string(make_command("LINE")->name()) == "LINE");

    const std::size_t before = command_names().size();
    CHECK(register_command("ECHOEXT", [] { return std::make_unique<EchoCommand>(); }));
    CHECK(register_command("ECHOEXT", [] { return std::make_unique<EchoCommand>(); }));
    CHECK(command_names().size() == before + 1);
}

TEST_CASE("extension: a registered command runs through the engine and from LISP") {
    Cleanup cleanup;
    int finished = 0;
    register_command("ECHOEXT", [&finished] { return std::make_unique<EchoCommand>(&finished); });

    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("ECHOEXT"));
    engine.supply(InputValue::of_string("hi"));
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(finished == 1);

    Context ctx;
    Interp in{ctx};
    std::ostringstream out;
    in.set_output(&out);
    in.set_database(&db);
    in.set_command_engine(&engine);
    Value v;
    CHECK(in.eval_string(R"((command "ECHOEXT" "there"))", v));
    CHECK(finished == 2);
}

TEST_CASE("extension: a registered sysvar reads, writes, and is case-insensitive") {
    Cleanup cleanup;
    SysvarSpec spec;
    spec.name = "extint";
    spec.type = SysvarType::Int;
    spec.int_default = 7;
    spec.int_min = 0;
    spec.int_max = 10;
    Sysvar id{};
    CHECK(register_sysvar(spec, id));
    CHECK(static_cast<std::size_t>(id) >= static_cast<std::size_t>(Sysvar::kCount));

    // A Database made BEFORE the registration would also work; one made after
    // certainly does.
    Database db;
    CHECK(db.sysvars().get_int(id) == 7);
    CHECK(find_sysvar("EXTINT") != nullptr);
    CHECK(std::string(find_sysvar("ExtInt")->name) == "EXTINT");

    SysvarValue v;
    CHECK(db.sysvars().get("extint", v));
    CHECK(v.integer == 7);
    CHECK(db.sysvars().set("EXTINT", SysvarValue::of_int(9)) == Sysvars::SetStatus::Ok);
    CHECK(db.sysvars().get_int(id) == 9);
    CHECK(db.sysvars().set("EXTINT", SysvarValue::of_int(11)) == Sysvars::SetStatus::OutOfRange);
    CHECK(db.sysvars().set("EXTINT", SysvarValue::of_string("x")) == Sysvars::SetStatus::WrongType);
}

TEST_CASE("extension: registering is idempotent, and refuses clashes") {
    Cleanup cleanup;
    SysvarSpec spec;
    spec.name = "EXTREAL";
    spec.type = SysvarType::Real;
    Sysvar a{}, b{};
    CHECK(register_sysvar(spec, a));
    CHECK(register_sysvar(spec, b));
    CHECK(a == b);

    SysvarSpec other = spec;
    other.type = SysvarType::Int;
    CHECK(!register_sysvar(other, b));

    SysvarSpec builtin = spec;
    builtin.name = "osmode";
    CHECK(!register_sysvar(builtin, b));
    SysvarSpec empty = spec;
    empty.name.clear();
    CHECK(!register_sysvar(empty, b));
}

TEST_CASE("extension: a Database created before registration still answers") {
    Cleanup cleanup;
    Database db;
    SysvarSpec spec;
    spec.name = "EXTLATE";
    spec.type = SysvarType::String;
    spec.string_default = "dflt";
    Sysvar id{};
    CHECK(register_sysvar(spec, id));
    CHECK(db.sysvars().get_string(id) == "dflt");
    CHECK(db.sysvars().set_string(id, "later") == Sysvars::SetStatus::Ok);
    CHECK(db.sysvars().get_string(id) == "later");
}

TEST_CASE("extension: a registered sysvar is undoable as a built-in is") {
    Cleanup cleanup;
    SysvarSpec spec;
    spec.name = "EXTUNDO";
    spec.type = SysvarType::Int;
    spec.int_default = 1;
    spec.int_max = 100;
    Sysvar id{};
    register_sysvar(spec, id);

    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("SETVAR"));
    engine.supply(InputValue::of_string("EXTUNDO"));
    engine.supply(InputValue::of_integer(42));
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(db.sysvars().get_int(id) == 42);

    engine.begin(make_command("UNDO"));
    CHECK(db.sysvars().get_int(id) == 1);
}

TEST_CASE("extension: a read-only registered sysvar refuses the user but not its owner") {
    Cleanup cleanup;
    SysvarSpec spec;
    spec.name = "EXTRO";
    spec.type = SysvarType::Int;
    spec.read_only = true;
    spec.int_max = 5;
    Sysvar id{};
    register_sysvar(spec, id);

    Database db;
    CHECK(db.sysvars().set("EXTRO", SysvarValue::of_int(3)) == Sysvars::SetStatus::ReadOnly);
    CHECK(db.sysvars().set_owned(id, SysvarValue::of_int(3)) == Sysvars::SetStatus::Ok);
    CHECK(db.sysvars().get_int(id) == 3);
}

TEST_CASE("extension: getvar and setvar from LISP reach a registered sysvar") {
    Cleanup cleanup;
    SysvarSpec spec;
    spec.name = "EXTSTR";
    spec.type = SysvarType::String;
    spec.string_default = "abc";
    Sysvar id{};
    register_sysvar(spec, id);

    Database db;
    Context ctx;
    Interp in{ctx};
    std::ostringstream out;
    in.set_output(&out);
    in.set_database(&db);
    Value v;
    CHECK(in.eval_string(R"((getvar "extstr"))", v));
    CHECK(prin1(v) == "\"abc\"");
    CHECK(in.eval_string(R"((setvar "EXTSTR" "xyz"))", v));
    CHECK(db.sysvars().get_string(id) == "xyz");
}

TEST_CASE("extension: SETVAR ? lists registered variables") {
    Cleanup cleanup;
    SysvarSpec spec;
    spec.name = "EXTLIST";
    Sysvar id{};
    register_sysvar(spec, id);

    Database db;
    CommandEngine engine(db);
    engine.begin(make_command("SETVAR"));
    engine.supply(InputValue::of_string("?"));
    CHECK(engine.status() == EngineStatus::Finished);
    CHECK(engine.message().find("EXTLIST") != std::string::npos);
}
