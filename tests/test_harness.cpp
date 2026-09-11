// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The instrument that says whether the other instruments ran.
//
// Brief 32 exists because 37 engine-backed cases printed "skipped" and were then
// counted `ok`, and the summary line without an engine was byte-identical to the
// one with it. Those 37 are the named acceptance instruments of four briefs, so
// every number written against that line could not tell a run that did the work
// from one that skipped it.
//
// A harness that reports skips is itself something that can rot, and it would
// rot the same silent way. So the rule is a function and the function is
// asserted, rather than the rule living inline where only a real skip could
// exercise it -- and a test that had to arrange a real skip would make the run
// it belongs to fail, which is why that shape was not available.

#include "test.hpp"

#include <cstdlib>

// --- the policy ---------------------------------------------------------------

TEST_CASE("harness: a skipped case is not a passing case") {
    // The line that would have caught the defect this brief was cut for: a run
    // with skips and nothing said about them is a FAILING run.
    CHECK(::nototest::verdict(0, 1, false) == 1);
    CHECK(::nototest::verdict(0, 37, false) == 1);

    // Nothing skipped and nothing failed is the only quiet pass.
    CHECK(::nototest::verdict(0, 0, false) == 0);

    // ASKED FOR IS DIFFERENT FROM UNNOTICED. Someone who cannot run a case
    // says so, and their run passes -- and the summary says it for them, which
    // is the half that makes this safe to offer at all.
    CHECK(::nototest::verdict(0, 1, true) == 0);
    CHECK(::nototest::verdict(0, 37, true) == 0);

    // A real failure is a failure however the skips were treated. The opt-out
    // excuses cases that did not run; it does not excuse cases that ran and
    // were wrong.
    CHECK(::nototest::verdict(1, 0, true) == 1);
    CHECK(::nototest::verdict(1, 5, true) == 1);
    CHECK(::nototest::verdict(2, 0, false) == 1);
}

TEST_CASE("harness: the opt-out is read from the environment, and only when set") {
    // `0` is not an opt-out. Someone writing NCAD_TESTS_ALLOW_NO_ENGINE=0 means
    // the opposite of the variable's name, and reading any non-empty value as
    // yes would take a strict run and quietly make it permissive.
    const char* was = std::getenv("NCAD_TESTS_ALLOW_NO_ENGINE");
    const std::string saved = was != nullptr ? was : "";

    ::unsetenv("NCAD_TESTS_ALLOW_NO_ENGINE");
    CHECK(!::nototest::skips_allowed());
    ::setenv("NCAD_TESTS_ALLOW_NO_ENGINE", "", 1);
    CHECK(!::nototest::skips_allowed());
    ::setenv("NCAD_TESTS_ALLOW_NO_ENGINE", "0", 1);
    CHECK(!::nototest::skips_allowed());
    ::setenv("NCAD_TESTS_ALLOW_NO_ENGINE", "1", 1);
    CHECK(::nototest::skips_allowed());

    if (was != nullptr) {
        ::setenv("NCAD_TESTS_ALLOW_NO_ENGINE", saved.c_str(), 1);
    } else {
        ::unsetenv("NCAD_TESTS_ALLOW_NO_ENGINE");
    }
}
