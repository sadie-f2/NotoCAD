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

#include "noto/commands.hpp"
#include "noto/database.hpp"

#include <set>
#include <string>

using namespace noto;

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

        // Not always identical: OPEN is an alias of DXFIN and reports DXFIN,
        // which is correct -- what matters is that it reports SOME advertised
        // name rather than something unreachable.
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
    CHECK(command_names().size() == 51);
    if (command_names().size() != 51) {
        std::printf("       registry holds %zu commands\n", command_names().size());
    }
}

TEST_CASE("registry: an unknown name resolves to nothing rather than to something") {
    CHECK(!resolve_command_name("NOSUCHCOMMAND").ok());
    CHECK(make_command("NOSUCHCOMMAND") == nullptr);
}
