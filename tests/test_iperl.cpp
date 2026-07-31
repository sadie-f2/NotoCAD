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

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace ncad;

namespace {

// Saves HOME and PATH and puts them back, because script_path() reads both and
// the rest of the suite -- and any iperl check after this one -- would inherit
// whatever was left behind.
class ScopedEnv {
public:
    ScopedEnv() : home_(get("HOME")), path_(get("PATH")) {}
    ~ScopedEnv() {
        put("HOME", home_);
        put("PATH", path_);
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    static std::string get(const char* name) {
        const char* v = std::getenv(name);
        return v == nullptr ? std::string{} : std::string(v);
    }
    static void put(const char* name, const std::string& value) {
        if (value.empty()) {
            ::unsetenv(name);
        } else {
            ::setenv(name, value.c_str(), 1);
        }
    }

    std::string home_;
    std::string path_;
};

// A directory under /tmp holding one file of the given name, so a PATH entry
// can be pointed at something real. Returns the directory.
std::string dir_holding(const std::string& parent, const char* name) {
    const std::string path = parent + "/" + name;
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (f != nullptr) {
        std::fputs("# a stand-in; script_path only has to find it\n", f);
        std::fclose(f);
    }
    return parent;
}

}  // namespace

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

TEST_CASE("iperl: an installed copy is found on PATH") {
    // The reason this exists: the search was home-relative only, so an iperl
    // installed the ordinary way was invisible and every machine that was not
    // the author's had to set NCAD_IPERL by hand.
    ScopedEnv restore;
    ::unsetenv("NCAD_IPERL");

    char tmpl[] = "/tmp/ncad_iperl_testXXXXXX";
    const char* made = ::mkdtemp(tmpl);
    REQUIRE(made != nullptr);
    const std::string tmp(made);

    // HOME somewhere with no iperl in it, so the home-relative paths -- which
    // win on purpose -- cannot answer and PATH is what is being measured.
    const std::string empty_home = tmp + "/home";
    REQUIRE(::mkdir(empty_home.c_str(), 0700) == 0);
    ::setenv("HOME", empty_home.c_str(), 1);

    const std::string bin = tmp + "/bin";
    REQUIRE(::mkdir(bin.c_str(), 0700) == 0);

    // Nothing on PATH yet, so the search has to come up empty rather than
    // finding something incidental.
    ::setenv("PATH", bin.c_str(), 1);
    CHECK(app::IperlSession::script_path().empty());

    // The source tree's spelling.
    dir_holding(bin, "iperl.pl");
    CHECK(app::IperlSession::script_path() == bin + "/iperl.pl");

    // And the spelling an install tends to use, in a directory of its own so
    // the first one is not what answers.
    const std::string sbin = tmp + "/sbin";
    REQUIRE(::mkdir(sbin.c_str(), 0700) == 0);
    dir_holding(sbin, "iperl");
    ::setenv("PATH", sbin.c_str(), 1);
    CHECK(app::IperlSession::script_path() == sbin + "/iperl");

    // A checkout still beats PATH, so working on iperl means testing the copy
    // being worked on.
    const std::string home_src = empty_home + "/src";
    const std::string home_iperl = home_src + "/iperl";
    REQUIRE(::mkdir(home_src.c_str(), 0700) == 0);
    REQUIRE(::mkdir(home_iperl.c_str(), 0700) == 0);
    dir_holding(home_iperl, "iperl.pl");
    CHECK(app::IperlSession::script_path() == home_iperl + "/iperl.pl");

    // And the override still beats both.
    ::setenv("NCAD_IPERL", (sbin + "/iperl").c_str(), 1);
    CHECK(app::IperlSession::script_path() == sbin + "/iperl");
    ::unsetenv("NCAD_IPERL");

    // An empty PATH entry means the working directory by POSIX convention, and
    // is skipped rather than honoured -- a script picked up from wherever ncad
    // was started is a surprise, and one an attacker can arrange.
    ::setenv("HOME", (tmp + "/nothing").c_str(), 1);
    ::setenv("PATH", ":", 1);
    const std::string cwd_iperl = "./iperl.pl";
    CHECK(app::IperlSession::script_path() != cwd_iperl);
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
