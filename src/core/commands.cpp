// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/commands.hpp"

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

// --- ERASE ------------------------------------------------------------------

Step EraseCommand::start(CommandContext&) { return Step::ask(select_prompt()); }

Prompt EraseCommand::select_prompt() const {
    Prompt p;
    p.kind = PromptKind::Entity;
    p.message = selected_.empty() ? "Select objects"
                                  : "Select objects (" + std::to_string(selected_.size()) +
                                        " found)";
    p.allow_empty = true;
    return p;
}

Step EraseCommand::next(CommandContext& ctx, const InputValue& value) {
    // Enter with nothing selected is a no-op, not an error.
    if (value.kind == InputKind::None) {
        for (const Handle h : selected_) ctx.db.erase(h);
        return Step::done(std::to_string(selected_.size()) + " erased");
    }

    if (value.kind != InputKind::Entity) return Step::failed("an entity is required");
    if (!ctx.db.get(value.entity)) return Step::failed("no such entity");

    // Selecting the same entity twice must not erase it twice.
    for (const Handle h : selected_) {
        if (h == value.entity) return Step::ask(select_prompt());
    }
    selected_.push_back(value.entity);
    return Step::ask(select_prompt());
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
    if (upper == "UNDO") return std::make_unique<UndoCommand>();
    if (upper == "REDO") return std::make_unique<RedoCommand>();
    return nullptr;
}

const std::vector<std::string>& command_names() {
    static const std::vector<std::string> names = {"CIRCLE", "DXFOUT", "ERASE",
                                                  "LINE",   "REDO",   "UNDO"};
    return names;
}

const std::vector<CommandAlias>& command_aliases() {
    // The R12 acad.pgp short forms for the commands that exist so far.
    static const std::vector<CommandAlias> aliases = {
        {"C", "CIRCLE"},
        {"E", "ERASE"},
        {"L", "LINE"},
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
