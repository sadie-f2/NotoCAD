// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/command.hpp"
#include "noto/commands.hpp"
#include "noto/database.hpp"
#include "noto/entities.hpp"
#include "noto/lisp/eval.hpp"

#include <memory>
#include <sstream>
#include <string>

using namespace noto;
using namespace noto::lisp;

namespace {

struct Fixture {
    Database db;
    CommandEngine engine{db};
    Context ctx;
    Interp in{ctx};
    std::ostringstream out;

    Fixture() {
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

    bool failed(const std::string& source) {
        in.clear_error();
        Value v;
        return !in.eval_string(source, v);
    }

    Handle line(double y) {
        return db.add(std::make_unique<Line>(Vec3{0, y, 0}, Vec3{10, y, 0}));
    }
};

}  // namespace

TEST_CASE("ssget: X takes the whole drawing") {
    Fixture f;
    f.line(0);
    f.line(5);
    CHECK(f.eval("(sslength (ssget \"X\"))") == "2");
}

TEST_CASE("ssget: an empty result is nil, as R12 has it") {
    Fixture f;
    // Nothing in the drawing at all.
    CHECK(f.eval("(ssget \"X\")") == "nil");

    // And a box that catches nothing.
    f.line(0);
    CHECK(f.eval("(ssget \"C\" '(100 100) '(110 110))") == "nil");

    // Which is what makes the idiomatic guard work.
    CHECK(f.eval("(if (ssget \"C\" '(100 100) '(110 110)) \"some\" \"none\")") == "\"none\"");
}

TEST_CASE("ssget: window and crossing differ as they do at the prompt") {
    Fixture f;
    f.line(0);
    f.line(100);

    // A box cutting the first line without enclosing it.
    CHECK(f.eval("(sslength (ssget \"C\" '(4 -1) '(6 1)))") == "1");
    CHECK(f.eval("(ssget \"W\" '(4 -1) '(6 1))") == "nil");

    // And one that encloses it.
    CHECK(f.eval("(sslength (ssget \"W\" '(-1 -1) '(11 1)))") == "1");
}

TEST_CASE("ssget: two-element points are accepted") {
    Fixture f;
    f.line(0);
    // AutoLISP points may omit z, and drawings written in 2D always do.
    CHECK(f.eval("(sslength (ssget \"C\" '(4 -1) '(6 1)))") == "1");
    CHECK(f.eval("(sslength (ssget \"C\" '(4 -1 0) '(6 1 0)))") == "1");
}

TEST_CASE("ssget: L is the newest entity") {
    Fixture f;
    f.line(0);
    const Handle newest = f.line(5);
    CHECK(f.eval("(sslength (ssget \"L\"))") == "1");
    CHECK(f.eval("(ssname (ssget \"L\") 0)") == prin1(make_ename(newest)));
}

TEST_CASE("ssget: P is the engine's previous selection") {
    Fixture f;
    const Handle a = f.line(0);
    f.line(5);

    // Build a selection through the engine and leave it behind.
    f.engine.begin(make_command("ERASE"));
    f.engine.supply(InputValue::of_entity(a));
    f.engine.supply(InputValue::cancel());
    f.engine.begin(make_command("LINE"));
    f.engine.supply(InputValue::cancel());

    CHECK(f.eval("(sslength (ssget \"P\"))") == "1");
}

TEST_CASE("ssget: an unknown mode is an error") {
    Fixture f;
    f.line(0);
    CHECK(f.failed("(ssget \"Z\")"));
    CHECK(f.failed("(ssget 7)"));
}

TEST_CASE("ssget: interactive selection says so rather than doing nothing") {
    Fixture f;
    f.line(0);
    // Better a clear refusal than an empty set that looks like "found nothing".
    CHECK(f.failed("(ssget)"));
}

TEST_CASE("ssget: a window needs both corners") {
    Fixture f;
    f.line(0);
    CHECK(f.failed("(ssget \"W\")"));
    CHECK(f.failed("(ssget \"W\" '(0 0))"));
}

TEST_CASE("ssname: walking a set by index, nil past the end") {
    Fixture f;
    f.line(0);
    f.line(5);

    CHECK(f.eval("(setq ss (ssget \"X\"))") != "nil");
    CHECK(f.eval("(type (ssname ss 0))") == "ENAME");
    CHECK(f.eval("(type (ssname ss 1))") == "ENAME");
    // Out of range is nil rather than an error: walking until nil is the loop.
    CHECK(f.eval("(ssname ss 2)") == "nil");
    CHECK(f.eval("(ssname ss -1)") == "nil");
}

TEST_CASE("ssadd: builds a set entity by entity") {
    Fixture f;
    f.line(0);
    f.line(5);

    // An empty set to start from.
    CHECK(f.eval("(setq ss (ssadd))") != "nil");
    CHECK(f.eval("(sslength ss)") == "0");

    CHECK(f.eval("(ssadd (entnext) ss)") != "nil");
    CHECK(f.eval("(sslength ss)") == "1");

    // Adding the same entity twice does not grow it.
    CHECK(f.eval("(ssadd (entnext) ss)") != "nil");
    CHECK(f.eval("(sslength ss)") == "1");

    CHECK(f.eval("(ssadd (entlast) ss)") != "nil");
    CHECK(f.eval("(sslength ss)") == "2");
}

TEST_CASE("ssadd: one argument makes a new single-entity set") {
    Fixture f;
    f.line(0);
    CHECK(f.eval("(sslength (ssadd (entlast)))") == "1");
}

TEST_CASE("ssdel: removes, and reports whether it did") {
    Fixture f;
    f.line(0);
    f.line(5);

    CHECK(f.eval("(setq ss (ssget \"X\"))") != "nil");
    CHECK(f.eval("(ssdel (entlast) ss)") != "nil");
    CHECK(f.eval("(sslength ss)") == "1");

    // Removing something that is not there is nil, which is how you test it.
    CHECK(f.eval("(ssdel (entlast) ss)") == "nil");
    CHECK(f.eval("(sslength ss)") == "1");
}

TEST_CASE("ssmemb: membership") {
    Fixture f;
    f.line(0);
    f.line(5);
    CHECK(f.eval("(setq ss (ssadd (entnext)))") != "nil");
    CHECK(f.eval("(if (ssmemb (entnext) ss) \"yes\" \"no\")") == "\"yes\"");
    CHECK(f.eval("(if (ssmemb (entlast) ss) \"yes\" \"no\")") == "\"no\"");
}

TEST_CASE("ssget: a set is shared, not copied") {
    Fixture f;
    f.line(0);
    f.line(5);

    // Two variables naming one set see the same thing, as AutoLISP does.
    CHECK(f.eval("(setq a (ssget \"X\"))") != "nil");
    CHECK(f.eval("(setq b a)") != "nil");
    CHECK(f.eval("(ssdel (entlast) a)") != "nil");
    CHECK(f.eval("(sslength b)") == "1");
}

TEST_CASE("ssget: the accessors reject things that are not sets") {
    Fixture f;
    f.line(0);
    CHECK(f.failed("(sslength 7)"));
    CHECK(f.failed("(ssname \"x\" 0)"));
    CHECK(f.failed("(ssadd 7 7)"));
    CHECK(f.failed("(ssdel (entlast) 7)"));
}

TEST_CASE("ssget: a selection set is not valid input to a command") {
    Fixture f;
    f.line(0);
    // R12 has no way to hand one to (command ...) either; you name the
    // entities, or you use Previous.
    CHECK(f.failed("(command \"ERASE\" (ssget \"X\") \"\")"));
}
