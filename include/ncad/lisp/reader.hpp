// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// AutoLISP reader: source text to Values.
//
// Errors are returned as status codes, never thrown. Per the project's dialect
// rules, exceptions are not control flow in the interpreter.
#pragma once

#include "ncad/lisp/value.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ncad::lisp {

enum class ReadStatus {
    Ok,
    EndOfInput,       // nothing left to read; not an error
    UnexpectedEof,    // input ended inside a list or string
    UnexpectedRParen,
    BadDottedPair,
    BadNumber,
    BadString,
};

const char* read_status_message(ReadStatus s);

struct ReadError {
    ReadStatus status{ReadStatus::Ok};
    std::size_t line{0};
    std::size_t column{0};
    std::string detail;

    bool ok() const { return status == ReadStatus::Ok; }
};

class Reader {
public:
    Reader(Context& ctx, std::string_view source) : ctx_(ctx), src_(source) {}

    // Reads one form. Returns false at end of input or on error; check
    // error().status to tell those apart.
    bool read(Value& out);

    // Reads every form in the source. Returns false on error.
    bool read_all(std::vector<Value>& out);

    const ReadError& error() const { return error_; }

    std::size_t line() const { return line_; }
    std::size_t column() const { return column_; }

private:
    bool at_end() const { return pos_ >= src_.size(); }
    char peek() const { return src_[pos_]; }
    char advance();
    void skip_whitespace_and_comments();

    bool read_form(Value& out);
    bool read_list(Value& out);
    bool read_string(Value& out);
    Value read_atom();

    void fail(ReadStatus status, std::string detail = {});

    Context& ctx_;
    std::string_view src_;
    std::size_t pos_{0};
    std::size_t line_{1};
    std::size_t column_{1};
    ReadError error_{};
};

// Convenience: read a single form from text. Returns nil on failure.
Value read_one(Context& ctx, std::string_view source, ReadError* err = nullptr);

}  // namespace ncad::lisp
