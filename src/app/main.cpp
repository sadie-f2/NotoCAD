// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// ncad -- the command-line application.
//
// A command prompt over the AutoLISP interpreter, holding one drawing. This is
// the whole user interface for now; the Qt6 shell comes later and stays thin
// behind the same core.
//
// Two modes. The default is R12's: a command prompt that evaluates LISP when a
// line starts with "(". --lisp gives a plain LISP read-eval-print loop instead,
// which is the better shape for piping a generated script in.
//
// Input completeness is decided by the reader, not by counting parentheses. A
// naive counter gets ")" inside a string or a comment wrong, and the reader
// already knows the difference -- an unterminated form comes back as
// UnexpectedEof, which is exactly the signal to ask for another line.
//
// Line editing is deliberately absent. GNU readline is GPLv3, and linking it
// would put the default build under the same obligation the DWG module is kept
// optional to avoid. libedit (BSD) can go behind a build option later without
// changing anything here.
#include "prompt.hpp"

#include "ncad/command.hpp"
#include "ncad/database.hpp"
#include "ncad/lisp/eval.hpp"
#include "ncad/lisp/reader.hpp"
#include "ncad/lisp/value.hpp"
#include "ncad/version.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#define NCAD_HAVE_ISATTY 1
#endif

namespace {

// The generated header, not a literal: ABOUT already printed the real version
// while the banner and --help still said 0.0.1, because a second copy of a
// number is a copy that goes stale.
constexpr const char* kVersion = ncad::kNcadVersion;

// AutoCAD's AutoLISP prompt, and the continuation used while a form is still
// open.
constexpr const char* kPrompt = "_$ ";
constexpr const char* kContinuation = "   ";

bool stdin_is_tty() {
#ifdef NCAD_HAVE_ISATTY
    return isatty(fileno(stdin)) != 0;
#else
    return false;
#endif
}

void print_usage() {
    std::cout << "ncad " << kVersion << " -- command-line CAD, AutoCAD R12 dialect\n\n"
              << "Usage:\n"
              << "  ncad [options] [file.lsp ...]\n\n"
              << "Options:\n"
              << "  -e EXPR    evaluate EXPR\n"
              << "  -i         stay interactive after evaluating files\n"
              << "  --lisp     plain AutoLISP REPL instead of the command prompt\n"
              << "  -q         suppress the banner\n"
              << "  -h         show this help\n\n"
              << "With no files and no -e, reads from standard input.\n\n"
              << "Example:\n"
              << "  ncad -e '(entmake (list (cons 0 \"CIRCLE\") (list 10 0.0 0.0 0.0)"
              << " (cons 40 5.0)))' \\\n"
              << "       -e '(dxfout \"out.dxf\")'\n";
}

// Evaluates every form in `source`. Returns false if evaluation failed, having
// reported why on stderr.
bool eval_source(ncad::lisp::Interp& in, const std::string& source, const std::string& origin) {
    in.clear_error();
    ncad::lisp::Value result;
    if (in.eval_string(source, result)) return true;

    std::cerr << origin << ": " << in.error().message() << "\n";
    return false;
}

bool load_file(ncad::lisp::Interp& in, const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "ncad: cannot open " << path << "\n";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return eval_source(in, buffer.str(), path);
}

// The loop. Accumulates lines until the reader agrees it has a complete form,
// then evaluates and prints. An error aborts the current input and returns to
// the prompt with the interpreter intact.
int repl(ncad::lisp::Context& ctx, ncad::lisp::Interp& in, bool interactive) {
    std::string pending;
    std::string line;

    while (true) {
        if (interactive) {
            std::cout << (pending.empty() ? kPrompt : kContinuation) << std::flush;
        }
        if (!std::getline(std::cin, line)) break;

        pending += line;
        pending += '\n';

        // Parse what we have. Incomplete input is not an error; it is a request
        // for the next line.
        ncad::lisp::Reader reader(ctx, pending);
        std::vector<ncad::lisp::Value> forms;
        if (!reader.read_all(forms)) {
            const ncad::lisp::ReadError& err = reader.error();
            if (err.status == ncad::lisp::ReadStatus::UnexpectedEof) continue;

            std::cerr << "; read error, line " << err.line << ", column " << err.column << ": "
                      << ncad::lisp::read_status_message(err.status) << "\n";
            pending.clear();
            continue;
        }
        pending.clear();

        for (const ncad::lisp::Value& form : forms) {
            in.clear_error();
            ncad::lisp::Value result;
            if (!in.eval(form, result)) {
                // Each form typed at the REPL is a top-level action of its own,
                // so the script's *error* handler gets its chance here just as
                // it does inside eval_string. Without this call a handler
                // defined at the REPL would simply never run.
                in.run_error_handler();
                std::cerr << "; error: " << in.error().message() << "\n";
                break;
            }
            // Printed even when not interactive, so `ncad < script.lsp` behaves
            // like a transcript rather than swallowing everything.
            std::cout << ncad::lisp::prin1(result) << "\n";
            if (in.quit_requested()) return 0;
        }
    }

    // Input ended mid-form. Silently discarding it would make a truncated
    // script look like it succeeded.
    if (pending.find_first_not_of(" \t\r\n") != std::string::npos) {
        std::cerr << "; error: unexpected end of input inside an unterminated form\n";
        return 1;
    }

    if (interactive) std::cout << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> files;
    std::vector<std::string> expressions;
    bool force_interactive = false;
    bool quiet = false;
    bool lisp_mode = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        }
        if (arg == "-i") {
            force_interactive = true;
        } else if (arg == "--lisp") {
            lisp_mode = true;
        } else if (arg == "-q") {
            quiet = true;
        } else if (arg == "-e") {
            if (i + 1 >= argc) {
                std::cerr << "ncad: -e needs an expression\n";
                return 2;
            }
            expressions.push_back(argv[++i]);
        } else if (!arg.empty() && arg[0] == '-' && arg != "-") {
            std::cerr << "ncad: unknown option " << arg << "\n";
            return 2;
        } else {
            files.push_back(arg);
        }
    }

    ncad::Database db;
    ncad::CommandEngine engine(db);
    ncad::lisp::Context ctx;
    ncad::lisp::Interp in(ctx);
    in.set_database(&db);
    in.set_command_engine(&engine);

    for (const std::string& path : files) {
        if (!load_file(in, path)) return 1;
        if (in.quit_requested()) return 0;
    }
    for (const std::string& expr : expressions) {
        if (!eval_source(in, expr, "-e")) return 1;
        if (in.quit_requested()) return 0;
    }

    // Files or expressions given: do the work and stop, unless asked to stay.
    const bool had_work = !files.empty() || !expressions.empty();
    if (had_work && !force_interactive) return 0;

    const bool interactive = stdin_is_tty();
    if (interactive && !quiet) {
        std::cout << "ncad " << kVersion
                  << (lisp_mode ? " -- AutoLISP. (quit) or Ctrl-D to exit.\n"
                                : " -- type ? for commands, ( for AutoLISP, QUIT to exit.\n");
    }
    if (lisp_mode) return repl(ctx, in, interactive);
    return ncad::app::run_command_prompt(ctx, in, engine, interactive);
}
