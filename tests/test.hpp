// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Minimal zero-dependency test harness. Deliberately small: swap for doctest or
// Catch2 later without touching test bodies, since the macro names match.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace nototest {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int n = 0;
    return n;
}

inline const char*& current() {
    static const char* name = "";
    return name;
}

// --- skips, which used to be invisible ----------------------------------------
//
// **A CASE THAT DID NOT RUN MUST NOT READ AS A CASE THAT PASSED.** This is here
// because it did: 37 engine-backed cases printed "skipped" and were then counted
// `ok`, and the summary line without an engine was BYTE-IDENTICAL to the one
// with it. Those 37 are the named acceptance instruments of four briefs, so
// every number written against that line -- including the ones in the bridge --
// could not tell a run that did the work from one that skipped it.
//
// The fix is not politeness. A skip is a FAILURE by default, because the whole
// defect is that a permissive default is indistinguishable from a strict one
// passing. Whoever genuinely cannot run those cases says so out loud with
// `NCAD_TESTS_ALLOW_NO_ENGINE=1`, and then the summary says it for them.
//
// NOTHING IN THIS TREE SKIPS TODAY -- the base has no engine and no case that
// needs one, so `skipped` is always 0 here. The mechanism is taken anyway, and
// with the variable's name unchanged, so that a case backported from the tree
// that does have one arrives working rather than needing this written twice.
struct Skip {
    const char* case_name;
    std::string reason;
};

inline std::vector<Skip>& skips() {
    static std::vector<Skip> all;
    return all;
}

// Recorded against whichever case is running. Not a failure at the point it
// happens -- the case simply did not run -- but counted, named, and turned into
// one at the end unless the run asked for it.
inline void record_skip(const std::string& reason) {
    skips().push_back({current(), reason});
}

// Whether this run said in advance that some of its cases cannot run.
inline bool skips_allowed() {
    const char* v = std::getenv("NCAD_TESTS_ALLOW_NO_ENGINE");
    return v != nullptr && *v != '\0' && std::string(v) != "0";
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void fail(const char* file, int line, const std::string& what) {
    ++failures();
    std::printf("  FAIL %s:%d\n       %s\n", file, line, what.c_str());
}

inline bool approx(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

// THE RULE, as a function, so it can be asserted without a run that skips.
//
// A skipped case is not a passing case. That sentence is the whole of brief 32,
// and putting it here rather than inline in `run_all` is what lets a test state
// it directly -- the alternative is a test that must arrange a real skip, which
// would then make the run it is part of fail.
inline int verdict(int failed_cases, std::size_t skipped, bool allowed) {
    if (failed_cases != 0) return 1;
    return (skipped > 0 && !allowed) ? 1 : 0;
}

inline int run_all() {
    int failed_cases = 0;
    for (const Case& c : registry()) {
        current() = c.name;
        const int before = failures();
        const std::size_t skipped_before = skips().size();
        c.fn();
        const bool skipped = skips().size() != skipped_before;
        const bool ok = (failures() == before);
        // A THIRD MARKER, because two were not enough to tell three states
        // apart. `ok` for a case that ran and passed, `skip` for one that did
        // not run at all, `--` for one that ran and failed.
        std::printf("%s %s\n", skipped ? " skip " : ok ? "  ok  " : "  --  ", c.name);
        if (!ok) ++failed_cases;
    }

    const std::size_t skipped = skips().size();
    const bool allowed = skips_allowed();

    if (skipped > 0) {
        std::printf("\n%zu case(s) did NOT run:\n", skipped);
        for (const Skip& s : skips()) {
            std::printf("  skipped  %s\n           %s\n", s.case_name, s.reason.c_str());
        }
    }

    // THE LINE PEOPLE ACTUALLY READ, and it now carries the number that was
    // missing from it. `rebuild.sh` and every acceptance line in the bridge
    // quote this; it has to be able to say "some of this did not happen".
    std::printf("\n%zu cases, %d failed, %zu skipped, %d assertion(s) failed%s\n",
                registry().size(), failed_cases, skipped, failures(),
                skipped > 0 && allowed ? "  (skips allowed: NCAD_TESTS_ALLOW_NO_ENGINE)" : "");

    if (skipped > 0 && !allowed) {
        std::printf(
            "\nFAILED: %zu case(s) could not run, so a green run without them means\n"
            "nothing. Give them what they need, or set NCAD_TESTS_ALLOW_NO_ENGINE=1\n"
            "to say out loud that this run cannot.\n",
            skipped);
    }

    // THE POLICY, from the one place that states it -- so the rule a test
    // asserts and the rule this run obeys cannot become two rules.
    return verdict(failed_cases, skipped, allowed);
}

}  // namespace nototest

// Two levels so __LINE__ expands to its value before being pasted.
#define NCAD_CAT_(a, b) a##b
#define NCAD_CAT(a, b) NCAD_CAT_(a, b)

#define TEST_CASE(name)                                                     \
    static void NCAD_CAT(ncad_test_fn_, __LINE__)();                        \
    static ::nototest::Registrar NCAD_CAT(ncad_test_reg_, __LINE__)(        \
        name, &NCAD_CAT(ncad_test_fn_, __LINE__));                          \
    static void NCAD_CAT(ncad_test_fn_, __LINE__)()

// This case cannot run, and says so where it can be counted.
//
// Replaces a `printf` and a bare `return`, which is the idiom that made 37 cases
// invisible. It returns, so it is only valid at test-case scope -- the same rule
// REQUIRE follows and for the same reason.
#define SKIP(reason)                                                        \
    do {                                                                    \
        ::nototest::record_skip(reason);                                    \
        return;                                                             \
    } while (0)

#define CHECK(expr)                                                         \
    do {                                                                    \
        if (!(expr)) ::nototest::fail(__FILE__, __LINE__, "CHECK(" #expr ")"); \
    } while (0)

// Like CHECK, but abandons the rest of the case rather than carrying on into
// code that the failed condition has just made unsafe -- a null pointer about
// to be dereferenced, or a container about to be indexed past its end. doctest
// and Catch2 both spell it this way, which is the point: test bodies stay
// portable to either.
//
// It returns from the test function, so it is only valid at test-case scope.
#define REQUIRE(expr)                                                       \
    do {                                                                    \
        if (!(expr)) {                                                      \
            ::nototest::fail(__FILE__, __LINE__, "REQUIRE(" #expr ")");     \
            return;                                                         \
        }                                                                   \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                               \
    do {                                                                    \
        const double a_ = (a), b_ = (b);                                    \
        if (!::nototest::approx(a_, b_, (eps)))                             \
            ::nototest::fail(__FILE__, __LINE__,                            \
                             "CHECK_NEAR(" #a ", " #b ")  got " +           \
                                 std::to_string(a_) + " vs " +              \
                                 std::to_string(b_));                       \
    } while (0)

#define CHECK_VEC(v, ex, ey, ez, eps)                                       \
    do {                                                                    \
        const ::ncad::Vec3 v_ = (v);                                        \
        CHECK_NEAR(v_.x, (ex), (eps));                                      \
        CHECK_NEAR(v_.y, (ey), (eps));                                      \
        CHECK_NEAR(v_.z, (ez), (eps));                                      \
    } while (0)
