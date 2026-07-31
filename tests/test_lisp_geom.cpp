// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// polar, distance, angle, inters and osnap -- the geometry helpers AutoLISP
// scripts are actually written in.
//
// These came before the larger gaps because polar, distance and angle appear in
// very nearly every drafting script ever written: they are how a script says "a
// point 40 units at 30 degrees from there". Without them a script fails on its
// third line no matter what else is present.

#include "test.hpp"

#include "ncad/database.hpp"
#include "ncad/entities.hpp"
#include "ncad/command.hpp"
#include "ncad/lisp/eval.hpp"

#include <memory>
#include <sstream>
#include <string>

using namespace ncad;
using namespace ncad::lisp;

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
        // (command ...) needs the engine, not just the drawing -- the whole
        // point of the design being that a LISP call and a keystroke drive the
        // same state machine.
        in.set_command_engine(&engine);
    }

    std::string eval(const std::string& source) {
        in.clear_error();
        Value v;
        if (!in.eval_string(source, v)) return "<error: " + in.error().message() + ">";
        return prin1(v);
    }

    double real(const std::string& source) {
        in.clear_error();
        Value v;
        if (!in.eval_string(source, v)) return -1e30;
        return is_number(v) ? as_double(v) : -1e30;
    }
};

}  // namespace

TEST_CASE("lisp polar: distance and angle from a point, in world XY") {
    Fixture f;
    CHECK(f.eval("(polar '(10.0 10.0 0.0) 0.0 5.0)") == "(15.0 10.0 0.0)");
    CHECK_NEAR(f.real("(cadr (polar '(10.0 10.0 0.0) (/ pi 2) 5.0))"), 15.0, 1e-9);

    // Z is carried through, not zeroed: a script working at a height stays at
    // it, which is what every use of this in a 3D drawing assumes.
    CHECK_NEAR(f.real("(caddr (polar '(0.0 0.0 7.0) 1.0 5.0))"), 7.0, 1e-12);

    // Two coordinates is a legal point and means Z = 0, as everywhere else.
    CHECK(f.eval("(polar '(0.0 0.0) 0.0 3.0)") == "(3.0 0.0 0.0)");
}

TEST_CASE("lisp distance: true 3D, not flattened") {
    Fixture f;
    CHECK_NEAR(f.real("(distance '(0 0 0) '(3.0 4.0 0))"), 5.0, 1e-12);
    // R12 flattened this when FLATLAND was set. FLATLAND was a compatibility
    // switch for drawings older than this program pretends to be.
    CHECK_NEAR(f.real("(distance '(0 0 0) '(0 0 9.0))"), 9.0, 1e-12);
    CHECK_NEAR(f.real("(distance '(1.0 2.0 3.0) '(1.0 2.0 3.0))"), 0.0, 1e-12);
}

TEST_CASE("lisp angle: the inverse of polar, normalised to a positive turn") {
    Fixture f;
    CHECK_NEAR(f.real("(angle '(0 0 0) '(1.0 0 0))"), 0.0, 1e-12);
    CHECK_NEAR(f.real("(angle '(0 0 0) '(0 1.0 0))"), 1.5707963267948966, 1e-12);

    // Round trip: angle undoes polar. This is the property scripts rely on.
    CHECK_NEAR(f.real("(angle '(5.0 5.0 0) (polar '(5.0 5.0 0) 0.7 3.0))"), 0.7, 1e-9);

    // Never negative. A script comparing angles should not have to know that
    // atan2 returns them.
    CHECK(f.real("(angle '(0 0 0) '(-1.0 -1.0 0))") > 3.0);
    CHECK(f.real("(angle '(0 0 0) '(1.0 -1.0 0))") > 4.0);
}

TEST_CASE("lisp inters: the fifth argument is the one people get backwards") {
    Fixture f;
    // Crossing segments.
    CHECK(f.eval("(inters '(0 0 0) '(10.0 10.0 0) '(0 10.0 0) '(10.0 0 0))") ==
          "(5.0 5.0 0.0)");

    // The same lines, but the first segment stops short. Omitted fifth argument
    // means "must be on both segments", so this is nil...
    CHECK(f.eval("(inters '(0 0 0) '(1.0 1.0 0) '(0 10.0 0) '(10.0 0 0))") == "nil");

    // ...and an explicit nil means the lines are infinite, so it is not.
    CHECK(f.eval("(inters '(0 0 0) '(1.0 1.0 0) '(0 10.0 0) '(10.0 0 0) nil)") ==
          "(5.0 5.0 0.0)");

    // Parallel lines meet nowhere, and that is an answer rather than an error:
    // "do these cross?" is a question a script asks expecting either reply.
    CHECK(f.eval("(inters '(0 0 0) '(10.0 0 0) '(0 5.0 0) '(10.0 5.0 0))") == "nil");
}

TEST_CASE("lisp osnap: finds the nearest snap of the named kinds") {
    Fixture f;
    f.db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    CHECK(f.eval("(osnap '(9.0 1.0 0.0) \"end\")") == "(10.0 0.0 0.0)");
    CHECK(f.eval("(osnap '(1.0 1.0 0.0) \"end\")") == "(0.0 0.0 0.0)");
    CHECK(f.eval("(osnap '(4.0 3.0 0.0) \"mid\")") == "(5.0 0.0 0.0)");

    // Several modes at once, as R12 spells them, and the nearest wins.
    CHECK(f.eval("(osnap '(9.5 0.5 0.0) \"mid,end\")") == "(10.0 0.0 0.0)");

    // A derived snap measures from the reference point.
    CHECK(f.eval("(osnap '(3.0 4.0 0.0) \"nea\")") == "(3.0 0.0 0.0)");
}

TEST_CASE("lisp osnap: nil when nothing matches, and NON means nothing") {
    Fixture f;
    f.db.add(std::make_unique<Line>(Vec3{0, 0, 0}, Vec3{10, 0, 0}));

    // A line has no centre, so asking for one finds nothing rather than
    // falling back to something else.
    CHECK(f.eval("(osnap '(5.0 5.0 0.0) \"cen\")") == "nil");
    CHECK(f.eval("(osnap '(5.0 5.0 0.0) \"none\")") == "nil");

    // An empty drawing has nothing to offer either.
    Fixture empty;
    CHECK(empty.eval("(osnap '(0 0 0) \"end\")") == "nil");
}

TEST_CASE("lisp geometry: the idiom a real script is built from") {
    // Six lines placed by polar around a centre -- which is what most drafting
    // scripts do, and what none of them could do here until now.
    Fixture f;
    f.eval(
        "(setq c '(0.0 0.0 0.0) r 20.0 i 0)"
        "(while (< i 6)"
        "  (command \"LINE\" (polar c (* i (/ pi 3)) r)"
        "                   (polar c (* (1+ i) (/ pi 3)) r) \"\")"
        "  (setq i (1+ i)))");

    CHECK(f.db.order().size() == 6);

    // Every vertex sits on the circle it was placed around.
    for (const Handle h : f.db.order()) {
        const Entity* e = f.db.get(h);
        REQUIRE(e->type() == EntityType::Line);
        const Line& l = static_cast<const Line&>(*e);
        CHECK_NEAR(length(l.start()), 20.0, 1e-9);
        CHECK_NEAR(length(l.end()), 20.0, 1e-9);
    }

    // And the midpoint of the first edge is where osnap says it is.
    CHECK(f.eval("(osnap '(20.0 1.0 0.0) \"mid\")") ==
          "(15.0 8.660254037844386 0.0)");
}

TEST_CASE("lisp geometry: wrong argument types are errors, not guesses") {
    Fixture f;
    CHECK(f.eval("(polar '(0 0 0) \"east\" 5.0)").find("<error") == 0);
    CHECK(f.eval("(distance 3 '(0 0 0))").find("<error") == 0);
    CHECK(f.eval("(angle '(0) '(1.0 1.0 0))").find("<error") == 0);
    CHECK(f.eval("(osnap '(0 0 0) 5)").find("<error") == 0);
}
