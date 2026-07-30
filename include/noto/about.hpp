// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// What this build is, and what it owes.
//
// Lives in the core so that `ncad` and `ncad_gui` say exactly the same thing.
// Two ABOUT texts would drift, and the one that drifted would be the one nobody
// was reading when it mattered.
//
// THE LIST IS OF WHAT IS ACTUALLY HERE, not of what is planned. OCCT is decided
// in SF_strategy.md and is not in the tree, so it is not named. LibreDWG is a
// compile-time option that currently wires nothing, so its line is guarded by
// the macro that will define it rather than printed on faith.
//
// The Hershey attribution is not a courtesy. Its licence requires the
// acknowledgements to travel with the font data, and that data is compiled into
// this binary -- so this text is part of how the condition is met, alongside
// the verbatim notice in third_party/hershey/.
#pragma once

#include <string>

namespace noto {

// Version, build stamp and the licence of every component this binary carries.
std::string about_text();

}  // namespace noto
