// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "iperl.hpp"

#include <csignal>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace ncad::app {
namespace {

// End of transmission. iperl writes it after every response, followed by one
// status digit. Chosen because it cannot occur in a number, a Perl string
// result or an error message, so scanning for it needs no escaping on either
// side.
constexpr char kEot = '\x04';

bool readable_file(const std::string& path) {
    return !path.empty() && ::access(path.c_str(), R_OK) == 0;
}

std::string home_relative(const char* tail) {
    const char* home = std::getenv("HOME");
    if (home == nullptr) return {};
    return std::string(home) + tail;
}

// PATH, for an iperl that was installed rather than cloned. Both spellings are
// tried in each directory: `iperl.pl` is what the source tree calls it, `iperl`
// is what an install tends to shorten it to.
//
// Readability rather than executability is the test, because the child runs
// `perl <script>` rather than the script itself -- so an installed copy without
// its executable bit still works, and requiring +x would reject it for no
// reason.
std::string on_path() {
    const char* path = std::getenv("PATH");
    if (path == nullptr) return {};

    const std::string spec(path);
    std::size_t begin = 0;
    while (begin <= spec.size()) {
        const std::size_t end = spec.find(':', begin);
        const std::size_t stop = (end == std::string::npos) ? spec.size() : end;
        const std::string dir = spec.substr(begin, stop - begin);

        // An empty entry means the working directory by POSIX convention.
        // Skipped deliberately: picking a script up from wherever ncad happens
        // to have been started is a surprise, and it is one an attacker can
        // arrange.
        if (!dir.empty()) {
            for (const char* name : {"/iperl.pl", "/iperl"}) {
                const std::string candidate = dir + name;
                if (readable_file(candidate)) return candidate;
            }
        }

        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}

}  // namespace

std::string IperlSession::script_path() {
    // The override comes first so a different iperl -- or none -- can be chosen
    // without touching the search below.
    if (const char* env = std::getenv("NCAD_IPERL"); env != nullptr && *env != '\0') {
        return readable_file(env) ? std::string(env) : std::string{};
    }

    // A checkout in the usual place beats an installed copy, so that working on
    // iperl means testing the version being worked on rather than whatever was
    // installed last.
    for (const char* tail : {"/src/iperl/iperl.pl", "/iperl.pl"}) {
        const std::string path = home_relative(tail);
        if (readable_file(path)) return path;
    }

    // Then PATH, which is the only one of the three that finds an iperl the
    // user installed rather than cloned -- and therefore the one that makes
    // this work on a machine that is not the author's.
    return on_path();
}

IperlSession::~IperlSession() { stop(); }

bool IperlSession::start() {
    if (pid_ > 0) return true;
    if (tried_) return false;
    tried_ = true;

    const std::string script = script_path();
    if (script.empty()) {
        why_unavailable_ = "iperl not found (set NCAD_IPERL to its path)";
        return false;
    }

    int down[2] = {-1, -1};  // parent -> child
    int up[2] = {-1, -1};    // child -> parent
    if (::pipe(down) != 0) {
        why_unavailable_ = "could not create a pipe to iperl";
        return false;
    }
    if (::pipe(up) != 0) {
        ::close(down[0]);
        ::close(down[1]);
        why_unavailable_ = "could not create a pipe from iperl";
        return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(down[0]);
        ::close(down[1]);
        ::close(up[0]);
        ::close(up[1]);
        why_unavailable_ = "could not start iperl";
        return false;
    }

    if (pid == 0) {
        // Child. Only the two ends it needs survive; leaking the others would
        // mean the parent's read never sees EOF when iperl exits.
        ::dup2(down[0], STDIN_FILENO);
        ::dup2(up[1], STDOUT_FILENO);
        ::close(down[0]);
        ::close(down[1]);
        ::close(up[0]);
        ::close(up[1]);
        ::execlp("perl", "perl", script.c_str(), "--pipe", static_cast<char*>(nullptr));
        ::_exit(127);  // exec failed; the parent will see EOF on its first read
    }

    ::close(down[0]);
    ::close(up[1]);
    to_child_ = down[1];
    from_child_ = up[0];
    pid_ = static_cast<int>(pid);

    // Writing to a pipe whose reader has gone raises SIGPIPE, which by default
    // kills the whole program -- so a missing perl would take ncad down with
    // it. Ignored here rather than handled at the write, because there is
    // nothing useful to do about it beyond noticing the error return.
    std::signal(SIGPIPE, SIG_IGN);
    return true;
}

void IperlSession::stop() {
    if (to_child_ >= 0) ::close(to_child_);
    if (from_child_ >= 0) ::close(from_child_);
    to_child_ = -1;
    from_child_ = -1;

    if (pid_ > 0) {
        // Closing its stdin is the polite request; iperl's read loop ends and
        // it exits. Waiting collects it so a long session does not accumulate
        // zombies.
        int status = 0;
        ::waitpid(static_cast<pid_t>(pid_), &status, 0);
        pid_ = -1;
    }
}

bool IperlSession::available() {
    if (pid_ > 0) return true;
    return start();
}

bool IperlSession::evaluate(const std::string& expr, std::string& out) {
    if (!available()) {
        out = why_unavailable_;
        return false;
    }

    // One expression per line, and a newline is what tells iperl the request is
    // complete -- so an expression containing one would be read as two and the
    // protocol would desynchronise for the rest of the session. Refused rather
    // than silently joined.
    if (expr.find('\n') != std::string::npos) {
        out = "an iperl expression must be a single line";
        return false;
    }

    const std::string request = expr + "\n";
    std::size_t written = 0;
    while (written < request.size()) {
        const ssize_t n = ::write(to_child_, request.data() + written, request.size() - written);
        if (n <= 0) {
            stop();
            out = "iperl stopped responding";
            return false;
        }
        written += static_cast<std::size_t>(n);
    }

    // Read until the terminator, then the status digit after it. Byte at a time
    // because the reply is short and the alternative is a buffer that has to be
    // carried between calls -- a source of bugs out of all proportion to the
    // syscalls it saves.
    std::string body;
    char c = 0;
    while (true) {
        const ssize_t n = ::read(from_child_, &c, 1);
        if (n <= 0) {
            stop();
            // The most likely cause by far: perl is not installed, so execlp
            // failed and the child exited before reading anything.
            out = body.empty() ? "iperl did not start (is perl installed?)"
                               : "iperl stopped responding";
            return false;
        }
        if (c == kEot) break;
        body += c;
    }

    char status = '1';
    if (::read(from_child_, &status, 1) <= 0) {
        stop();
        out = "iperl stopped responding";
        return false;
    }

    // Trailing newline after the status, discarded. Its absence is not worth
    // failing over.
    char eol = 0;
    (void)::read(from_child_, &eol, 1);

    while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) body.pop_back();
    out = body;
    return status == '0';
}

}  // namespace ncad::app
