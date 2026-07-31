// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "test.hpp"

#include "ncad/database.hpp"
#include "ncad/lisp/eval.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace ncad;
using namespace ncad::lisp;

namespace {

// Writes into the build directory rather than a fixed path, so a parallel run
// cannot have two cases fighting over one file.
std::string temp_path(const char* name) {
    return std::string("ncad_test_") + name + ".dxf";
}

struct Fixture {
    Database db;
    Context ctx;
    Interp in{ctx};
    std::ostringstream out;
    std::string path;

    explicit Fixture(const char* name) : path(temp_path(name)) {
        in.set_output(&out);
        in.set_database(&db);
    }

    ~Fixture() { std::remove(path.c_str()); }

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

    std::string contents() const {
        std::ifstream f(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
};

std::size_t count_of(const std::string& haystack, const std::string& needle) {
    std::size_t n = 0;
    for (std::size_t p = haystack.find(needle); p != std::string::npos;
         p = haystack.find(needle, p + needle.size())) {
        ++n;
    }
    return n;
}

}  // namespace

TEST_CASE("dxfout: writes a file and reports success") {
    Fixture f("basic");
    f.eval(R"((entmake '((0 . "LINE") (10 0.0 0.0 0.0) (11 10.0 0.0 0.0))))");
    CHECK(f.eval("(dxfout \"" + f.path + "\")") == "T");

    const std::string text = f.contents();
    CHECK(!text.empty());
    CHECK(text.find("SECTION") != std::string::npos);
    CHECK(text.find("ENTITIES") != std::string::npos);
    CHECK(text.find("LINE") != std::string::npos);
    // R12 DXF is CRLF, and staying that way matters for the other readers.
    CHECK(text.find("\r\n") != std::string::npos);
    // Ends with the EOF marker.
    CHECK(text.size() > 5);
    CHECK(text.substr(text.size() - 5) == "EOF\r\n");
}

TEST_CASE("dxfout: an unwritable path is nil, not an error") {
    Fixture f("unwritable");
    f.eval(R"((entmake '((0 . "LINE") (10 0.0 0.0 0.0) (11 1.0 0.0 0.0))))");
    // A directory that cannot exist. This is a condition a script can test for.
    CHECK(f.eval(R"((dxfout "/nonexistent-dir-xyzzy/out.dxf"))") == "nil");
    CHECK(f.in.error().ok());
}

TEST_CASE("dxfout: a non-string path is an error") {
    Fixture f("badarg");
    CHECK(f.status("(dxfout 42)") == EvalStatus::BadArgumentType);
}

TEST_CASE("dxfout: reports a missing drawing rather than crashing") {
    Context ctx;
    Interp in(ctx);
    Value v;
    CHECK(in.eval_string(R"((dxfout "unused.dxf"))", v) == false);
    CHECK(in.error().status == EvalStatus::BadArgumentType);
}

TEST_CASE("dxfout: an empty drawing still produces a valid file") {
    Fixture f("empty");
    CHECK(f.eval("(dxfout \"" + f.path + "\")") == "T");
    const std::string text = f.contents();
    CHECK(text.find("ENTITIES") != std::string::npos);
    CHECK(text.substr(text.size() - 5) == "EOF\r\n");
}

TEST_CASE("dxfout: the whole path runs from LISP source to a file on disk") {
    // The milestone this represents: a drawing described entirely in LISP,
    // generated procedurally, and written out without any C++ in between.
    Fixture f("program");
    const std::string program = R"(
        (setq pi 3.14159265358979)
        (defun spoke (i n / a)
          (setq a (/ (* 2.0 pi i) n))
          (entmake (list '(0 . "LINE") '(8 . "SPOKES")
                         '(10 0.0 0.0 0.0)
                         (list 11 (* 10.0 (cos a)) (* 10.0 (sin a)) 0.0))))
        (defun wheel (n / i)
          (setq i 0)
          (while (< i n) (spoke i n) (setq i (1+ i)))
          (entmake (list '(0 . "CIRCLE") '(8 . "RIM")
                         '(10 0.0 0.0 0.0) (cons 40 10.0))))
        (wheel 12)
    )";
    f.eval(program);
    CHECK(f.db.size() == 13);  // 12 spokes plus the rim
    CHECK(f.eval("(dxfout \"" + f.path + "\")") == "T");

    const std::string text = f.contents();
    CHECK(count_of(text, "\r\nLINE\r\n") == 12);
    CHECK(count_of(text, "\r\nCIRCLE\r\n") == 1);
    // Both layers reached the table, having been created on demand by entmake.
    CHECK(text.find("SPOKES") != std::string::npos);
    CHECK(text.find("RIM") != std::string::npos);
}

TEST_CASE("dxfout: a tilted entity carries its extrusion vector to disk") {
    // entmake takes group 10 in the entity coordinate system and the writer
    // must put it back the same way. Reading the file confirms the two halves
    // agree rather than each being self-consistent.
    Fixture f("tilted");
    f.eval(R"((entmake '((0 . "CIRCLE") (10 5.0 0.0 15.0) (40 . 3.0) (210 1.0 0.0 0.0))))");
    CHECK(f.eval("(dxfout \"" + f.path + "\")") == "T");

    const std::string text = f.contents();
    // Group 210 with a unit X extrusion, written back as it came in.
    CHECK(text.find("\r\n210\r\n1.0\r\n") != std::string::npos);
    // And group 10 is the ECS centre, not the world centre (15, 5, 0).
    CHECK(text.find("\r\n10\r\n5.0\r\n") != std::string::npos);
}
