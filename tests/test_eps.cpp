// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes
//
// Brief 28 item 3's acceptance line, as a test rather than as a habit.
//
// The rename is worth nothing if the next hurried change writes `1e-9` again.
// A grep in a test is the only thing that makes "these two numbers have names"
// a property of the tree rather than a description of one afternoon.

#include "ncad/eps.hpp"
#include "test.hpp"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ncad;

namespace {

// The literal as CODE, ignoring anything after `//`.
//
// Comments may name the numbers freely -- a comment explaining why a gate sits
// at 1e-9 is the explanation, and forbidding that would be forbidding the
// explanation rather than the literal.
bool code_mentions(const std::string& line, const std::string& lit) {
    const std::size_t note = line.find("//");
    const std::string code = note == std::string::npos ? line : line.substr(0, note);
    std::size_t at = code.find(lit);
    while (at != std::string::npos) {
        // A whole token: `1e-9` but not `1e-90`, and not the tail of `2.1e-9`
        // -- which would be a different number wearing the same characters.
        const bool left = at == 0 || (std::isalnum(static_cast<unsigned char>(code[at - 1])) == 0 &&
                                      code[at - 1] != '.' && code[at - 1] != '_');
        const std::size_t end = at + lit.size();
        const bool right = end >= code.size() ||
                           std::isalnum(static_cast<unsigned char>(code[end])) == 0;
        if (left && right) return true;
        at = code.find(lit, at + 1);
    }
    return false;
}

}  // namespace

TEST_CASE("eps: no bare 1e-9 or 1e-12 survives in the geometry core") {
    const std::filesystem::path dir = std::filesystem::path(NCAD_SOURCE_DIR) / "src" / "core";
    REQUIRE(std::filesystem::is_directory(dir));

    std::vector<std::string> found;
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".cpp") continue;
        ++files;
        std::ifstream in(entry.path());
        std::string line;
        int n = 0;
        while (std::getline(in, line)) {
            ++n;
            for (const char* lit : {"1e-9", "1e-12"}) {
                if (code_mentions(line, lit)) {
                    found.push_back(entry.path().filename().string() + ":" +
                                    std::to_string(n) + "  " + lit);
                }
            }
        }
    }

    // THE DENOMINATOR IS ASSERTED TOO. A directory iterator that found nothing
    // would report a clean sweep, and an empty search is not a passing one --
    // which is the failure this project has made three times in a week.
    REQUIRE(files > 20u);

    // Printed before the assertion, so a failure names every site rather than
    // only saying there was one.
    for (const std::string& f : found) {
        std::printf("      bare epsilon: %s  (use kSameEps or kFloatFloor)\n", f.c_str());
    }
    CHECK(found.empty());
}

TEST_CASE("eps: the two names are three orders apart and neither has moved") {
    // The values are load-bearing: brief 28 named them and deliberately did NOT
    // move them, so that a corpus shifting later cannot be confused with this.
    CHECK(kSameEps == 1e-9);
    CHECK(kFloatFloor == 1e-12);
    // A quantity this program calls zero must be far below one it calls equal.
    CHECK(kFloatFloor * 1000.0 == kSameEps);
}
