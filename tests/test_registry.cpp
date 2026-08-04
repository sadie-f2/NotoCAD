// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The command registry, checked against itself.
//
// This exists because of a specific near-miss: a refactor deleted two command
// implementations outright, and the only thing that caught it was a link error
// about a missing vtable. Had those commands also left `make_command`, it would
// have compiled, linked and passed the whole suite -- because nothing else names
// them, and a command nobody names is a command nobody tests.
//
// So the invariants here are deliberately about the registry AS A WHOLE rather
// than about any command in it: every advertised name must construct, every
// constructed command must answer to the name it was asked for, and the total
// must be what we think it is. The count is the part that catches a deletion.

#include "test.hpp"

#include "ncad/about.hpp"
#include "ncad/commands.hpp"
#include "ncad/version.hpp"
#include "ncad/database.hpp"

#include <set>
#include <string>

using namespace ncad;

TEST_CASE("registry: every advertised name constructs") {
    for (const std::string& name : command_names()) {
        CommandPtr c = make_command(name);
        CHECK(c != nullptr);
        if (!c) std::printf("       no command for advertised name %s\n", name.c_str());
    }
}

TEST_CASE("registry: a constructed command answers to the name it was asked for") {
    for (const std::string& name : command_names()) {
        CommandPtr c = make_command(name);
        if (!c) continue;

        // Not always identical: SAVEAS and QSAVE are modes of SaveCommand. What
        // matters is that a command reports SOME advertised name rather than
        // something unreachable. OPEN and DXFIN used to be the case here too,
        // until DXFIN stopped clearing the drawing and they became two modes
        // that each report their own name.
        const std::string reported = c->name();
        bool advertised = false;
        for (const std::string& n : command_names()) {
            if (n == reported) advertised = true;
        }
        CHECK(advertised);
        if (!advertised) {
            std::printf("       %s reports unadvertised name %s\n", name.c_str(),
                        reported.c_str());
        }
    }
}

TEST_CASE("registry: the advertised list has no duplicates") {
    std::set<std::string> seen;
    for (const std::string& name : command_names()) {
        CHECK(seen.insert(name).second);
    }
    CHECK(seen.size() == command_names().size());
}

TEST_CASE("registry: every alias resolves to an advertised name") {
    for (const CommandAlias& a : command_aliases()) {
        const CommandMatch m = resolve_command_name(a.alias);
        CHECK(m.ok());
        CHECK(m.name == a.name);
        CHECK(make_command(m.name) != nullptr);
    }
}

TEST_CASE("registry: the command count is what we think it is") {
    // Deliberately a hard number. A refactor that loses a command silently is
    // exactly what this file exists to catch, and a count that adjusts itself
    // would catch nothing -- so raise it ON PURPOSE when adding a command, and
    // treat it dropping as a bug until proven otherwise.
    // QUIT is deliberately NOT here. It ends the session rather than acting on
    // the drawing, so prompt.cpp handles it beside EXIT and it owns no Command.
    // That is why `?` lists 60 names while the registry holds 59, which is
    // confusing enough to be worth saying once.
    CHECK(command_names().size() == 59);
    if (command_names().size() != 59) {
        std::printf("       registry holds %zu commands\n", command_names().size());
    }
}

TEST_CASE("registry: an unknown name resolves to nothing rather than to something") {
    CHECK(!resolve_command_name("NOSUCHCOMMAND").ok());
    CHECK(make_command("NOSUCHCOMMAND") == nullptr);
}

TEST_CASE("registry: the version's patch number IS the command count") {
    // Sadie's convention: 0.0.53 means the registry holds 53 commands, so the
    // version says something a version usually does not -- how much of R12's
    // surface is actually there.
    //
    // Asserted rather than documented, because a convention that lives only in
    // a comment is one nobody remembers to honour. Raise BOTH numbers when a
    // command is added: the literal above, and project(VERSION) in the root
    // CMakeLists.
    const std::string v = kNcadVersion;
    const std::size_t last_dot = v.rfind('.');
    REQUIRE(last_dot != std::string::npos);

    const std::string patch = v.substr(last_dot + 1);
    CHECK(patch == std::to_string(command_names().size()));
}

TEST_CASE("about: names every licence this build actually carries") {
    const std::string s = about_text();

    // The version and the build stamp, so a bug report can say which binary.
    CHECK(s.find(kNcadVersion) != std::string::npos);
    CHECK(s.find(kNcadGitHash) != std::string::npos);

    // The Hershey acknowledgements are a LICENCE CONDITION, not a courtesy:
    // they must travel with the font data, and that data is compiled in here.
    // If this assertion ever fails, the binary is out of compliance.
    CHECK(s.find("A. V. Hershey") != std::string::npos);
    CHECK(s.find("James Hurt") != std::string::npos);

    CHECK(s.find("BSD-3-Clause") != std::string::npos);
    CHECK(s.find("LGPLv3") != std::string::npos);

    // And it must be honest about what is NOT linked. A default build carries
    // no GPL code, and saying otherwise either way would be the failure.
#ifdef NCAD_WITH_DWG
    CHECK(s.find("CONVEYED UNDER GPLv3") != std::string::npos);
#else
    CHECK(s.find("links no GPL code") != std::string::npos);
#endif
}

TEST_CASE("about: disclaims affiliation, which is what makes naming the marks fair") {
    const std::string s = about_text();

    // Naming AutoLISP is unavoidable -- there is no other way to say which
    // dialect the interpreter implements -- and nominative fair use turns on
    // nothing implying sponsorship. The disclaimer is that condition, so it is
    // asserted rather than left to survive a future edit of the text.
    CHECK(s.find("trademarks of Autodesk") != std::string::npos);
    CHECK(s.find("not affiliated with") != std::string::npos);
}
