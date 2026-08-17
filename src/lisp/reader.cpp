// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/lisp/reader.hpp"

#include <cstdlib>
#include <cerrno>
#include <climits>

namespace ncad::lisp {
namespace {

// How deep a nested form may be before the reader gives up.
//
// Generous, because legitimate machine-generated LISP does nest -- but bounded,
// because read_form and read_list recurse into each other once per level and
// 50k parens segfaulted the release build. Matched to the evaluator's default
// max_depth_ of 2000 so that the two limits tell the same story: a form this
// program will not evaluate is one it need not read either.
constexpr std::size_t kMaxReadDepth = 2000;

}  // namespace

namespace {

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// Characters that end an atom.
bool is_terminator(char c) {
    return is_space(c) || c == '(' || c == ')' || c == '"' || c == '\'' || c == ';';
}

// Full-consumption parses, so that "1+" and "-" stay symbols rather than being
// half-read as numbers.
bool parse_int(std::string_view text, std::int32_t& out) {
    if (text.empty()) return false;
    std::string buf(text);
    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(buf.c_str(), &end, 10);
    if (errno == ERANGE || end != buf.c_str() + buf.size()) return false;
    if (v < INT32_MIN || v > INT32_MAX) return false;
    out = static_cast<std::int32_t>(v);
    return true;
}

bool parse_real(std::string_view text, double& out) {
    if (text.empty()) return false;
    std::string buf(text);
    char* end = nullptr;
    const double v = std::strtod(buf.c_str(), &end);
    if (end != buf.c_str() + buf.size()) return false;
    out = v;
    return true;
}

}  // namespace

const char* read_status_message(ReadStatus s) {
    switch (s) {
        case ReadStatus::Ok: return "ok";
        case ReadStatus::EndOfInput: return "end of input";
        case ReadStatus::UnexpectedEof: return "unexpected end of input";
        case ReadStatus::UnexpectedRParen: return "unexpected )";
        case ReadStatus::BadDottedPair: return "malformed dotted pair";
        case ReadStatus::BadNumber: return "malformed number";
        case ReadStatus::BadString: return "malformed string";
        case ReadStatus::TooDeep: return "form nested too deeply";
    }
    return "unknown error";
}

char Reader::advance() {
    const char c = src_[pos_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return c;
}

void Reader::skip_whitespace_and_comments() {
    while (!at_end()) {
        const char c = peek();
        if (is_space(c)) {
            advance();
        } else if (c == ';') {
            while (!at_end() && peek() != '\n') advance();
        } else {
            return;
        }
    }
}

void Reader::fail(ReadStatus status, std::string detail) {
    error_.status = status;
    error_.line = line_;
    error_.column = column_;
    error_.detail = std::move(detail);
}

bool Reader::read(Value& out) {
    error_ = ReadError{};
    skip_whitespace_and_comments();
    if (at_end()) {
        error_.status = ReadStatus::EndOfInput;
        return false;
    }
    return read_form(out);
}

bool Reader::read_all(std::vector<Value>& out) {
    Value v;
    while (read(v)) out.push_back(v);
    return error_.status == ReadStatus::EndOfInput;
}

bool Reader::read_form(Value& out) {
    skip_whitespace_and_comments();
    if (at_end()) {
        fail(ReadStatus::UnexpectedEof);
        return false;
    }

    // Guarded here rather than in read_list so that '(((... -- quote, which
    // also recurses -- is bounded by the same counter.
    if (depth_ >= kMaxReadDepth) {
        fail(ReadStatus::TooDeep);
        return false;
    }

    const char c = peek();
    if (c == '(') {
        advance();
        ++depth_;
        const bool ok = read_list(out);
        --depth_;
        return ok;
    }
    if (c == ')') {
        advance();
        fail(ReadStatus::UnexpectedRParen);
        return false;
    }
    if (c == '"') {
        advance();
        return read_string(out);
    }
    if (c == '\'') {
        advance();
        Value quoted;
        ++depth_;
        const bool ok = read_form(quoted);
        --depth_;
        if (!ok) return false;
        // 'x reads as (QUOTE x).
        out = ctx_.cons(make_sym(ctx_.sym_quote()), ctx_.cons(quoted, make_nil()));
        return true;
    }

    out = read_atom();
    return true;
}

bool Reader::read_list(Value& out) {
    // Built front to back, so no reversal pass is needed.
    Value head = make_nil();
    Cons* tail = nullptr;

    for (;;) {
        skip_whitespace_and_comments();
        if (at_end()) {
            fail(ReadStatus::UnexpectedEof, "list not closed");
            return false;
        }

        if (peek() == ')') {
            advance();
            out = head;
            return true;
        }

        // A dot here means a dotted pair -- but only when it stands alone;
        // ".5" is a number and ".foo" is a symbol.
        if (peek() == '.' && pos_ + 1 < src_.size() && is_terminator(src_[pos_ + 1])) {
            advance();
            if (tail == nullptr) {
                fail(ReadStatus::BadDottedPair, "dot with no preceding element");
                return false;
            }
            Value tail_value;
            if (!read_form(tail_value)) return false;
            tail->cdr = tail_value;

            skip_whitespace_and_comments();
            if (at_end() || peek() != ')') {
                fail(ReadStatus::BadDottedPair, "expected ) after dotted tail");
                return false;
            }
            advance();
            out = head;
            return true;
        }

        Value item;
        if (!read_form(item)) return false;

        Cons* cell = ctx_.arena().make<Cons>();
        cell->car = item;
        cell->cdr = make_nil();
        if (tail == nullptr) {
            head = make_cons(cell);
        } else {
            tail->cdr = make_cons(cell);
        }
        tail = cell;
    }
}

bool Reader::read_string(Value& out) {
    std::string text;
    for (;;) {
        if (at_end()) {
            fail(ReadStatus::BadString, "string not closed");
            return false;
        }
        const char c = advance();
        if (c == '"') break;

        if (c != '\\') {
            text += c;
            continue;
        }
        if (at_end()) {
            fail(ReadStatus::BadString, "trailing backslash");
            return false;
        }
        const char esc = advance();
        switch (esc) {
            case 'n': text += '\n'; break;
            case 't': text += '\t'; break;
            case 'r': text += '\r'; break;
            case 'e': text += '\x1b'; break;  // R12 spelling for escape
            case '\\': text += '\\'; break;
            case '"': text += '"'; break;
            default: text += esc; break;
        }
    }
    out = make_str(ctx_.new_string(text));
    return true;
}

Value Reader::read_atom() {
    const std::size_t start = pos_;
    while (!at_end() && !is_terminator(peek())) advance();
    const std::string_view text = src_.substr(start, pos_ - start);

    std::int32_t as_int = 0;
    if (parse_int(text, as_int)) return make_int(as_int);

    double as_real = 0.0;
    if (parse_real(text, as_real)) return make_real(as_real);

    Symbol* sym = ctx_.intern(text);
    // nil and T read as the values themselves, not as symbols to look up.
    if (sym == ctx_.sym_nil()) return make_nil();
    if (sym == ctx_.sym_t()) return make_true();
    return make_sym(sym);
}

Value read_one(Context& ctx, std::string_view source, ReadError* err) {
    Reader r(ctx, source);
    Value v = make_nil();
    const bool ok = r.read(v);
    if (err) *err = r.error();
    return ok ? v : make_nil();
}

}  // namespace ncad::lisp
