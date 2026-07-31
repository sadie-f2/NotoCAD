// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// AutoLISP file I/O.
//
// Named in CLAUDE.md's own scope statement, and it is the path the project
// exists for: pulling external analysis results in and generating geometry from
// them. The last test here is that round trip end to end, because the individual
// functions working is not the same claim.

#include "test.hpp"

#include "ncad/command.hpp"
#include "ncad/database.hpp"
#include "ncad/entities.hpp"
#include "ncad/lisp/eval.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace ncad;
using namespace ncad::lisp;

namespace {

std::string temp_dir() {
    const std::filesystem::path p = std::filesystem::temp_directory_path() / "ncad_lisp_io";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p.string();
}

std::string temp_path(const char* leaf) { return temp_dir() + "/" + leaf; }

void write_file(const std::string& path, const std::string& body) {
    std::ofstream f(path, std::ios::binary);
    f << body;
}

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream b;
    b << f.rdbuf();
    return b.str();
}

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

    double real_of(const std::string& source) {
        in.clear_error();
        Value v;
        if (!in.eval_string(source, v)) return -1e30;
        return is_number(v) ? as_double(v) : -1e30;
    }

    // Source with a path spliced in, since every test here needs one.
    std::string evalf(const std::string& fmt, const std::string& path) {
        std::string s = fmt;
        const std::string key = "@PATH@";
        for (std::size_t i = s.find(key); i != std::string::npos; i = s.find(key)) {
            s.replace(i, key.size(), path);
        }
        return eval(s);
    }
};

}  // namespace

TEST_CASE("lisp open: gives a FILE, and nil for what is not there") {
    const std::string path = temp_path("read.txt");
    write_file(path, "one\ntwo\n");

    Fixture f;
    CHECK(f.evalf("(type (open \"@PATH@\" \"r\"))", path) == "FILE");

    // A missing file is a condition a script tests for, not an error.
    CHECK(f.eval("(open \"/no/such/directory/file.txt\" \"r\")") == "nil");

    // A bad mode IS an error: that is a bug in the script, not a condition.
    CHECK(f.evalf("(open \"@PATH@\" \"x\")", path).find("<error") == 0);
}

TEST_CASE("lisp read-line: yields each line, then nil, without separators") {
    const std::string path = temp_path("lines.txt");
    // The last line is CRLF-terminated, as a file written on Windows would be.
    write_file(path, "alpha\nbeta\ngamma\r\n");

    Fixture f;
    f.evalf("(setq g (open \"@PATH@\" \"r\"))", path);

    CHECK(f.eval("(read-line g)") == "\"alpha\"");
    CHECK(f.eval("(read-line g)") == "\"beta\"");
    // The CR is dropped: a script should not have to know which machine wrote
    // its input, and a trailing invisible character breaks string comparison in
    // a way that takes an hour to find.
    CHECK(f.eval("(read-line g)") == "\"gamma\"");
    CHECK(f.eval("(read-line g)") == "nil");
    // And it stays nil rather than becoming an error.
    CHECK(f.eval("(read-line g)") == "nil");
    f.eval("(close g)");
}

TEST_CASE("lisp read-line: a file with no final newline still yields its last line") {
    const std::string path = temp_path("noeol.txt");
    write_file(path, "first\nlast-without-newline");

    Fixture f;
    f.evalf("(setq g (open \"@PATH@\" \"r\"))", path);
    CHECK(f.eval("(read-line g)") == "\"first\"");
    CHECK(f.eval("(read-line g)") == "\"last-without-newline\"");
    CHECK(f.eval("(read-line g)") == "nil");
}

TEST_CASE("lisp write-line: writes with a newline and returns without one") {
    const std::string path = temp_path("write.txt");
    std::filesystem::remove(path);

    Fixture f;
    f.evalf("(setq g (open \"@PATH@\" \"w\"))", path);
    // R12 returns what it was given, not what it wrote.
    CHECK(f.eval("(write-line \"hello\" g)") == "\"hello\"");
    f.eval("(write-line \"world\" g)");
    f.eval("(close g)");

    CHECK(read_file(path) == "hello\nworld\n");
}

TEST_CASE("lisp: append adds, write truncates") {
    const std::string path = temp_path("append.txt");
    write_file(path, "existing\n");

    Fixture f;
    f.evalf("(setq g (open \"@PATH@\" \"a\")) (write-line \"added\" g) (close g)", path);
    CHECK(read_file(path) == "existing\nadded\n");

    f.evalf("(setq g (open \"@PATH@\" \"w\")) (write-line \"fresh\" g) (close g)", path);
    CHECK(read_file(path) == "fresh\n");
}

TEST_CASE("lisp read-char and write-char: single bytes both ways") {
    const std::string path = temp_path("chars.txt");
    write_file(path, "AB");

    Fixture f;
    f.evalf("(setq g (open \"@PATH@\" \"r\"))", path);
    CHECK(f.eval("(read-char g)") == "65");
    CHECK(f.eval("(read-char g)") == "66");
    CHECK(f.eval("(read-char g)") == "nil");
    f.eval("(close g)");

    const std::string out = temp_path("chars_out.txt");
    std::filesystem::remove(out);
    f.evalf("(setq g (open \"@PATH@\" \"w\")) (write-char 90 g) (close g)", out);
    CHECK(read_file(out) == "Z");
}

TEST_CASE("lisp close: a stale descriptor reads nothing rather than something else") {
    const std::string path = temp_path("stale.txt");
    write_file(path, "data\n");

    Fixture f;
    f.evalf("(setq g (open \"@PATH@\" \"r\"))", path);
    CHECK(f.eval("(close g)") == "nil");

    // Indices are never reused, so a descriptor held past its close cannot
    // silently address whatever was opened next.
    CHECK(f.eval("(read-line g)") == "nil");
    // Closing twice is a no-op: a script with a close in both the success path
    // and its cleanup is written correctly, not incorrectly.
    CHECK(f.eval("(close g)") == "nil");
}

TEST_CASE("lisp: reading from a write handle, and writing to a read handle") {
    const std::string path = temp_path("modes.txt");
    write_file(path, "x\n");

    Fixture f;
    f.evalf("(setq r (open \"@PATH@\" \"r\"))", path);
    f.evalf("(setq w (open \"@PATH@2\" \"w\"))", path);

    CHECK(f.eval("(read-line w)") == "nil");
    CHECK(f.eval("(write-line \"no\" r)").find("<error") == 0);
    f.eval("(close r) (close w)");
}

TEST_CASE("lisp findfile: the full path, or nil") {
    const std::string path = temp_path("found.txt");
    write_file(path, "here\n");

    Fixture f;
    CHECK(f.evalf("(findfile \"@PATH@\")", path) != "nil");
    CHECK(f.eval("(findfile \"/no/such/file/at/all\")") == "nil");
}

TEST_CASE("lisp read: the first expression only, unevaluated") {
    Fixture f;
    // The FIRST, not all of them. That is R12's behaviour rather than an
    // economy, and it is why the parenthesis idiom below exists.
    CHECK(f.eval("(read \"1 2 3\")") == "1");
    CHECK(f.eval("(read \"(a b c)\")") == "(A B C)");

    // Nothing is evaluated: this is the three-element list, not 3.
    CHECK(f.eval("(read \"(+ 1 2)\")") == "(+ 1 2)");

    // A blank line in a data file is a thing that happens, so an empty string
    // is nil rather than an error -- otherwise every caller has to guard.
    CHECK(f.eval("(read \"\")") == "nil");
    CHECK(f.eval("(read \"   \")") == "nil");

    // Malformed input IS an error: that is a broken file, not a blank one.
    CHECK(f.eval("(read \"(a b\")").find("<error") == 0);
    CHECK(f.eval("(read 5)").find("<error") == 0);
}

TEST_CASE("lisp read: the idiom that splits a record") {
    // Wrapping a line in parentheses makes the reader do the splitting, which
    // is how AutoLISP has always parsed a whitespace-separated record and the
    // reason `read` was the missing piece rather than a split-string function.
    Fixture f;
    CHECK(f.eval("(read (strcat \"(\" \"30.0 40.0\" \")\"))") == "(30.0 40.0)");
    CHECK(f.eval("(car (read (strcat \"(\" \"30.0 40.0\" \")\")))") == "30.0");

    // And a blank record collapses to nil, which a caller can test in one step.
    CHECK(f.eval("(read (strcat \"(\" \"\" \")\"))") == "nil");
}

TEST_CASE("lisp: the round trip the project exists for") {
    // External analysis data in, geometry out, results back to a file. Each
    // function working is not the same claim as this working.
    const std::string in_path = temp_path("points.txt");
    const std::string out_path = temp_path("lengths.txt");
    std::filesystem::remove(out_path);
    // A blank line in the middle, because real data files have them.
    write_file(in_path,
               "0.0 0.0\n"
               "30.0 40.0\n"
               "\n"
               "30.0 0.0\n");

    Fixture f;
    f.evalf(
        "(setq g (open \"@PATH@\" \"r\") prev nil total 0.0)"
        // The whole point: each line becomes a real point through `read`, and
        // consecutive points become geometry.
        "(while (setq ln (read-line g))"
        "  (setq p (read (strcat \"(\" ln \")\")))"
        "  (if p"
        "    (progn"
        "      (if prev (progn (command \"LINE\" prev p \"\")"
        "                      (setq total (+ total (distance prev p)))))"
        "      (setq prev p))))"
        "(close g)",
        in_path);

    // Three points survive; the blank line is skipped rather than breaking it.
    CHECK(f.db.order().size() == 2);
    // 0,0 -> 30,40 is 50; 30,40 -> 30,0 is 40.
    CHECK_NEAR(f.real_of("total"), 90.0, 1e-9);

    // And the answer goes back out to a file.
    f.evalf(
        "(setq o (open \"@PATH@\" \"w\"))"
        "(write-line (strcat \"total=\" (rtos total 2 1)) o)"
        "(close o)",
        out_path);

    CHECK(read_file(out_path).find("total=90.0") != std::string::npos);
}
