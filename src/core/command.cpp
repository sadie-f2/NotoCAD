// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/command.hpp"

namespace ncad {

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

InputValue InputValue::of_picked_entity(Handle h, const Vec3& at) {
    InputValue v = of_entity(h);
    v.point = at;
    v.has_point = true;
    return v;
}

InputValue InputValue::of_deferred_snap(const Vec3& at, OsnapType type, Handle from) {
    InputValue v = of_point(at);
    v.snap_type = type;
    v.snap_entity = from;
    v.snap_deferred = true;
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

    // A selection that was built and used becomes Previous, and the working set
    // starts empty. Only a non-empty one is promoted, so Previous survives the
    // LINE you ran between ERASE and MOVE.
    if (!selection_.empty()) {
        previous_ = selection_;
        selection_.clear();
    }

    command_ = std::move(cmd);
    message_.clear();
    open_group(command_->name());
    return apply(command_->start(ctx_));
}

EngineStatus CommandEngine::begin_transparent(CommandPtr cmd) {
    if (!cmd) {
        message_ = "no such command";
        return status_;
    }
    // Nothing to be transparent over: run it as an ordinary command.
    if (!command_ || status_ != EngineStatus::Waiting) return begin(std::move(cmd));

    // Nested apostrophes are refused rather than stacked. One level covers
    // 'ZOOM inside LINE, which is the whole of what this is for, and a stack
    // would need a matching answer for what Escape means at depth three.
    if (transparent_) {
        message_ = "a transparent command is already running";
        return status_;
    }

    outer_prompt_ = prompt_;
    transparent_ = std::move(cmd);

    // No open_group(): a transparent command sits inside the outer command's
    // undo group and changes nothing to record in any case.
    const Step step = transparent_->start(ctx_);
    return apply_transparent(step);
}

EngineStatus CommandEngine::apply_transparent(const Step& step) {
    switch (step.kind) {
        case StepKind::Prompt:
            prompt_ = step.prompt;
            prompt_.last_point = last_point_;
            prompt_.has_last_point = has_last_point_;
            status_ = EngineStatus::Waiting;
            return status_;

        case StepKind::Done:
        case StepKind::Cancelled:
        case StepKind::Failed:
            // Whatever happened, the outer command gets its question back. A
            // failed 'ZOOM must not take LINE down with it.
            transparent_.reset();
            message_ = step.message;
            prompt_ = outer_prompt_;
            status_ = EngineStatus::Waiting;
            return status_;
    }
    return status_;
}

EngineStatus CommandEngine::supply(const InputValue& value) {
    // A transparent command owns the prompt while it is running.
    if (transparent_ && status_ == EngineStatus::Waiting) {
        if (value.kind == InputKind::Cancel) {
            // Escape abandons the transparent command only, leaving the outer
            // one exactly as it was. Cancelling LINE by escaping out of a 'ZOOM
            // would lose work for a keystroke that was about the view.
            transparent_.reset();
            prompt_ = outer_prompt_;
            status_ = EngineStatus::Waiting;
            message_.clear();
            return status_;
        }
        return apply_transparent(transparent_->next(ctx_, value));
    }

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


bool CommandEngine::preview(const InputValue& tentative, InFlight& out) {
    out.clear();

    // A transparent command is a ZOOM or a PAN over the top of the real one.
    // It changes no drawing state, so it has nothing to show, and the outer
    // command is suspended rather than standing at a prompt.
    if (transparent_ != nullptr) return false;
    if (command_ == nullptr || status_ != EngineStatus::Waiting) return false;

    return command_->preview(ctx_, tentative, out);
}

}  // namespace ncad
