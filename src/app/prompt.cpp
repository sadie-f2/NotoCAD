// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "prompt.hpp"

#include "iperl.hpp"

#include "ncad/commands.hpp"
#include "ncad/input_text.hpp"
#include "ncad/lisp/command_input.hpp"
#include "ncad/lisp/reader.hpp"
#include "ncad/lisp/value.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace ncad::app {
namespace {

constexpr const char* kCommandPrompt = "Command: ";

std::string upcase(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

std::string trim(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// Feeds one typed line to the running command. The third InputSource
// implementation, and the only one that can reach back into the interpreter:
// `!name` substitutes a variable and `(expr)` evaluates in place.
class PromptLineSource final : public InputSource {
public:
    PromptLineSource(std::vector<std::string> tokens, lisp::Interp& in, const Database* db)
        : tokens_(std::move(tokens)), in_(in), db_(db) {}

    bool next_value(const Prompt& prompt, InputValue& out) override;

    bool failed() const { return !error_.empty(); }
    const std::string& error() const { return error_; }

    // Non-empty when the last next_value() stopped because it met an
    // apostrophe. The caller runs that command and then pulls again.
    const std::string& transparent() const { return transparent_; }
    void clear_transparent() { transparent_.clear(); }
    bool exhausted() const { return pos_ >= tokens_.size(); }

private:
    // Evaluates `source` and converts the result for `prompt`.
    bool from_lisp(const Prompt& prompt, const std::string& source, InputValue& out);

    // The drawing, for the current UCS. Typed coordinates are read in it.
    const Database* db_{nullptr};

    std::vector<std::string> tokens_;
    std::string transparent_;
    lisp::Interp& in_;
    std::size_t pos_{0};
    std::string error_;
};

// One iperl for the process. Not a member of either class that uses it: both
// the command prompt and the answer prompt reach it, they are different
// objects, and two co-processes would mean two RPN stacks writing over each
// other's save file.
IperlSession& iperl() {
    static IperlSession session;
    return session;
}

bool PromptLineSource::from_lisp(const Prompt& prompt, const std::string& source,
                                 InputValue& out) {
    in_.clear_error();
    lisp::Value result;
    if (!in_.eval_string(source, result)) {
        error_ = in_.error().message();
        return false;
    }
    std::string why;
    if (!lisp::value_to_input(prompt, result, out, why)) {
        error_ = why;
        return false;
    }
    return true;
}

bool PromptLineSource::next_value(const Prompt& prompt, InputValue& out) {
    if (pos_ >= tokens_.size()) return false;

    std::string token = tokens_[pos_++];

    // !name -- substitute the value of a LISP variable. Evaluating the bare
    // name gets variable lookup, unbound reporting and case-insensitivity for
    // free rather than reimplementing them.
    if (token.size() > 1 && token[0] == '!') {
        return from_lisp(prompt, token.substr(1), out);
    }

    // =expr -- iperl. The result comes back as text and goes through the same
    // parser a typed answer does, so it can answer ANY prompt: `=2*$pi*5` at a
    // radius, `=p("3 4 +")` at a distance, a comma-separated pair at a point.
    // Nothing here knows what kind of answer was wanted, which is what makes
    // one branch enough.
    if (token.size() > 1 && token[0] == '=') {
        std::string text;
        if (!iperl().evaluate(token.substr(1), text)) {
            error_ = text;
            return false;
        }
        // The result REPLACES the token and falls through to the ordinary
        // parser below rather than being parsed here. That is what lets it
        // answer any prompt -- a radius, an angle, a comma-separated point --
        // and it means iperl's answer is read by exactly the same code as a
        // typed one, so the two cannot come to disagree.
        token = text;
    }

    // A parenthesised expression is evaluated and its value used as the answer,
    // except at a string prompt: "bracket (rev 2).dxf" is an ordinary file name
    // and parens are common in paths, where a leading ! is not.
    if (!token.empty() && token[0] == '(' && prompt.kind != PromptKind::String) {
        return from_lisp(prompt, token, out);
    }

    // 'ZOOM and friends: an apostrophe runs a command inside this one and
    // hands the question back. Handled here rather than in parse_input because
    // it is not an answer to the prompt at all -- the prompt is about to be
    // asked again, unchanged.
    if (token.size() > 1 && token[0] == '\'') {
        transparent_ = token.substr(1);
        return false;  // no value: the caller sees the request and runs it
    }

    // R12 has no Escape key over a pipe, so the word is accepted too.
    if (upcase(token) == "CANCEL") {
        out = InputValue::cancel();
        return true;
    }

    // Read fresh, because a UCS command earlier on the same line changes what
    // everything after it means.
    Mat4 frame = Mat4::identity();
    const Mat4* ucs = nullptr;
    if (db_) {
        frame = db_->current_ucs().to_world();
        ucs = &frame;
    }

    if (!parse_input(prompt, token, out, error_, ucs)) return false;
    return true;
}

// Drives a line of answers into the engine, stopping to run any transparent
// command it meets and then carrying on. Without the loop, 'ZOOM would eat the
// rest of the line: the source stops at the apostrophe, and whatever followed
// it would never be pulled.
void run_with_transparent(CommandEngine& engine, PromptOutput& out, PromptLineSource& source) {
    for (;;) {
        engine.run(source);
        if (source.transparent().empty()) break;

        const std::string typed = source.transparent();
        source.clear_transparent();

        const CommandMatch match = resolve_command_name(upcase(typed));
        if (!match.ok()) {
            out.write_error("Unknown command \"" + upcase(typed) + "\".\n");
            continue;
        }
        if (!command_is_transparent(match.name)) {
            // R12 says so rather than running it and destroying the outer
            // command's state behind the user's back.
            out.write_error("** " + match.name + " cannot be used transparently **\n");
            continue;
        }
        engine.begin_transparent(make_command(match.name));
    }
}

// Writes a session's output to the standard streams, which is what a terminal
// front end wants and the GUI does not.
class StreamOutput final : public PromptOutput {
public:
    void write(const std::string& text) override { std::cout << text; }
    void write_error(const std::string& text) override { std::cerr << text; }
};

}  // namespace

std::vector<std::string> split_prompt_line(const std::string& line, bool whole_line) {
    // A string prompt takes the line as typed, spaces and all.
    if (whole_line) {
        const std::string t = trim(line);
        return t.empty() ? std::vector<std::string>{std::string()}
                         : std::vector<std::string>{t};
    }

    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        if (c == ';') break;  // comment to end of line

        // A parenthesised expression stays whole however many spaces it holds.
        if (c == '(') {
            const std::size_t start = i;
            int depth = 0;
            bool in_string = false;
            for (; i < line.size(); ++i) {
                const char d = line[i];
                if (in_string) {
                    if (d == '\\' && i + 1 < line.size()) {
                        ++i;
                    } else if (d == '"') {
                        in_string = false;
                    }
                    continue;
                }
                if (d == '"') {
                    in_string = true;
                } else if (d == '(') {
                    ++depth;
                } else if (d == ')') {
                    if (--depth == 0) {
                        ++i;
                        break;
                    }
                }
            }
            tokens.push_back(line.substr(start, i - start));
            continue;
        }

        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
        tokens.push_back(line.substr(start, i - start));
    }
    return tokens;
}

bool needs_more_input(lisp::Context& ctx, const std::string& text) {
    lisp::Reader reader(ctx, text);
    std::vector<lisp::Value> forms;
    if (reader.read_all(forms)) return false;
    return reader.error().status == lisp::ReadStatus::UnexpectedEof;
}

PromptSession::PromptSession(lisp::Context& ctx, lisp::Interp& in, CommandEngine& engine,
                             PromptOutput& out, bool interactive)
    : ctx_(ctx), in_(in), engine_(engine), out_(out), interactive_(interactive) {}

void PromptSession::report_finished() {
    switch (engine_.status()) {
        case EngineStatus::Finished:
            if (!engine_.message().empty()) out_.write(engine_.message() + "\n");
            break;
        case EngineStatus::Cancelled:
            out_.write("*Cancel*\n");
            break;
        case EngineStatus::Failed:
            out_.write_error("; " + engine_.message() + "\n");
            break;
        default:
            break;
    }
}

void PromptSession::list_commands() {
    std::string s = "Commands:";
    for (const std::string& name : command_names()) s += ' ' + name;
    s += " QUIT\nAliases:";
    for (const CommandAlias& a : command_aliases()) s += ' ' + a.alias + '=' + a.name;
    s += "\nAny abbreviation works; the shortest match wins."
         "\nPoints: 10,20  @5,0 (relative)  @30<45 (polar, degrees)  @ (last point)"
         "\nAnything starting with ( is evaluated as AutoLISP."
         "\n!name prints a variable, or answers a prompt with it."
         "\nCANCEL aborts a command; Enter repeats the last one.\n";
    out_.write(s);
}

std::string PromptSession::current_prompt() const {
    if (!pending_.empty()) return ">  ";
    if (engine_.active()) return engine_.prompt().text();
    return kCommandPrompt;
}

bool PromptSession::feed_line(const std::string& line) {
    // --- an incomplete LISP form, continued ---------------------------------
    if (!pending_.empty() ||
        (!trim(line).empty() && trim(line)[0] == '(' && !engine_.active())) {
        pending_ += line;
        pending_ += '\n';
        if (needs_more_input(ctx_, pending_)) return true;

        const std::string source = pending_;
        pending_.clear();

        in_.clear_error();
        lisp::Value result;
        if (!in_.eval_string(source, result)) {
            out_.write_error("; " + in_.error().message() + "\n");
            return true;
        }
        out_.write(lisp::prin1(result) + "\n");
        return !in_.quit_requested();
    }

    // --- answering a running command ----------------------------------------
    if (engine_.active()) {
        const bool whole_line = engine_.prompt().kind == PromptKind::String;
        std::vector<std::string> tokens = split_prompt_line(line, whole_line);
        // A blank line is Enter. Without this the line yields no tokens, the
        // command never advances, and the next line typed gets eaten as its
        // answer instead.
        if (tokens.empty()) tokens.emplace_back();

        PromptLineSource source(std::move(tokens), in_, &engine_.db());
        run_with_transparent(engine_, out_, source);
        if (source.failed()) out_.write_error("; " + source.error() + "\n");
        if (!engine_.active()) report_finished();
        return true;
    }

    // --- the command prompt -------------------------------------------------
    const std::vector<std::string> tokens = split_prompt_line(line, false);
    if (tokens.empty()) {
        // Enter repeats the last command, as R12 does. Interactive only: blank
        // lines are common in files and silently repeating a command while
        // reading one would quietly duplicate geometry.
        if (interactive_ && !last_command_.empty()) {
            engine_.begin(make_command(last_command_));
            if (!engine_.active()) report_finished();
        }
        return true;
    }

    const std::string head = tokens.front();
    const std::string upper = upcase(head);

    if (confirm_quit_) {
        confirm_quit_ = false;
        if (upper == "Y" || upper == "YES") return false;
        out_.write("Drawing kept.\n");
        return true;
    }

    if (upper == "QUIT" || upper == "EXIT") {
        // Nothing to lose, so nothing to ask.
        if (!engine_.db().journal().dirty()) return false;

        // Asked rather than refused, and defaulting to the answer that loses
        // nothing. Cancel is a real third answer: without it there is no way to
        // back out of a QUIT you did not mean, and the two-answer version makes
        // "No" ambiguous between "do not save" and "do not quit".
        out_.write("Drawing has unsaved changes.\n");
        out_.write("  Yes    -- quit and lose them\n");
        out_.write("  No     -- stay in the drawing\n");
        confirm_quit_ = true;
        return true;
    }
    if (head == "?") {
        list_commands();
        return true;
    }

    // =expr at the command prompt prints the result, the way !name does for a
    // LISP variable. Being able to just ask is most of what a calculator is
    // for, and it is also how you find out whether iperl is there at all.
    if (head.size() > 1 && head[0] == '=') {
        std::string text;
        if (!iperl().evaluate(head.substr(1), text)) {
            out_.write_error("; " + text + "\n");
        } else {
            out_.write(text + "\n");
        }
        return true;
    }

    // !name at the command prompt prints the variable.
    if (head.size() > 1 && head[0] == '!') {
        in_.clear_error();
        lisp::Value result;
        if (!in_.eval_string(head.substr(1), result)) {
            out_.write_error("; " + in_.error().message() + "\n");
        } else {
            out_.write(lisp::prin1(result) + "\n");
        }
        return true;
    }

    // Abbreviations resolve here and only here; see make_command.
    const CommandMatch match = resolve_command_name(upper);
    CommandPtr cmd = match.ok() ? make_command(match.name) : nullptr;
    if (!cmd) {
        out_.write_error("Unknown command \"" + upper + "\". Type ? for a list.\n");
        return true;
    }

    last_command_ = match.name;
    engine_.begin(std::move(cmd));

    // Anything after the command name on the same line answers its prompts.
    if (tokens.size() > 1) {
        PromptLineSource source(std::vector<std::string>(tokens.begin() + 1, tokens.end()),
                                in_, &engine_.db());
        run_with_transparent(engine_, out_, source);
        if (source.failed()) out_.write_error("; " + source.error() + "\n");
    }
    if (!engine_.active()) report_finished();
    return true;
}

bool PromptSession::finish() {
    if (!pending_.empty() && !trim(pending_).empty()) {
        out_.write_error("; unexpected end of input inside an unterminated form\n");
        return false;
    }
    return true;
}

int run_command_prompt(lisp::Context& ctx, lisp::Interp& in, CommandEngine& engine,
                       bool interactive) {
    StreamOutput out;
    PromptSession session(ctx, in, engine, out, interactive);
    std::string line;

    while (true) {
        if (interactive) std::cout << session.current_prompt() << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (!session.feed_line(line)) return 0;
    }

    if (!session.finish()) return 1;
    if (interactive) std::cout << "\n";
    return 0;
}

}  // namespace ncad::app
