// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/host.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#define NCAD_HAVE_GETHOSTNAME 1
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

}  // namespace ncad
