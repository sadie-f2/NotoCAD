// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "prompt.hpp"

#include "noto/commands.hpp"
#include "noto/input_text.hpp"
#include "noto/lisp/command_input.hpp"
#include "noto/lisp/reader.hpp"
#include "noto/lisp/value.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace noto::app {
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
    PromptLineSource(std::vector<std::string> tokens, lisp::Interp& in)
        : tokens_(std::move(tokens)), in_(in) {}

    bool next_value(const Prompt& prompt, InputValue& out) override;

    bool failed() const { return !error_.empty(); }
    const std::string& error() const { return error_; }
    bool exhausted() const { return pos_ >= tokens_.size(); }

private:
    // Evaluates `source` and converts the result for `prompt`.
    bool from_lisp(const Prompt& prompt, const std::string& source, InputValue& out);

    std::vector<std::string> tokens_;
    lisp::Interp& in_;
    std::size_t pos_{0};
    std::string error_;
};

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

    const std::string token = tokens_[pos_++];

    // !name -- substitute the value of a LISP variable. Evaluating the bare
    // name gets variable lookup, unbound reporting and case-insensitivity for
    // free rather than reimplementing them.
    if (token.size() > 1 && token[0] == '!') {
        return from_lisp(prompt, token.substr(1), out);
    }

    // A parenthesised expression is evaluated and its value used as the answer.
    if (!token.empty() && token[0] == '(') {
        return from_lisp(prompt, token, out);
    }

    // R12 has no Escape key over a pipe, so the word is accepted too.
    if (upcase(token) == "CANCEL") {
        out = InputValue::cancel();
        return true;
    }

    if (!parse_input(prompt, token, out, error_)) return false;
    return true;
}

void report_finished(const CommandEngine& engine, std::ostream& os) {
    switch (engine.status()) {
        case EngineStatus::Finished:
            if (!engine.message().empty()) os << engine.message() << "\n";
            break;
        case EngineStatus::Cancelled:
            os << "*Cancel*\n";
            break;
        case EngineStatus::Failed:
            os << "; " << engine.message() << "\n";
            break;
        default:
            break;
    }
}

void list_commands(std::ostream& os) {
    os << "Commands:";
    for (const std::string& name : command_names()) os << ' ' << name;
    os << " QUIT"
       << "\nAnything starting with ( is evaluated as AutoLISP."
       << "\n!name prints a variable, or answers a prompt with it."
       << "\nCANCEL aborts a command; Enter repeats the last one.\n";
}

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

int run_command_prompt(lisp::Context& ctx, lisp::Interp& in, CommandEngine& engine,
                       bool interactive) {
    std::string pending;   // an incomplete LISP form spanning lines
    std::string line;
    std::string last_command;

    while (true) {
        if (interactive) {
            if (!pending.empty()) {
                std::cout << ">  ";
            } else if (engine.active()) {
                std::cout << engine.prompt().text();
            } else {
                std::cout << kCommandPrompt;
            }
            std::cout << std::flush;
        }
        if (!std::getline(std::cin, line)) break;

        // --- an incomplete LISP form, continued -----------------------------
        if (!pending.empty() || (!trim(line).empty() && trim(line)[0] == '(' &&
                                 !engine.active())) {
            pending += line;
            pending += '\n';
            if (needs_more_input(ctx, pending)) continue;

            const std::string source = pending;
            pending.clear();

            in.clear_error();
            lisp::Value result;
            if (!in.eval_string(source, result)) {
                std::cerr << "; " << in.error().message() << "\n";
                continue;
            }
            std::cout << lisp::prin1(result) << "\n";
            if (in.quit_requested()) return 0;
            continue;
        }

        // --- answering a running command ------------------------------------
        if (engine.active()) {
            const bool whole_line = engine.prompt().kind == PromptKind::String;
            std::vector<std::string> tokens = split_prompt_line(line, whole_line);
            // A blank line is Enter. Without this the line yields no tokens, the
            // command never advances, and the next line typed gets eaten as its
            // answer instead.
            if (tokens.empty()) tokens.emplace_back();

            PromptLineSource source(std::move(tokens), in);
            engine.run(source);
            if (source.failed()) std::cerr << "; " << source.error() << "\n";
            if (!engine.active()) report_finished(engine, std::cout);
            continue;
        }

        // --- the command prompt ---------------------------------------------
        const std::vector<std::string> tokens = split_prompt_line(line, false);
        if (tokens.empty()) {
            // Enter repeats the last command, as R12 does. Interactive only:
            // blank lines are common in files and silently repeating a command
            // while reading one would quietly duplicate geometry.
            if (interactive && !last_command.empty()) {
                engine.begin(make_command(last_command));
                if (!engine.active()) report_finished(engine, std::cout);
            }
            continue;
        }

        const std::string head = tokens.front();
        const std::string upper = upcase(head);

        if (upper == "QUIT" || upper == "EXIT") return 0;
        if (head == "?") {
            list_commands(std::cout);
            continue;
        }

        // !name at the command prompt prints the variable.
        if (head.size() > 1 && head[0] == '!') {
            in.clear_error();
            lisp::Value result;
            if (!in.eval_string(head.substr(1), result)) {
                std::cerr << "; " << in.error().message() << "\n";
            } else {
                std::cout << lisp::prin1(result) << "\n";
            }
            continue;
        }

        CommandPtr cmd = make_command(upper);
        if (!cmd) {
            std::cerr << "Unknown command \"" << upper << "\". Type ? for a list.\n";
            continue;
        }

        last_command = upper;
        engine.begin(std::move(cmd));

        // Anything after the command name on the same line answers its prompts.
        if (tokens.size() > 1) {
            PromptLineSource source(
                std::vector<std::string>(tokens.begin() + 1, tokens.end()), in);
            engine.run(source);
            if (source.failed()) std::cerr << "; " << source.error() << "\n";
        }
        if (!engine.active()) report_finished(engine, std::cout);
    }

    if (!pending.empty() && !trim(pending).empty()) {
        std::cerr << "; unexpected end of input inside an unterminated form\n";
        return 1;
    }
    if (interactive) std::cout << "\n";
    return 0;
}

}  // namespace noto::app
