// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/command.hpp"

namespace noto {

// --- Prompt -----------------------------------------------------------------

std::string Prompt::text() const {
    std::string s = message;
    if (!keywords.empty()) {
        s += " or [";
        for (std::size_t i = 0; i < keywords.size(); ++i) {
            if (i != 0) s += '/';
            s += keywords[i];
        }
        s += ']';
    }
    s += ": ";
    return s;
}

// --- InputValue -------------------------------------------------------------

bool prompt_takes_point(PromptKind kind) {
    switch (kind) {
        case PromptKind::Point:
        case PromptKind::Distance:
        case PromptKind::Angle: return true;
        case PromptKind::Real:
        case PromptKind::Integer:
        case PromptKind::String:
        case PromptKind::Entity: return false;
    }
    return false;
}

InputValue InputValue::none() { return InputValue{}; }

InputValue InputValue::cancel() {
    InputValue v;
    v.kind = InputKind::Cancel;
    return v;
}

InputValue InputValue::of_point(const Vec3& p) {
    InputValue v;
    v.kind = InputKind::Point;
    v.point = p;
    return v;
}

InputValue InputValue::of_real(double r) {
    InputValue v;
    v.kind = InputKind::Real;
    v.real = r;
    return v;
}

InputValue InputValue::of_integer(std::int32_t i) {
    InputValue v;
    v.kind = InputKind::Integer;
    v.integer = i;
    return v;
}

InputValue InputValue::of_string(std::string s) {
    InputValue v;
    v.kind = InputKind::String;
    v.text = std::move(s);
    return v;
}

InputValue InputValue::of_keyword(std::string s) {
    InputValue v;
    v.kind = InputKind::Keyword;
    v.text = std::move(s);
    return v;
}

InputValue InputValue::of_entity(Handle h) {
    InputValue v;
    v.kind = InputKind::Entity;
    v.entity = h;
    return v;
}

InputValue InputValue::of_osnap_override(OsnapMask mask) {
    InputValue v;
    v.kind = InputKind::OsnapOverride;
    v.integer = static_cast<std::int32_t>(mask);
    return v;
}

// --- Step -------------------------------------------------------------------

Step Step::ask(Prompt p) {
    Step s;
    s.kind = StepKind::Prompt;
    s.prompt = std::move(p);
    return s;
}

Step Step::done(std::string msg) {
    Step s;
    s.kind = StepKind::Done;
    s.message = std::move(msg);
    return s;
}

Step Step::cancelled() {
    Step s;
    s.kind = StepKind::Cancelled;
    s.message = "*Cancel*";
    return s;
}

Step Step::failed(std::string msg) {
    Step s;
    s.kind = StepKind::Failed;
    s.message = std::move(msg);
    return s;
}

// --- CommandEngine ----------------------------------------------------------

EngineStatus CommandEngine::begin(CommandPtr cmd) {
    if (!cmd) {
        message_ = "no such command";
        status_ = EngineStatus::Failed;
        return status_;
    }
    // Typing a new command at a prompt abandons the current one, as R12 does.
    if (command_) cancel();

    command_ = std::move(cmd);
    message_.clear();
    open_group(command_->name());
    return apply(command_->start(ctx_));
}

EngineStatus CommandEngine::supply(const InputValue& value) {
    if (!command_ || status_ != EngineStatus::Waiting) {
        message_ = "no command is waiting for input";
        status_ = EngineStatus::Failed;
        return status_;
    }
    // Escape is handled here rather than in every command. Work already
    // committed to the database stays, matching R12: cancelling LINE after
    // three segments keeps the three segments.
    if (value.kind == InputKind::Cancel) {
        cancel();
        return status_;
    }
    // A one-shot osnap override is absorbed here and never reaches the command.
    // It does not answer the prompt, so the same question stands and the engine
    // stays Waiting -- which is exactly what typing "cen" mid-prompt should do.
    if (value.kind == InputKind::OsnapOverride) {
        osnap_override_ = static_cast<OsnapMask>(value.integer);
        has_osnap_override_ = true;
        return status_;
    }

    // Any value that does answer a prompt spends the override. One pick is the
    // whole lifetime of one, including a pick that turns out to be a keyword or
    // an Enter -- otherwise a stale override would surprise the next point.
    clear_osnap_override();

    // Every point entered becomes LASTPOINT, whichever command took it.
    if (value.kind == InputKind::Point) set_last_point(value.point);

    return apply(command_->next(ctx_, value));
}

EngineStatus CommandEngine::run(InputSource& src) {
    while (status_ == EngineStatus::Waiting) {
        InputValue value;
        // The source has nothing right now. Suspend and hand control back --
        // this is the line that makes a GUI possible.
        if (!src.next_value(prompt_, value)) return status_;
        supply(value);
    }
    return status_;
}

void CommandEngine::cancel() {
    if (!command_) return;
    // Committed work survives Escape, as in R12 -- cancelling LINE after three
    // segments keeps them. So the group closes normally and those three
    // segments remain one undoable step, rather than being rolled back here.
    close_group();
    command_.reset();
    prompt_ = Prompt{};
    message_ = "*Cancel*";
    status_ = EngineStatus::Cancelled;
}

// One command is one undo step, which is what makes UNDO after a four-segment
// LINE remove the whole line rather than one segment. Groups nest, so a
// (command ...) inside a running command does not start a second one.
void CommandEngine::open_group(const char* name) {
    ctx_.db.journal().begin_group(name ? name : "");
    group_open_ = true;
}

void CommandEngine::close_group() {
    if (!group_open_) return;
    group_open_ = false;
    ctx_.db.journal().end_group();
}

EngineStatus CommandEngine::apply(const Step& step) {
    switch (step.kind) {
        case StepKind::Prompt:
            prompt_ = step.prompt;
            // Commands do not set this; the engine does, so that every prompt
            // can resolve @ without each command having to thread it through.
            prompt_.last_point = last_point_;
            prompt_.has_last_point = has_last_point_;
            status_ = EngineStatus::Waiting;
            return status_;
        case StepKind::Done:
            close_group();
            command_.reset();
            prompt_ = Prompt{};
            message_ = step.message;
            status_ = EngineStatus::Finished;
            return status_;
        case StepKind::Cancelled:
            close_group();
            command_.reset();
            prompt_ = Prompt{};
            message_ = step.message;
            status_ = EngineStatus::Cancelled;
            return status_;
        case StepKind::Failed:
            close_group();
            command_.reset();
            prompt_ = Prompt{};
            message_ = step.message;
            status_ = EngineStatus::Failed;
            return status_;
    }
    return status_;
}

}  // namespace noto
