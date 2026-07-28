// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/commands.hpp"

#include "noto/pick.hpp"
#include "noto/scene.hpp"

#include "noto/dxf.hpp"
#include "noto/entities.hpp"

#include <memory>
#include <string>

namespace noto {
namespace {

std::string upcase(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

bool keyword_is(const InputValue& v, const char* name) {
    return v.kind == InputKind::Keyword && v.text == name;
}

// A distance prompt accepts either a number or a second point, since picking two
// points is how a radius gets specified with a mouse.
bool distance_from(const InputValue& v, const Vec3& base, double& out) {
    if (v.kind == InputKind::Real || v.kind == InputKind::Integer) {
        out = (v.kind == InputKind::Real) ? v.real : static_cast<double>(v.integer);
        return true;
    }
    if (v.kind == InputKind::Point) {
        out = length(v.point - base);
        return true;
    }
    return false;
}

}  // namespace

// --- LINE -------------------------------------------------------------------

Step LineCommand::start(CommandContext&) {
    Prompt p;
    p.kind = PromptKind::Point;
    p.message = "Specify first point";
    return Step::ask(p);
}

Prompt LineCommand::next_prompt() const {
    Prompt p;
    p.kind = PromptKind::Point;
    p.message = "Specify next point";
    p.allow_empty = true;
    p.base = previous_;
    p.has_base = true;
    // Close only makes sense once there is something to close back to.
    if (segments_.size() >= 2) p.keywords.push_back("Close");
    if (!segments_.empty()) p.keywords.push_back("Undo");
    return p;
}

Step LineCommand::next(CommandContext& ctx, const InputValue& value) {
    if (!have_first_) {
        if (value.kind != InputKind::Point) return Step::failed("a point is required");
        first_ = previous_ = value.point;
        vertices_.push_back(value.point);
        have_first_ = true;
        return Step::ask(next_prompt());
    }

    // Enter ends the command, which is why the loop needs no count.
    if (value.kind == InputKind::None) return Step::done();

    if (keyword_is(value, "CLOSE")) {
        if (segments_.size() < 2) return Step::failed("nothing to close");
        ctx.db.add(std::make_unique<Line>(previous_, first_));
        return Step::done();
    }

    if (keyword_is(value, "UNDO")) {
        if (segments_.empty()) return Step::failed("nothing to undo");
        ctx.db.erase(segments_.back());
        segments_.pop_back();
        // The vertex list is the command's own state, so backing up is a pop
        // rather than a query against entities that may since have changed.
        vertices_.pop_back();
        previous_ = vertices_.back();
        return Step::ask(next_prompt());
    }

    if (value.kind != InputKind::Point) return Step::failed("a point is required");

    segments_.push_back(ctx.db.add(std::make_unique<Line>(previous_, value.point)));
    vertices_.push_back(value.point);
    previous_ = value.point;
    return Step::ask(next_prompt());
}

// --- CIRCLE -----------------------------------------------------------------

Step CircleCommand::start(CommandContext&) {
    Prompt p;
    p.kind = PromptKind::Point;
    p.message = "Specify center point for circle";
    return Step::ask(p);
}

Step CircleCommand::next(CommandContext& ctx, const InputValue& value) {
    switch (state_) {
        case State::Centre: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            centre_ = value.point;
            state_ = State::Radius;

            Prompt p;
            p.kind = PromptKind::Distance;
            p.message = "Specify radius of circle";
            p.keywords.push_back("Diameter");
            p.base = centre_;
            p.has_base = true;
            return Step::ask(p);
        }

        case State::Radius: {
            if (keyword_is(value, "DIAMETER")) {
                diameter_ = true;
                Prompt p;
                p.kind = PromptKind::Distance;
                p.message = "Specify diameter of circle";
                p.base = centre_;
                p.has_base = true;
                return Step::ask(p);
            }

            double d = 0.0;
            if (!distance_from(value, centre_, d)) return Step::failed("a distance is required");
            const double radius = diameter_ ? d * 0.5 : d;
            if (radius <= 0.0) return Step::failed("radius must be positive");

            ctx.db.add(std::make_unique<Circle>(centre_, radius));
            return Step::done();
        }
    }
    return Step::failed("internal state error");
}

// --- selection --------------------------------------------------------------

Prompt SelectionPrompter::prompt(const CommandContext& ctx) const {
    Prompt p;

    if (state_ != State::Selecting) {
        // The corner sub-prompts. R12 words them by mode, so you can tell a
        // window from a crossing box before you drag it rather than after.
        p.kind = PromptKind::Point;
        const char* what = crossing_ ? "crossing" : "window";
        if (state_ == State::FirstCorner) {
            p.message = std::string("First corner of ") + what;
        } else {
            p.message = "Other corner";
            // Gives the viewport something to rubber-band the box from.
            p.base = first_;
            p.has_base = true;
        }
        return p;
    }

    p.kind = PromptKind::Entity;
    const std::size_t n = ctx.selection.size();
    p.message = removing_ ? "Remove objects" : "Select objects";
    if (n != 0) p.message += " (" + std::to_string(n) + " found)";

    // One vocabulary, written once, so every command that selects offers the
    // same words.
    p.keywords = {"Window", "Crossing", "Last", "Previous", "ALL", "Remove", "Add"};
    p.allow_empty = true;
    return p;
}

void SelectionPrompter::apply_region(CommandContext& ctx, const Vec3& a, const Vec3& b) {
    // The two corners came back in whatever order they were given, and the
    // region wants a positive-extent frame -- so build the axes from the
    // diagonal rather than assuming which corner came first.
    const Vec3 d = b - a;

    SelectionRegion r;
    r.origin = a;

    // The box is screen-aligned, so its axes are the view's. Flipped below so
    // the extents come out positive whichever way the drag went; a degenerate
    // drag then yields a zero-size region that selects nothing, which is the
    // safe way for it to fail.
    Vec3 ax = view_ax_;
    Vec3 ay = view_ay_;
    if (is_zero(cross(ax, ay))) {
        ax = Vec3{1, 0, 0};
        ay = Vec3{0, 1, 0};
    }

    double u = dot(d, ax);
    double v = dot(d, ay);
    if (u < 0.0) {
        ax = ax * -1.0;
        u = -u;
    }
    if (v < 0.0) {
        ay = ay * -1.0;
        v = -v;
    }
    r.ax = ax;
    r.ay = ay;
    r.width = u;
    r.height = v;

    const std::size_t n = removing_ ? deselect_by_region(ctx.db, draw_, r, crossing_, ctx.selection)
                                    : select_by_region(ctx.db, draw_, r, crossing_, ctx.selection);

    // STRETCH asks which defining points fell inside the crossing box, so the
    // box is kept -- and only a crossing one, since a window box is not a
    // stretch region and keeping it would make STRETCH silently act like MOVE.
    if (crossing_ && !removing_) ctx.selection.set_region(r);

    note_ = std::to_string(n) + (removing_ ? " removed" : " found");
}

SelectionPrompter::Result SelectionPrompter::feed(CommandContext& ctx, const InputValue& value) {
    note_.clear();

    if (state_ != State::Selecting) {
        if (value.kind != InputKind::Point) return Result::Rejected;
        if (state_ == State::FirstCorner) {
            first_ = value.point;
            state_ = State::SecondCorner;
            return Result::Selecting;
        }
        apply_region(ctx, first_, value.point);
        state_ = State::Selecting;
        return Result::Selecting;
    }

    if (value.kind == InputKind::None) return Result::Finished;

    if (value.kind == InputKind::Keyword) {
        const std::string& k = value.text;
        if (k == "WINDOW" || k == "CROSSING") {
            crossing_ = (k == "CROSSING");
            state_ = State::FirstCorner;
            return Result::Selecting;
        }
        if (k == "REMOVE") {
            removing_ = true;
            return Result::Selecting;
        }
        if (k == "ADD") {
            removing_ = false;
            return Result::Selecting;
        }
        if (k == "LAST") {
            const Handle h = ctx.db.last();
            if (h == kNullHandle) {
                note_ = "Nothing to select";
                return Result::Selecting;
            }
            if (removing_ ? ctx.selection.remove(h) : ctx.selection.add(h)) {
                note_ = removing_ ? "1 removed" : "1 found";
            }
            return Result::Selecting;
        }
        if (k == "PREVIOUS") {
            std::size_t n = 0;
            for (const Handle h : ctx.previous.handles()) {
                // Entities erased since then are skipped rather than reselected
                // as dangling handles.
                if (!ctx.db.get(h)) continue;
                if (removing_ ? ctx.selection.remove(h) : ctx.selection.add(h)) ++n;
            }
            note_ = std::to_string(n) + (removing_ ? " removed" : " found");
            return Result::Selecting;
        }
        if (k == "ALL") {
            std::size_t n = 0;
            for (const Handle h : ctx.db.order()) {
                const Entity* e = ctx.db.get(h);
                // R12's All skips what is not visible: you cannot erase what you
                // cannot see without noticing afterwards.
                if (!e || !entity_visible(ctx.db, *e)) continue;
                if (removing_ ? ctx.selection.remove(h) : ctx.selection.add(h)) ++n;
            }
            note_ = std::to_string(n) + (removing_ ? " removed" : " found");
            return Result::Selecting;
        }
        return Result::Rejected;
    }

    if (value.kind != InputKind::Entity) return Result::Rejected;
    if (!ctx.db.get(value.entity)) return Result::Rejected;

    if (removing_ ? ctx.selection.remove(value.entity) : ctx.selection.add(value.entity)) {
        note_ = removing_ ? "1 removed" : "1 found";
    }
    return Result::Selecting;
}

// --- ERASE ------------------------------------------------------------------

Step EraseCommand::start(CommandContext& ctx) { return Step::ask(select_.prompt(ctx)); }

Step EraseCommand::next(CommandContext& ctx, const InputValue& value) {
    switch (select_.feed(ctx, value)) {
        case SelectionPrompter::Result::Selecting:
            return Step::ask(select_.prompt(ctx));

        case SelectionPrompter::Result::Finished: {
            // Enter with nothing selected is a no-op, not an error -- missing
            // everything is how you decide you are finished.
            std::size_t n = 0;
            for (const Handle h : ctx.selection.handles()) {
                if (ctx.db.erase(h)) ++n;
            }
            return Step::done(std::to_string(n) + " erased");
        }

        case SelectionPrompter::Result::Rejected:
            break;
    }
    return Step::failed("an entity is required");
}

// --- UNDO / REDO ------------------------------------------------------------

Step UndoCommand::start(CommandContext& ctx) {
    UndoJournal& j = ctx.db.journal();
    if (!j.can_undo()) return Step::done("Nothing to undo");

    // Named before the undo runs: afterwards the group has moved to the redo
    // stack and undo_name() is talking about something else.
    const std::string what = j.undo_name();
    if (!j.undo(ctx.db)) return Step::failed("undo failed");
    return Step::done(what.empty() ? "Undone" : what);
}

Step UndoCommand::next(CommandContext&, const InputValue&) {
    return Step::failed("UNDO takes no input");
}

Step RedoCommand::start(CommandContext& ctx) {
    UndoJournal& j = ctx.db.journal();
    if (!j.can_redo()) return Step::done("Nothing to redo");

    const std::string what = j.redo_name();
    if (!j.redo(ctx.db)) return Step::failed("redo failed");
    return Step::done(what.empty() ? "Redone" : what);
}

Step RedoCommand::next(CommandContext&, const InputValue&) {
    return Step::failed("REDO takes no input");
}

// --- MOVE / COPY ------------------------------------------------------------

Step MoveCommand::start(CommandContext& ctx) { return Step::ask(select_.prompt(ctx)); }

Step MoveCommand::apply(CommandContext& ctx, const Vec3& delta) {
    const Mat4 m = Mat4::translation(delta);

    // Snapshotted, because COPY adds to the database while iterating it and a
    // fresh clone must not then be copied again.
    const std::vector<Handle> handles = ctx.selection.handles();

    std::size_t n = 0;
    for (const Handle h : handles) {
        const Entity* e = ctx.db.get(h);
        if (!e) continue;  // erased since selection; skip rather than fail

        EntityPtr moved = e->clone();
        moved->transform(m);

        // The one line that differs. MOVE replaces in place so the handle
        // survives -- AutoLISP may be holding it, and undo records the swap.
        if (copy_) {
            ctx.db.add(std::move(moved));
        } else {
            ctx.db.replace(h, std::move(moved));
        }
        ++n;
    }

    return Step::done(std::to_string(n) + (copy_ ? " copied" : " moved"));
}

Step MoveCommand::next(CommandContext& ctx, const InputValue& value) {
    switch (state_) {
        case State::Selecting: {
            switch (select_.feed(ctx, value)) {
                case SelectionPrompter::Result::Selecting:
                    return Step::ask(select_.prompt(ctx));
                case SelectionPrompter::Result::Rejected:
                    return Step::failed("an entity is required");
                case SelectionPrompter::Result::Finished:
                    break;
            }
            if (ctx.selection.empty()) return Step::done("Nothing selected");

            state_ = State::Base;
            Prompt p;
            p.kind = PromptKind::Point;
            p.message = "Base point or displacement";
            return Step::ask(p);
        }

        case State::Base: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            base_ = value.point;
            state_ = State::Displacement;

            Prompt p;
            p.kind = PromptKind::Point;
            p.message = "Second point of displacement";
            p.allow_empty = true;  // Enter means the base point was the vector
            p.base = base_;
            p.has_base = true;
            return Step::ask(p);
        }

        case State::Displacement: {
            // R12's "<displacement>": Enter here means the first point was the
            // vector itself, measured from the origin.
            if (value.kind == InputKind::None) return apply(ctx, base_);
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            return apply(ctx, value.point - base_);
        }
    }
    return Step::failed("internal state error");
}

// --- DXFOUT -----------------------------------------------------------------

Step DxfOutCommand::start(CommandContext&) {
    Prompt p;
    p.kind = PromptKind::String;
    p.message = "Enter file name";
    return Step::ask(p);
}

Step DxfOutCommand::next(CommandContext& ctx, const InputValue& value) {
    if (value.kind != InputKind::String || value.text.empty()) {
        return Step::failed("a file name is required");
    }

    // R12 supplies the extension when you leave it off.
    std::string path = value.text;
    if (path.size() < 4 || upcase(path.substr(path.size() - 4)) != ".DXF") path += ".dxf";

    if (!write_dxf_file(ctx.db, path)) return Step::failed("cannot write " + path);
    return Step::done(path + " written");
}

// --- registry ---------------------------------------------------------------

CommandPtr make_command(std::string_view name) {
    const std::string upper = upcase(name);
    if (upper == "LINE") return std::make_unique<LineCommand>();
    if (upper == "CIRCLE") return std::make_unique<CircleCommand>();
    if (upper == "ERASE") return std::make_unique<EraseCommand>();
    if (upper == "DXFOUT") return std::make_unique<DxfOutCommand>();
    if (upper == "MOVE") return std::make_unique<MoveCommand>(false);
    if (upper == "COPY") return std::make_unique<MoveCommand>(true);
    if (upper == "UNDO") return std::make_unique<UndoCommand>();
    if (upper == "REDO") return std::make_unique<RedoCommand>();
    return nullptr;
}

const std::vector<std::string>& command_names() {
    static const std::vector<std::string> names = {"CIRCLE", "COPY", "DXFOUT", "ERASE",
                                                  "LINE",   "MOVE", "REDO",   "UNDO"};
    return names;
}

const std::vector<CommandAlias>& command_aliases() {
    // The R12 acad.pgp short forms for the commands that exist so far.
    static const std::vector<CommandAlias> aliases = {
        {"C", "CIRCLE"},
        {"E", "ERASE"},
        {"CP", "COPY"},
        {"L", "LINE"},
        {"M", "MOVE"},
        {"U", "UNDO"},
    };
    return aliases;
}

CommandMatch resolve_in(std::string_view typed, const std::vector<std::string>& names,
                        const std::vector<CommandAlias>& aliases) {
    CommandMatch result;
    const std::string upper = upcase(typed);
    if (upper.empty()) return result;

    // An exact name beats everything, so a command can never be shadowed by an
    // abbreviation of another.
    for (const std::string& name : names) {
        if (name == upper) {
            result.name = name;
            return result;
        }
    }

    // Then the alias table, which exists to override prefix matching.
    for (const CommandAlias& alias : aliases) {
        if (alias.alias == upper) {
            result.name = alias.name;
            return result;
        }
    }

    // Finally the unique-prefix rule.
    for (const std::string& name : names) {
        if (name.size() < upper.size()) continue;
        if (name.compare(0, upper.size(), upper) != 0) continue;
        result.candidates.push_back(name);
    }
    if (result.candidates.size() == 1) {
        result.name = result.candidates.front();
        result.candidates.clear();
        return result;
    }
    if (result.candidates.size() > 1) {
        result.ambiguous = true;
        // Shortest wins, alphabetical breaks ties. Stable forever, and it puts
        // the fundamental command ahead of the elaborate one sharing its prefix.
        const std::string* best = &result.candidates.front();
        for (const std::string& candidate : result.candidates) {
            if (candidate.size() < best->size() ||
                (candidate.size() == best->size() && candidate < *best)) {
                best = &candidate;
            }
        }
        result.name = *best;
    }
    return result;
}

CommandMatch resolve_command_name(std::string_view typed) {
    return resolve_in(typed, command_names(), command_aliases());
}

}  // namespace noto
