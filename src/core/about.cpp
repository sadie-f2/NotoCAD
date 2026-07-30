// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/about.hpp"

#include "noto/commands.hpp"
#include "noto/version.hpp"

namespace noto {

std::string about_text() {
    std::string s;

    s += "NotoCAD ";
    s += kNotoVersion;
    s += "  (";
    s += kNotoGitHash;
    s += ")\n";
    s += "A command-line-first CAD tool modelled on AutoCAD R12.\n";
    s += "Copyright (c) 2026, Sadie Forbes.\n";
    s += "\n";

    // The patch number is the command count -- see version.hpp.in. Printing the
    // registry size rather than re-stating the number keeps the claim true even
    // if the two ever did drift, and tests/test_registry.cpp asserts they do not.
    s += "Commands: " + std::to_string(command_names().size()) + "\n";
    s += "\n";

    s += "--- Licence ---------------------------------------------------\n";
    s += "NotoCAD itself is BSD-3-Clause. The full text is in LICENSE, and\n";
    s += "it covers this program's documentation as well as its source.\n";
    s += "\n";

    s += "--- Bundled: Hershey fonts ------------------------------------\n";
    s += "TEXT and MTEXT are drawn with the Hershey Roman Simplex stroke\n";
    s += "font, compiled into this binary. Its licence permits any use,\n";
    s += "commercial or otherwise, on the condition that the following\n";
    s += "acknowledgements travel with the font data:\n";
    s += "\n";
    s += "  The Hershey Fonts were originally created by Dr. A. V. Hershey\n";
    s += "  while working at the U. S. National Bureau of Standards.\n";
    s += "\n";
    s += "  The format of the font data in this distribution was\n";
    s += "  originally created by James Hurt, Cognition, Inc.,\n";
    s += "  900 Technology Park Drive, Billerica, MA 01821.\n";
    s += "\n";
    s += "The data may be converted to any format except the one\n";
    s += "distributed by the U.S. NTIS. The verbatim notice is kept at\n";
    s += "third_party/hershey/HERSHEY-NOTICE.txt.\n";
    s += "\n";

    s += "--- Qt 6 ------------------------------------------------------\n";
    s += "Qt 6 is LGPLv3 and is linked DYNAMICALLY, on purpose: static\n";
    s += "linking would impose relinking obligations the BSD-3 core exists\n";
    s += "to avoid. Only ncad_gui links it -- the ncad terminal binary and\n";
    s += "the core libraries see no Qt at all, and src/gui/CMakeLists.txt\n";
    s += "aborts the build if it finds a static Qt.\n";
    s += "\n";

#ifdef NOTO_WITH_DWG
    // Reached only in a build that actually links LibreDWG. The CMake option
    // exists but currently defines nothing, so this is the branch that goes
    // live when DWG import is wired rather than a claim made in advance.
    s += "--- LibreDWG (this build) -------------------------------------\n";
    s += "This binary was built with -DNOTO_WITH_DWG=ON and links\n";
    s += "LibreDWG, which is GPLv3. THIS BINARY MUST THEREFORE BE\n";
    s += "CONVEYED UNDER GPLv3. The project's own source remains\n";
    s += "BSD-3-Clause; it is the distributed binary that is affected.\n";
    s += "\n";
#else
    s += "--- LibreDWG (not in this build) ------------------------------\n";
    s += "DWG import is an optional compile-time module and is OFF. This\n";
    s += "build links no GPL code. A -DNOTO_WITH_DWG=ON binary would link\n";
    s += "LibreDWG, which is GPLv3, and would have to be conveyed under\n";
    s += "GPLv3 -- which is exactly why it is an option and not a\n";
    s += "dependency.\n";
    s += "\n";
#endif

    s += "--- iperl -----------------------------------------------------\n";
    s += "The =expression calculator runs iperl as a SEPARATE PROCESS over\n";
    s += "a pipe. Nothing of it is linked or included here, so it carries\n";
    s += "no licence obligation into this binary. If iperl is absent the\n";
    s += "calculator reports itself unavailable and nothing else changes.\n";

    return s;
}

}  // namespace noto
