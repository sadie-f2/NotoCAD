// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Minimal zero-dependency test harness. Deliberately small: swap for doctest or
// Catch2 later without touching test bodies, since the macro names match.
#pragma once

#include <cmath>
#include <cstdio>
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

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void fail(const char* file, int line, const std::string& what) {
    ++failures();
    std::printf("  FAIL %s:%d\n       %s\n", file, line, what.c_str());
}

inline bool approx(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

inline int run_all() {
    int failed_cases = 0;
    for (const Case& c : registry()) {
        current() = c.name;
        const int before = failures();
        c.fn();
        const bool ok = (failures() == before);
        std::printf("%s %s\n", ok ? "  ok  " : "  --  ", c.name);
        if (!ok) ++failed_cases;
    }
    std::printf("\n%zu cases, %d failed, %d assertion(s) failed\n",
                registry().size(), failed_cases, failures());
    return failed_cases == 0 ? 0 : 1;
}

}  // namespace nototest

// Two levels so __LINE__ expands to its value before being pasted.
#define NOTO_CAT_(a, b) a##b
#define NOTO_CAT(a, b) NOTO_CAT_(a, b)

#define TEST_CASE(name)                                                     \
    static void NOTO_CAT(noto_test_fn_, __LINE__)();                        \
    static ::nototest::Registrar NOTO_CAT(noto_test_reg_, __LINE__)(        \
        name, &NOTO_CAT(noto_test_fn_, __LINE__));                          \
    static void NOTO_CAT(noto_test_fn_, __LINE__)()

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
        const ::noto::Vec3 v_ = (v);                                        \
        CHECK_NEAR(v_.x, (ex), (eps));                                      \
        CHECK_NEAR(v_.y, (ey), (eps));                                      \
        CHECK_NEAR(v_.z, (ez), (eps));                                      \
    } while (0)
