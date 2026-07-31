// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// iperl as a co-process.
//
// The property that matters most is the one tested when iperl is NOT there:
// this is optional at runtime, so a machine without perl must get a clear
// message and carry on, not a crash and not a hang. That case is the default on
// a build machine, so it is the one these run most often.

#include "test.hpp"

#include "iperl.hpp"

#include <cstdlib>
#include <string>

using namespace ncad;

TEST_CASE("iperl: missing is reported, not fatal") {
    // A path that cannot exist, so this exercises the unavailable path on a
    // machine that does have perl and iperl installed.
    ::setenv("NCAD_IPERL", "/nonexistent/definitely/not/iperl.pl", 1);
    CHECK(app::IperlSession::script_path().empty());

    app::IperlSession session;
    CHECK(!session.available());

    std::string out;
    CHECK(!session.evaluate("2+2", out));
    // The message has to say what to do about it, since the answer is almost
    // always "it is not installed" rather than "it is broken".
    CHECK(!out.empty());
    CHECK(out.find("NCAD_IPERL") != std::string::npos);

    ::unsetenv("NCAD_IPERL");
}

TEST_CASE("iperl: a multi-line expression is refused rather than desynchronising") {
    // One expression per line is the protocol, so an embedded newline would be
    // read as two requests and every reply after it would answer the wrong
    // question for the rest of the session. Worth refusing loudly.
    ::setenv("NCAD_IPERL", "/nonexistent/not/here.pl", 1);
    app::IperlSession session;
    std::string out;
    CHECK(!session.evaluate("2+2\n3+3", out));
    CHECK(!out.empty());
    ::unsetenv("NCAD_IPERL");
}

TEST_CASE("iperl: arithmetic, when it is actually installed") {
    ::unsetenv("NCAD_IPERL");
    app::IperlSession session;
    if (!session.available()) {
        std::printf("       (iperl not installed here; skipping the live checks)\n");
        return;
    }

    std::string out;
    REQUIRE(session.evaluate("2*3+1", out));
    CHECK(out == "7");

    // Full precision, which is the whole reason --pipe formats numbers itself:
    // Perl's default stringification is %.15g and would drop the last two
    // digits, which for a coordinate is a silent loss the kernel would inherit.
    REQUIRE(session.evaluate("sqrt(2)", out));
    CHECK(out == "1.4142135623730951");

    // The stack is persistent and shared with the terminal sessions, so the
    // process is kept rather than restarted per expression.
    REQUIRE(session.evaluate("rpn(11,22)", out));
    CHECK(out == "22");

    // A dying expression reports and the session survives it.
    CHECK(!session.evaluate("1/0", out));
    CHECK(out.find("division by zero") != std::string::npos);
    REQUIRE(session.evaluate("6*7", out));
    CHECK(out == "42");

    // Shelling out is refused from the pipe.
    CHECK(!session.evaluate("!echo hello", out));
}
