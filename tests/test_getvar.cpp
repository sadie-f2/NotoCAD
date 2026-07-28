// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "noto/database.hpp"
#include "noto/lisp/eval.hpp"
#include "noto/sysvar.hpp"

#include <sstream>
#include <string>

using namespace noto;
using namespace noto::lisp;

namespace {

struct Fixture {
    Database db;
    Context ctx;
    Interp in{ctx};
    std::ostringstream out;

    Fixture() {
        in.set_output(&out);
        in.set_database(&db);
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
};

}  // namespace

TEST_CASE("getvar: reads the R12 defaults") {
    Fixture f;
    CHECK(f.eval("(getvar \"OSMODE\")") == "53");
    CHECK(f.eval("(getvar \"PICKBOX\")") == "3");
    CHECK(f.eval("(getvar \"APERTURE\")") == "10");
}

TEST_CASE("getvar: names are case-insensitive") {
    Fixture f;
    CHECK(f.eval("(setvar \"osmode\" 47)") == "47");
    CHECK(f.eval("(getvar \"OsMode\")") == "47");
}

TEST_CASE("setvar: the change is visible on the C++ side") {
    Fixture f;
    CHECK(f.eval("(setvar \"OSMODE\" 47)") == "47");
    CHECK(f.db.sysvars().get_int(Sysvar::OsMode) == 47);

    // And the other way: what the kernel sets, LISP reads.
    CHECK(f.db.sysvars().set_int(Sysvar::PickBox, 9) == Sysvars::SetStatus::Ok);
    CHECK(f.eval("(getvar \"PICKBOX\")") == "9");
}

TEST_CASE("getvar: an unknown name is nil, not an error") {
    Fixture f;
    CHECK(f.eval("(getvar \"NOSUCHVAR\")") == "nil");
    CHECK(!f.failed("(getvar \"NOSUCHVAR\")"));

    // Which is the point: LISP can probe for a variable and carry on.
    CHECK(f.eval("(if (getvar \"NOSUCHVAR\") \"yes\" \"no\")") == "\"no\"");
}

TEST_CASE("setvar: an unknown name is an error") {
    Fixture f;
    CHECK(f.failed("(setvar \"NOSUCHVAR\" 1)"));
}

TEST_CASE("setvar: out of range fails and leaves the value alone") {
    Fixture f;
    CHECK(f.failed("(setvar \"PICKBOX\" 900)"));
    CHECK(f.db.sysvars().get_int(Sysvar::PickBox) == 3);
    CHECK(f.eval("(getvar \"PICKBOX\")") == "3");
}

TEST_CASE("setvar: a real is not truncated into an integer variable") {
    Fixture f;
    CHECK(f.failed("(setvar \"OSMODE\" 47.5)"));
    CHECK(f.failed("(setvar \"OSMODE\" \"47\")"));
    CHECK(f.db.sysvars().get_int(Sysvar::OsMode) == 53);
}

TEST_CASE("getvar and setvar: the name must be a string") {
    Fixture f;
    CHECK(f.failed("(getvar 'OSMODE)"));
    CHECK(f.failed("(getvar 1)"));
    CHECK(f.failed("(setvar 'OSMODE 1)"));
}

TEST_CASE("setvar: OSMODE composed from bits, read back by LISP") {
    Fixture f;
    // END|MID|CEN|NOD|INT, built the way a startup file would.
    CHECK(f.eval("(setvar \"OSMODE\" (+ 1 2 4 8 32))") == "47");
    CHECK(f.eval("(getvar \"OSMODE\")") == "47");
    CHECK(f.db.sysvars().get_int(Sysvar::OsMode) == 47);

    // Turning snapping off again, which is what an OSNAP command will do.
    CHECK(f.eval("(setvar \"OSMODE\" 0)") == "0");
    CHECK(f.db.sysvars().get_int(Sysvar::OsMode) == 0);
}
