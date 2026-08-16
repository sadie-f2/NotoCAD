// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/host.hpp"

#include <cstdlib>

#if defined(__unix__) || defined(__APPLE__)
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#define NCAD_HAVE_GETHOSTNAME 1
#define NCAD_HAVE_GETPWUID 1
#endif

namespace ncad {

const char* host_platform() {
#if defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
#if defined(__aarch64__)
    return "linux/arm64";
#elif defined(__arm__)
    return "linux/arm";
#elif defined(__x86_64__)
    return "linux/x86_64";
#elif defined(__i386__)
    return "linux/x86";
#else
    return "linux";
#endif
#else
    return "unknown";
#endif
}

std::string host_name() {
#ifdef NCAD_HAVE_GETHOSTNAME
    // POSIX does not promise termination when the name does not fit, so the
    // buffer is one larger than the length asked for and that byte is zeroed.
    char buf[256];
    buf[sizeof(buf) - 1] = '\0';
    if (gethostname(buf, sizeof(buf) - 1) != 0) return "unknown";
    if (buf[0] == '\0') return "unknown";
    // Taken as returned. These machines report short names already, and cutting
    // at the first dot would be guessing at a domain that may be part of how the
    // host is actually known.
    return std::string(buf);
#else
    return "unknown";
#endif
}

std::string user_name() {
    // The environment first, because that is the name the user recognises and
    // the one a login shell set. It is also what AutoCAD's lock carries.
    for (const char* var : {"USER", "LOGNAME"}) {
        const char* v = std::getenv(var);
        if (v != nullptr && *v != '\0') return std::string(v);
    }

#ifdef NCAD_HAVE_GETPWUID
    // No environment -- a daemon, a cron job, a stripped `env`. getpwuid_r
    // rather than getpwuid: the non-reentrant one returns a pointer into shared
    // storage that the next caller overwrites.
    struct passwd pw {};
    struct passwd* result = nullptr;
    char buf[1024];
    if (getpwuid_r(getuid(), &pw, buf, sizeof(buf), &result) == 0 && result != nullptr &&
        pw.pw_name != nullptr && pw.pw_name[0] != '\0') {
        return std::string(pw.pw_name);
    }
#endif

    return "unknown";
}

}  // namespace ncad
