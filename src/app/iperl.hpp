// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// iperl as a calculator at the prompt: `=2*$pi*5` at a radius, `=p("3 4 +")`
// anywhere a number is wanted.
//
// A SUBPROCESS, not a library, and that is the whole licensing story. iperl is
// Artistic 2.0 and Perl itself is dual Artistic/GPL, so linking libperl would
// be a decision to make and a dependency to carry -- the same shape as the DWG
// question. Running a program is not linking, so this costs nothing: no
// compile-time module, no flag in the build graph, and the default binary stays
// BSD-3 with no Perl anywhere near it.
//
// It is also entirely OPTIONAL AT RUNTIME. No perl, no script, or a script that
// will not start, and the prefix reports politely and nothing else changes.
// Nothing in the build depends on any of it.
//
// One process, kept. Starting perl per expression would work and would cost
// twenty milliseconds, but iperl's RPN stack is persistent -- and shared with
// the terminal sessions through ~/.iperl_rpn_stack -- so a fresh process each
// time would throw away the state that makes an RPN calculator worth having.
//
// R12 precedent: AutoCAD's CAL, a transparent geometry calculator you invoked
// mid-command. This is that, with a better language behind it.
//
// KNOWN LIMIT: as an ANSWER to a prompt, the expression may not contain spaces.
// A line of answers is tokenised on whitespace -- `CIRCLE 0,0 5` is three
// answers -- so `=p("3 4 +")` is torn into pieces before it is ever seen. Write
// it without spaces (`=p("3+4")`, `=sqrt(2)*10`, `=join(",",3*4,5*2)`) or put
// it at the COMMAND prompt, which takes the whole line. Teaching the tokeniser
// to hold an `=` run together needs a rule for where it ends that does not
// break multi-answer lines, and that is a design question rather than an
// oversight.
#pragma once

#include <string>

namespace noto::app {

class IperlSession {
public:
    IperlSession() = default;
    ~IperlSession();

    IperlSession(const IperlSession&) = delete;
    IperlSession& operator=(const IperlSession&) = delete;

    // Evaluates one expression, starting iperl if it is not running yet.
    //
    // False when iperl is unavailable or the expression died; `out` carries the
    // reason either way, so a caller can print it without asking why. True with
    // `out` holding the result, formatted to round-trip as a double.
    bool evaluate(const std::string& expr, std::string& out);

    // Whether the co-process could be started at all. Starts it as a side
    // effect, because there is no cheaper way to find out.
    bool available();

    // Where iperl was found, or empty. NOTO_IPERL overrides; otherwise the
    // couple of places it usually lives.
    static std::string script_path();

private:
    bool start();
    void stop();

    int to_child_{-1};
    int from_child_{-1};
    int pid_{-1};

    // Set once a start has been attempted, so a missing perl is reported once
    // and not retried on every keystroke.
    bool tried_{false};
    std::string why_unavailable_;
};

}  // namespace noto::app
