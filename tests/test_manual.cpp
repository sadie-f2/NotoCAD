// Coverage for docs/manual/, asserted rather than hoped.
//
// ROADMAP.md 7 asks for exactly one instrument behind the manual: a test that
// reads the manual and requires every registered command name to appear in it.
// It is the same shape as the version-is-the-command-count convention in
// test_registry.cpp -- a rule that lives only in a comment is one nobody
// remembers to honour.
//
// It is a RATCHET, not a wish. The manual is written one chapter per brief, so
// the gap starts large and closes over many sessions; pinning the gap at a hard
// number means it can only go DOWN. A chapter that documents ten commands
// lowers it by ten and the number is edited on purpose, exactly as
// kCommandCount is. A refactor that drops a chapter, or a command quietly
// dropped from the prose it was documented in, raises it and fails here.

#include "test.hpp"

#include "ncad/commands.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace ncad;

namespace {

std::filesystem::path manual_dir() {
    return std::filesystem::path(NCAD_SOURCE_DIR) / "docs" / "manual";
}

std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// A stub chapter carries this marker and is NOT counted. Without it the scope
// notes in a stub -- which list the commands the chapter will cover, because
// that is what makes the skeleton useful to plan against -- would read as
// coverage, and the instrument would report a manual that is finished on the
// day it is empty.
const char* kStubMarker = "<!-- stub:";

// Whole-word, so COPY is not credited to COPYCLIP, DIM not to DIMLINEAR, and
// SOLID not to SOLIDS. Command names are upper-case ASCII and may contain
// digits (3DFACE, DIM1), so the boundary is "not a letter, digit or
// underscore" on each side.
bool names_command(const std::string& text, const std::string& name) {
    const auto is_word = [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_';
    };
    std::size_t at = 0;
    while ((at = text.find(name, at)) != std::string::npos) {
        const bool left = at == 0 || !is_word(static_cast<unsigned char>(text[at - 1]));
        const std::size_t end = at + name.size();
        const bool right = end >= text.size() || !is_word(static_cast<unsigned char>(text[end]));
        if (left && right) return true;
        at += 1;
    }
    return false;
}

// Fenced code blocks removed. A command name inside a transcript does NOT
// count as documenting it, and this is the instrument's most important rule --
// it was found the hard way. Chapter 1 quotes the `?` listing, which prints
// every registered command name; with fences counted, the manual measured 86
// of 86 documented on the day two chapters existed. A transcript SHOWS a
// command working; the prose around it is what documents it. So coverage is
// measured on prose only, and a chapter earns a command by talking about it.
//
// Inline code (`EXTRUDE`) stays: that IS prose, and it is how the chapters
// refer to commands.
std::string without_code_blocks(const std::string& md) {
    std::string out;
    out.reserve(md.size());
    std::size_t at = 0;
    bool inside = false;
    while (at < md.size()) {
        const std::size_t nl = md.find('\n', at);
        const std::size_t stop = nl == std::string::npos ? md.size() : nl;
        std::string line = md.substr(at, stop - at);
        std::size_t first = line.find_first_not_of(" \t");
        const bool fence =
            first != std::string::npos && line.compare(first, 3, "```") == 0;
        if (fence) {
            inside = !inside;
        } else if (!inside) {
            out += line;
            out += '\n';
        }
        at = stop + 1;
    }
    return out;
}

// Every chapter that is actually written, concatenated. Read from the SOURCE
// tree, as test_step_read.cpp reads its corpus: the manual is a source file,
// and a copy staged into the build directory could go stale without anyone
// noticing.
struct Manual {
    std::string prose;         // written chapters only
    std::vector<std::string> chapters;  // file names, written
    std::vector<std::string> stubs;     // file names, stubbed
};

Manual read_manual() {
    Manual m;
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(manual_dir())) {
        if (e.is_regular_file() && e.path().extension() == ".md") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& p : files) {
        if (p.filename() == "README.md") continue;
        const std::string text = read_file(p);
        if (text.find(kStubMarker) != std::string::npos) {
            m.stubs.push_back(p.filename().string());
        } else {
            m.chapters.push_back(p.filename().string());
            m.prose += without_code_blocks(text);
            m.prose += "\n";
        }
    }
    return m;
}

std::vector<std::string> undocumented(const Manual& m) {
    std::vector<std::string> missing;
    for (const std::string& name : command_names()) {
        if (!names_command(m.prose, name)) missing.push_back(name);
    }
    return missing;
}

}  // namespace

TEST_CASE("manual: the skeleton has a file for every chapter") {
    const Manual m = read_manual();
    // EIGHT chapters here, not the nine of the tree this came from. The
    // working set is ROADMAP 7's: starting, drafting, editing, viewing,
    // layers/blocks, dimensions/text, files, scripting -- and then solids,
    // which is chapter 9 and belongs to the tree that has them. This program
    // has no solid modelling, so it has no chapter 9, and a file for one would
    // be a chapter about commands that are not in the registry.
    CHECK(m.chapters.size() + m.stubs.size() == 8);
    for (const char* want : {"01-starting.md", "02-drafting.md", "03-editing.md",
                             "04-viewing.md", "05-organising.md", "06-annotation.md",
                             "07-files.md", "08-scripting.md"}) {
        const bool present =
            std::find(m.chapters.begin(), m.chapters.end(), want) != m.chapters.end() ||
            std::find(m.stubs.begin(), m.stubs.end(), want) != m.stubs.end();
        CHECK(present);
        if (!present) std::printf("       missing chapter file: %s\n", want);
    }
}

TEST_CASE("manual: every registered command is documented, and the gap only shrinks") {
    const Manual m = read_manual();
    const std::vector<std::string> missing = undocumented(m);

    // THE RATCHET. Raise nothing here; LOWER it when a chapter lands, exactly
    // as the command count in test_registry.cpp is raised when a command lands.
    // The manual is complete when this is 0 and the CHECK below can become an
    // equality against zero.
    //
    // 54, NOT THE 51 OF THE TREE THIS INSTRUMENT CAME FROM, and the difference
    // is not a regression. That number was measured against a registry of 86
    // with a ninth chapter covering the solids commands; this registry holds
    // 74 and has no chapter 9, so the denominator and the covered set are both
    // different and only a number measured HERE means anything. 20 of 74
    // documented on the day the manual arrived.
    constexpr std::size_t kUndocumented = 54;

    CHECK(missing.size() <= kUndocumented);

    // Always reported, passing or failing. The gap is the point of this test --
    // a number nobody can see is a number nobody lowers.
    std::printf("       manual: %zu of %zu commands documented, %zu owed (pinned at %zu)\n",
                command_names().size() - missing.size(), command_names().size(),
                missing.size(), kUndocumented);
    if (missing.size() < kUndocumented) {
        std::printf("       the gap CLOSED -- lower kUndocumented in test_manual.cpp to %zu\n",
                    missing.size());
    }
    if (missing.size() > kUndocumented) {
        for (const std::string& n : missing) std::printf("         undocumented: %s\n", n.c_str());
    }
}

TEST_CASE("manual: a written chapter is read for coverage and a stub is not") {
    const Manual m = read_manual();
    // The distinction the ratchet rests on. If a stub's scope note counted,
    // the manual would measure as finished while empty.
    CHECK(!m.chapters.empty());
    CHECK(!m.stubs.empty());
    for (const std::string& f : m.stubs) {
        const std::string text = read_file(manual_dir() / f);
        CHECK(text.find(kStubMarker) != std::string::npos);
    }
    for (const std::string& f : m.chapters) {
        const std::string text = read_file(manual_dir() / f);
        CHECK(text.find(kStubMarker) == std::string::npos);
    }
}

TEST_CASE("manual: a transcript does not document a command, the prose does") {
    // The flaw this instrument shipped with for about ten minutes. Chapter 1
    // quotes `?`, whose output names every command in the registry; counting
    // fenced blocks made the manual measure as complete while empty.
    const std::string md =
        "# Chapter\n\nThis chapter is about ZOOM.\n\n```\nCommand: ?\n"
        "Commands: ZOOM PAN LAYER WBLOCK\n```\n";
    const std::string prose = without_code_blocks(md);
    CHECK(names_command(prose, "ZOOM"));    // named in the prose
    CHECK(!names_command(prose, "PAN"));    // only ever inside the transcript
    CHECK(!names_command(prose, "WBLOCK"));
}

TEST_CASE("manual: coverage is whole-word, so COPY is not credited to COPYCLIP") {
    // The bug this instrument would otherwise have shipped with: a substring
    // search credits COPY to COPYCLIP, DIM to DIMLINEAR, SOLID to SOLIDS and
    // ARC to SOLARRAY, and the manual measures far more complete than it is.
    CHECK(!names_command("PASTECLIP and COPYCLIP move geometry", "COPY"));
    CHECK(names_command("COPYCLIP, CUTCLIP; and COPY duplicates", "COPY"));
    CHECK(!names_command("DIMLINEAR measures", "DIM"));
    CHECK(!names_command("SOLIDS reports the engine", "SOLID"));
    CHECK(names_command("a SOLID is four-sided", "SOLID"));
    // Digits are word characters: DIM1 must not be credited to DIM.
    CHECK(!names_command("DIM1 repeats", "DIM"));
    CHECK(names_command("3DFACE draws", "3DFACE"));
}
