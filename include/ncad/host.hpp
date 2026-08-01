// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Which machine this window is running on.
//
// The project now builds on three platforms and is routinely run on several
// machines at once over X11, at which point "which host is this window?" stops
// being answerable by looking at it. That is the whole reason this exists.
//
// Lives in the core, not in the widget, for the same reason about.hpp does:
// `ncad` and `ncad_gui` must not each grow their own answer.
#pragma once

#include <string>

namespace ncad {

// "macOS", "linux/x86_64", "linux/arm64", or "unknown".
//
// Strictly this reports the ARCHITECTURE THIS BINARY WAS BUILT FOR, since it is
// decided by preprocessor macros -- a cross-compiled binary would name its
// target rather than its host. Not worth solving: nothing here is
// cross-compiled, and the alternative (uname(2)) reports the kernel's idea of
// the machine, which under Rosetta or a 32-bit userland is its own kind of lie.
const char* host_platform();

// The unqualified hostname, or "unknown" if the system will not say.
std::string host_name();

}  // namespace ncad
