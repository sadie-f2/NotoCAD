// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "io_subrs.hpp"

#include "ncad/paths.hpp"

#include <cstdio>
// <ostream> for the char and const char* operator<< overloads. libstdc++ drags
// them in through <string>; libc++ does not, so omitting it builds on Linux and
// fails on macOS.
#include <ostream>
#include <string>

namespace ncad::lisp {
namespace {

bool arg_string(Interp& in, const char* who, const Value& v, std::string& out) {
    if (v.type != Type::Str) {
        return in.fail(EvalStatus::BadArgumentType,
                       std::string(who) + ": not a string: " + prin1(v));
    }
    out = std::string(v.str->view());
    return true;
}

// A descriptor argument, resolved to the open file it names. Nil out-parameter
// with a true return means "was a FILE but is closed", which read-line reports
// as end of input rather than as an error -- reading a closed file is a script
// bug, but a noisy one is worse than a nil.
bool arg_file(Interp& in, const char* who, const Value& v, Interp::OpenFile*& out) {
    if (v.type != Type::File) {
        return in.fail(EvalStatus::BadArgumentType,
                       std::string(who) + ": not a file descriptor: " + prin1(v));
    }
    out = in.open_file(v.file);
    return true;
}

}  // namespace

bool subr_open(Interp& in, const Value* args, std::size_t, Value& out) {
    std::string path;
    std::string mode;
    if (!arg_string(in, "open", args[0], path)) return false;
    if (!arg_string(in, "open", args[1], mode)) return false;

    // R12 takes one letter and ignores case. Anything else is a bug in the
    // script rather than a condition to report as nil.
    char m = mode.empty() ? '\0' : mode[0];
    if (m >= 'A' && m <= 'Z') m = static_cast<char>(m - 'A' + 'a');
    if (m != 'r' && m != 'w' && m != 'a') {
        return in.fail(EvalStatus::BadArgumentType,
                       "open: mode must be \"r\", \"w\" or \"a\", not " + prin1(args[1]));
    }

    // The same ~ expansion the file commands give, so a script and the SAVE
    // prompt agree about what "~/drawings/x.dxf" means.
    const std::string full = expand_user_path(path);

    const char* stdio_mode = (m == 'r') ? "rb" : (m == 'w' ? "wb" : "ab");
    std::FILE* fp = std::fopen(full.c_str(), stdio_mode);
    if (fp == nullptr) {
        // A file that is not there, or a directory that cannot be written, is
        // exactly what a script tests for. nil, not an error.
        out = make_nil();
        return true;
    }

    Value v;
    v.type = Type::File;
    v.file = in.new_file(fp, m != 'r');
    out = v;
    return true;
}

bool subr_close(Interp& in, const Value* args, std::size_t, Value& out) {
    if (args[0].type != Type::File) {
        return in.fail(EvalStatus::BadArgumentType,
                       "close: not a file descriptor: " + prin1(args[0]));
    }
    // Closing an already-closed descriptor is a no-op rather than an error: a
    // script with a (close f) in both the success path and its cleanup is
    // written correctly, not incorrectly.
    in.close_file(args[0].file);
    out = make_nil();
    return true;
}

bool subr_read_line(Interp& in, const Value* args, std::size_t, Value& out) {
    Interp::OpenFile* f = nullptr;
    if (!arg_file(in, "read-line", args[0], f)) return false;
    if (f == nullptr || f->writable) {
        out = make_nil();
        return true;
    }

    std::string line;
    int c = 0;
    bool any = false;
    while ((c = std::fgetc(f->fp)) != EOF) {
        any = true;
        if (c == '\n') break;
        line.push_back(static_cast<char>(c));
    }
    if (!any) {
        out = make_nil();  // end of file
        return true;
    }

    // A file written on Windows ends its lines with CRLF. Dropping the CR here
    // means a script does not have to know which machine wrote its input, and
    // the alternative -- a trailing invisible character on every value -- breaks
    // string comparisons in a way that takes an hour to see.
    if (!line.empty() && line.back() == '\r') line.pop_back();

    out = make_str(in.ctx().new_string(line));
    return true;
}

bool subr_write_line(Interp& in, const Value* args, std::size_t n, Value& out) {
    std::string text;
    if (!arg_string(in, "write-line", args[0], text)) return false;

    if (n < 2) {
        // No descriptor: R12 writes to the screen.
        in.output() << text << "\n";
        out = args[0];
        return true;
    }

    Interp::OpenFile* f = nullptr;
    if (!arg_file(in, "write-line", args[1], f)) return false;
    if (f == nullptr || !f->writable) {
        return in.fail(EvalStatus::BadArgumentType, "write-line: file is not open for writing");
    }

    std::fwrite(text.data(), 1, text.size(), f->fp);
    std::fputc('\n', f->fp);

    // R12 returns what it was given, WITHOUT the newline it added.
    out = args[0];
    return true;
}

bool subr_read_char(Interp& in, const Value* args, std::size_t, Value& out) {
    Interp::OpenFile* f = nullptr;
    if (!arg_file(in, "read-char", args[0], f)) return false;
    if (f == nullptr || f->writable) {
        out = make_nil();
        return true;
    }

    const int c = std::fgetc(f->fp);
    out = (c == EOF) ? make_nil() : make_int(static_cast<std::int32_t>(c));
    return true;
}

bool subr_write_char(Interp& in, const Value* args, std::size_t n, Value& out) {
    if (!is_number(args[0])) {
        return in.fail(EvalStatus::BadArgumentType,
                       "write-char: not a character code: " + prin1(args[0]));
    }
    const int c = static_cast<int>(as_double(args[0]));

    if (n < 2) {
        in.output() << static_cast<char>(c);
        out = args[0];
        return true;
    }

    Interp::OpenFile* f = nullptr;
    if (!arg_file(in, "write-char", args[1], f)) return false;
    if (f == nullptr || !f->writable) {
        return in.fail(EvalStatus::BadArgumentType, "write-char: file is not open for writing");
    }

    std::fputc(c, f->fp);
    out = args[0];
    return true;
}

bool subr_findfile(Interp& in, const Value* args, std::size_t, Value& out) {
    std::string path;
    if (!arg_string(in, "findfile", args[0], path)) return false;

    const std::string full = expand_user_path(path);
    if (!path_exists(full)) {
        out = make_nil();
        return true;
    }
    // The FULL path, which is the point: a script asks this to find out where a
    // file actually is, not merely whether it is there.
    out = make_str(in.ctx().new_string(normalised_path(full)));
    return true;
}

}  // namespace ncad::lisp
