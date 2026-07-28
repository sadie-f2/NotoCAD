// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/commands.hpp"

#include "noto/pick.hpp"
#include "noto/scene.hpp"

#include "noto/dxf.hpp"
#include "noto/entities.hpp"

#include <memory>
#include <cmath>
#include <cstdio>
#include <numbers>
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
// An angle, in radians. Typed as degrees -- R12 talks degrees to the user and
// radians to AutoLISP -- or shown by pointing, in which case it is the direction
// from the base point to where you pointed.
bool angle_from(const InputValue& v, const Vec3& base, double& out) {
    if (v.kind == InputKind::Real || v.kind == InputKind::Integer) {
        const double degrees = (v.kind == InputKind::Real) ? v.real : static_cast<double>(v.integer);
        out = degrees * std::numbers::pi / 180.0;
        return true;
    }
    if (v.kind == InputKind::Point) {
        const Vec3 d = v.point - base;
        if (is_zero(d)) return false;
        out = std::atan2(d.y, d.x);
        return true;
    }
    return false;
}

// Like distance_from, but keeps the sign of a typed number. ARRAY needs it:
// a negative row distance arrays downward rather than upward.
bool signed_distance_from(const InputValue& v, const Vec3& base, double& out) {
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

namespace {

// Enter is allowed here and means two different things depending on the mode --
// end the command in Multiple, or "<displacement>" otherwise. The caller decides
// which; this only has to permit it.
Prompt displacement_prompt(const Vec3& base) {
    Prompt p;
    p.kind = PromptKind::Point;
    p.message = "Second point of displacement";
    p.allow_empty = true;
    p.base = base;
    p.has_base = true;
    return p;
}

}  // namespace

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

    placed_ += n;
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
            // R12 offers Multiple on COPY only: moving something to several
            // places at once is not a thing.
            if (copy_) p.keywords.push_back("Multiple");
            return Step::ask(p);
        }

        case State::Base: {
            if (copy_ && keyword_is(value, "MULTIPLE")) {
                multiple_ = true;
                Prompt p;
                p.kind = PromptKind::Point;
                p.message = "Base point";
                return Step::ask(p);
            }
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            base_ = value.point;
            state_ = State::Displacement;
            return Step::ask(displacement_prompt(base_));
        }

        case State::Displacement: {
            if (value.kind == InputKind::None) {
                // Two readings of Enter, and the mode decides which. In Multiple
                // mode it ends the command; otherwise it is R12's
                // "<displacement>", meaning the base point was the vector
                // itself, measured from the origin.
                if (multiple_) {
                    return Step::done(std::to_string(placed_) + " copied");
                }
                return apply(ctx, base_);
            }
            if (value.kind != InputKind::Point) return Step::failed("a point is required");

            if (!multiple_) return apply(ctx, value.point - base_);

            // Multiple: place one and ask again, always measuring from the same
            // base point, so the copies fan out from where you started rather
            // than chaining off each other.
            const Step placed = apply(ctx, value.point - base_);
            if (placed.kind != StepKind::Done) return placed;
            return Step::ask(displacement_prompt(base_));
        }
    }
    return Step::failed("internal state error");
}

// --- ROTATE / SCALE / MIRROR ------------------------------------------------

namespace {

// The normal of the current construction plane. World Z until UCS exists; this
// is the single place that has to learn about UCS later.
Vec3 construction_normal() {
    return kWorldZ;
}

}  // namespace

const char* TransformCommand::name() const {
    switch (kind_) {
        case Kind::Rotate: return "ROTATE";
        case Kind::Scale: return "SCALE";
        case Kind::Mirror: return "MIRROR";
    }
    return "?";
}

Step TransformCommand::start(CommandContext& ctx) { return Step::ask(select_.prompt(ctx)); }

Prompt TransformCommand::amount_prompt() const {
    Prompt p;
    switch (kind_) {
        case Kind::Rotate:
            p.kind = PromptKind::Angle;
            p.message = "Rotation angle";
            break;
        case Kind::Scale:
            p.kind = PromptKind::Distance;
            p.message = "Scale factor";
            break;
        case Kind::Mirror:
            p.kind = PromptKind::Point;
            p.message = "Second point of mirror line";
            break;
    }
    // Both magnitudes can be shown instead of typed: an angle by pointing along
    // it, a scale by pointing at the distance it should become.
    p.base = base_;
    p.has_base = true;
    return p;
}

Step TransformCommand::apply(CommandContext& ctx, const Mat4& m, bool erase_originals) {
    // Snapshotted: MIRROR without deletion adds while iterating, as COPY does.
    const std::vector<Handle> handles = ctx.selection.handles();

    std::size_t n = 0;
    for (const Handle h : handles) {
        const Entity* e = ctx.db.get(h);
        if (!e) continue;

        EntityPtr moved = e->clone();
        moved->transform(m);

        if (erase_originals) {
            // In place, so the handle survives an AutoLISP ename.
            ctx.db.replace(h, std::move(moved));
        } else {
            ctx.db.add(std::move(moved));
        }
        ++n;
    }
    return Step::done(std::to_string(n) + " changed");
}

Step TransformCommand::next(CommandContext& ctx, const InputValue& value) {
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
            p.message = (kind_ == Kind::Mirror) ? "First point of mirror line" : "Base point";
            return Step::ask(p);
        }

        case State::Base: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            base_ = value.point;
            state_ = (kind_ == Kind::Mirror) ? State::MirrorSecond : State::Amount;
            return Step::ask(amount_prompt());
        }

        case State::Amount: {
            if (kind_ == Kind::Rotate) {
                double radians = 0.0;
                if (!angle_from(value, base_, radians)) return Step::failed("an angle is required");
                return apply(ctx, Mat4::rotation(base_, construction_normal(), radians), true);
            }

            double factor = 0.0;
            if (!distance_from(value, base_, factor)) return Step::failed("a factor is required");
            // Zero collapses everything to a point and is not undoable by
            // scaling back, so it is refused rather than obeyed.
            if (factor <= 0.0) return Step::failed("scale factor must be positive");
            return apply(ctx,
                         Mat4::translation(base_) * Mat4::uniform_scaling(factor) *
                             Mat4::translation(base_ * -1.0),
                         true);
        }

        case State::MirrorSecond: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            mirror_second_ = value.point;

            const Vec3 along = mirror_second_ - base_;
            if (is_zero(along)) return Step::failed("the mirror line has no direction");

            state_ = State::MirrorDelete;
            Prompt p;
            p.kind = PromptKind::String;
            p.message = "Delete old objects? <N>";
            p.keywords = {"Yes", "No"};
            p.allow_empty = true;
            return Step::ask(p);
        }

        case State::MirrorDelete: {
            // R12 defaults to keeping them, so Enter means No.
            const bool erase_originals = keyword_is(value, "YES");
            if (!erase_originals && value.kind != InputKind::None && !keyword_is(value, "NO")) {
                return Step::failed("answer Yes or No");
            }

            // The mirror plane contains the line and the plane normal, so its
            // own normal is perpendicular to both.
            const Vec3 along = mirror_second_ - base_;
            const Vec3 normal = cross(along, construction_normal());
            if (is_zero(normal)) return Step::failed("the mirror line has no direction");

            return apply(ctx, Mat4::mirror(base_, normalize(normal)), erase_originals);
        }
    }
    return Step::failed("internal state error");
}

// --- ARRAY ------------------------------------------------------------------

Step ArrayCommand::start(CommandContext& ctx) { return Step::ask(select_.prompt(ctx)); }

std::size_t ArrayCommand::place(CommandContext& ctx, const Mat4& m) {
    // Snapshotted: this adds to the database while walking the selection, and a
    // fresh copy must not become an item of the array in its own right.
    const std::vector<Handle> handles = ctx.selection.handles();

    std::size_t n = 0;
    for (const Handle h : handles) {
        const Entity* e = ctx.db.get(h);
        if (!e) continue;
        EntityPtr copy = e->clone();
        copy->transform(m);
        ctx.db.add(std::move(copy));
        ++n;
    }
    return n;
}

Step ArrayCommand::ask_rows() {
    state_ = State::Rows;
    Prompt p;
    p.kind = PromptKind::Integer;
    p.message = "Number of rows (---) <1>";
    p.allow_empty = true;
    return Step::ask(p);
}

Step ArrayCommand::ask_columns() {
    state_ = State::Columns;
    Prompt p;
    p.kind = PromptKind::Integer;
    p.message = "Number of columns (|||) <1>";
    p.allow_empty = true;
    return Step::ask(p);
}

Step ArrayCommand::ask_spacing(bool rows) {
    state_ = rows ? State::RowSpacing : State::ColumnSpacing;
    Prompt p;
    p.kind = PromptKind::Distance;
    p.message = rows ? "Distance between rows (---)" : "Distance between columns (|||)";
    return Step::ask(p);
}

Step ArrayCommand::build_rectangular(CommandContext& ctx) {
    std::size_t n = 0;
    for (std::int32_t r = 0; r < rows_; ++r) {
        for (std::int32_t c = 0; c < columns_; ++c) {
            // The original is the item at (0,0); it is already in the drawing.
            if (r == 0 && c == 0) continue;
            const Vec3 delta{column_spacing_ * c, row_spacing_ * r, 0.0};
            n += place(ctx, Mat4::translation(delta));
        }
    }
    return Step::done(std::to_string(n) + " added");
}

Step ArrayCommand::build_polar(CommandContext& ctx) {
    // Spacing between items. A full circle divides by the count so the last item
    // does not land on the first; a partial fill divides by one less so the
    // first and last sit on the ends of the arc.
    //
    // NOTE: that rule is the sensible reading and is not verified against the
    // R12 documentation -- flagged in SF_todo.md.
    const double two_pi = 2.0 * std::numbers::pi;
    const bool full = std::abs(std::abs(fill_) - two_pi) < 1e-9;
    const double step =
        full ? fill_ / count_ : (count_ > 1 ? fill_ / (count_ - 1) : 0.0);

    std::size_t n = 0;
    for (std::int32_t i = 1; i < count_; ++i) {
        n += place(ctx, Mat4::rotation(centre_, kWorldZ, step * i));
    }
    return Step::done(std::to_string(n) + " added");
}

Step ArrayCommand::next(CommandContext& ctx, const InputValue& value) {
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

            state_ = State::Type;
            Prompt p;
            p.kind = PromptKind::String;
            p.message = "Rectangular or Polar array (R/P) <R>";
            p.keywords = {"Rectangular", "Polar"};
            p.allow_empty = true;
            return Step::ask(p);
        }

        case State::Type: {
            if (keyword_is(value, "POLAR")) {
                state_ = State::Centre;
                Prompt p;
                p.kind = PromptKind::Point;
                p.message = "Center point of array";
                return Step::ask(p);
            }
            // Enter or R: rectangular, which is R12's default.
            if (value.kind != InputKind::None && !keyword_is(value, "RECTANGULAR")) {
                return Step::failed("answer R or P");
            }
            return ask_rows();
        }

        case State::Rows: {
            rows_ = (value.kind == InputKind::None) ? 1 : value.integer;
            if (value.kind != InputKind::None && value.kind != InputKind::Integer) {
                return Step::failed("a number is required");
            }
            if (rows_ < 1) return Step::failed("row count must be positive");
            return ask_columns();
        }

        case State::Columns: {
            columns_ = (value.kind == InputKind::None) ? 1 : value.integer;
            if (value.kind != InputKind::None && value.kind != InputKind::Integer) {
                return Step::failed("a number is required");
            }
            if (columns_ < 1) return Step::failed("column count must be positive");
            // One row by one column is the drawing as it stands.
            if (rows_ == 1 && columns_ == 1) return Step::done("0 added");
            // R12 skips a spacing it cannot use.
            if (rows_ == 1) return ask_spacing(false);
            return ask_spacing(true);
        }

        case State::RowSpacing: {
            // Signed: a negative distance arrays downward, which is how you
            // array below the original rather than above it.
            if (!signed_distance_from(value, Vec3{0, 0, 0}, row_spacing_)) {
                return Step::failed("a distance is required");
            }
            if (columns_ == 1) return build_rectangular(ctx);
            return ask_spacing(false);
        }

        case State::ColumnSpacing: {
            if (!signed_distance_from(value, Vec3{0, 0, 0}, column_spacing_)) {
                return Step::failed("a distance is required");
            }
            return build_rectangular(ctx);
        }

        case State::Centre: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            centre_ = value.point;
            state_ = State::Count;

            Prompt p;
            p.kind = PromptKind::Integer;
            p.message = "Number of items";
            return Step::ask(p);
        }

        case State::Count: {
            if (value.kind != InputKind::Integer) return Step::failed("a number is required");
            count_ = value.integer;
            if (count_ < 1) return Step::failed("item count must be positive");
            state_ = State::Fill;

            Prompt p;
            p.kind = PromptKind::Angle;
            p.message = "Angle to fill (+=ccw, -=cw) <360>";
            p.allow_empty = true;
            p.base = centre_;
            p.has_base = true;
            return Step::ask(p);
        }

        case State::Fill: {
            if (value.kind == InputKind::None) {
                fill_ = 2.0 * std::numbers::pi;
            } else if (!angle_from(value, centre_, fill_)) {
                return Step::failed("an angle is required");
            }
            if (fill_ == 0.0) return Step::failed("angle to fill cannot be zero");

            state_ = State::RotateItems;
            Prompt p;
            p.kind = PromptKind::String;
            p.message = "Rotate objects as they are copied? <Y>";
            p.keywords = {"Yes", "No"};
            p.allow_empty = true;
            return Step::ask(p);
        }

        case State::RotateItems: {
            const bool rotate = (value.kind == InputKind::None) || keyword_is(value, "YES");
            if (!rotate && !keyword_is(value, "NO")) return Step::failed("answer Yes or No");
            if (rotate) return build_polar(ctx);

            // Not rotating: each item keeps its orientation while its position
            // travels the arc. The point that follows the arc is the selection's
            // bounding-box centre -- R12 uses an object base point, which for
            // these entity types amounts to the same idea.
            //
            // NOTE: which reference point R12 actually uses is unverified; see
            // SF_todo.md.
            BBox extent;
            for (const Handle h : ctx.selection.handles()) {
                const Entity* e = ctx.db.get(h);
                if (e) extent.expand(e->bbox());
            }
            if (!extent.valid()) return Step::done("0 added");
            const Vec3 ref = extent.center();

            const double two_pi = 2.0 * std::numbers::pi;
            const bool full = std::abs(std::abs(fill_) - two_pi) < 1e-9;
            const double step = full ? fill_ / count_ : (count_ > 1 ? fill_ / (count_ - 1) : 0.0);

            std::size_t n = 0;
            for (std::int32_t i = 1; i < count_; ++i) {
                const Mat4 r = Mat4::rotation(centre_, kWorldZ, step * i);
                n += place(ctx, Mat4::translation(r.transform_point(ref) - ref));
            }
            return Step::done(std::to_string(n) + " added");
        }
    }
    return Step::failed("internal state error");
}

// --- STRETCH ----------------------------------------------------------------

Step StretchCommand::start(CommandContext& ctx) { return Step::ask(select_.prompt(ctx)); }

namespace {

// Which of an entity's grips STRETCH is allowed to move.
//
// The Stretch-kind ones, when it has any -- a line's endpoints, an arc's. A
// line's midpoint grip is Move, and including it would let a crossing window
// over the middle of a line drag the whole line, which is not what STRETCH
// does.
//
// An entity with no Stretch grips at all falls back to its Move grip, because
// that is its definition point: a circle has no stretchable geometry, and R12
// moves a circle when its centre is inside the window.
//
// NOTE: an arc caught by its centre alone does nothing here, since only its
// endpoints are Stretch grips. Whether R12 moves it is unverified; see
// SF_todo.md.
void eligible_grips(const std::vector<Grip>& grips, const SelectionRegion& region,
                    std::vector<GripIndex>& out) {
    out.clear();

    bool any_stretch = false;
    for (const Grip& g : grips) {
        if (g.kind == GripKind::Stretch) {
            any_stretch = true;
            break;
        }
    }

    const GripKind wanted = any_stretch ? GripKind::Stretch : GripKind::Move;
    for (const Grip& g : grips) {
        if (g.kind == wanted && region.contains(g.pos)) out.push_back(g.index);
    }
}

}  // namespace

Step StretchCommand::apply(CommandContext& ctx, const Vec3& delta) {
    const std::vector<Handle> handles = ctx.selection.handles();

    // No crossing region: every defining point is "inside" the selection, so
    // this degenerates into MOVE. Saying so is the difference between the
    // command looking broken and the user knowing what they asked for.
    if (!ctx.selection.has_region()) {
        const Mat4 m = Mat4::translation(delta);
        std::size_t n = 0;
        for (const Handle h : handles) {
            const Entity* e = ctx.db.get(h);
            if (!e) continue;
            EntityPtr moved = e->clone();
            moved->transform(m);
            ctx.db.replace(h, std::move(moved));
            ++n;
        }
        return Step::done(std::to_string(n) + " moved (no crossing window: stretched as a move)");
    }

    const SelectionRegion& region = ctx.selection.region();
    std::vector<Grip> grips;
    std::vector<GripIndex> indices;

    std::size_t n = 0;
    for (const Handle h : handles) {
        const Entity* e = ctx.db.get(h);
        if (!e) continue;

        EntityPtr copy = e->clone();
        grips.clear();
        copy->grips(grips);
        eligible_grips(grips, region, indices);

        // Nothing of this entity fell inside: it was merely crossed, so it
        // stays exactly where it is.
        if (indices.empty()) continue;

        copy->stretch(delta, indices.data(), indices.size());
        ctx.db.replace(h, std::move(copy));
        ++n;
    }
    return Step::done(std::to_string(n) + " stretched");
}

Step StretchCommand::next(CommandContext& ctx, const InputValue& value) {
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
            p.allow_empty = true;
            p.base = base_;
            p.has_base = true;
            return Step::ask(p);
        }

        case State::Displacement: {
            if (value.kind == InputKind::None) return apply(ctx, base_);
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            return apply(ctx, value.point - base_);
        }
    }
    return Step::failed("internal state error");
}

// --- PLAN -------------------------------------------------------------------

Step PlanCommand::start(CommandContext&) {
    Prompt p;
    p.kind = PromptKind::String;
    p.message = "<Current UCS>/Ucs/World";
    p.keywords = {"Current", "Ucs", "World"};
    p.allow_empty = true;
    return Step::ask(p);
}

Step PlanCommand::next(CommandContext& ctx, const InputValue& value) {
    if (value.kind != InputKind::None && value.kind != InputKind::Keyword) {
        return Step::failed("answer Current, Ucs or World");
    }

    // No display: say so rather than reporting success for something that did
    // not happen. `ncad` is a real way to drive this program, not a degraded one.
    if (!ctx.view) return Step::failed("no view to change");

    // All three answers name world XY until UCS exists.
    ctx.view->set_plan_view(kWorldZ);
    return Step::done("Regenerating drawing.");
}

// --- ZOOM / PAN -------------------------------------------------------------

Step ZoomCommand::start(CommandContext&) {
    Prompt p;
    // R12's own wording. Dynamic, Center and Left are not implemented yet and
    // are deliberately absent from the list rather than offered and refused.
    p.kind = PromptKind::Distance;
    p.message = "All/Extents/Previous/Window/<Scale (X)>";
    p.keywords = {"All", "Extents", "Previous", "Window"};
    return Step::ask(p);
}

Step ZoomCommand::next(CommandContext& ctx, const InputValue& value) {
    if (!ctx.view) return Step::failed("no view to change");

    switch (state_) {
        case State::Option: {
            if (keyword_is(value, "EXTENTS") || keyword_is(value, "ALL")) {
                // All and Extents differ only once LIMITS exists: All shows the
                // limits or the extents, whichever is larger. Until then they
                // are the same view, and pretending otherwise would be
                // decoration.
                ctx.view->zoom_extents();
                return Step::done();
            }
            if (keyword_is(value, "PREVIOUS")) {
                if (!ctx.view->zoom_previous()) return Step::done("No previous view");
                return Step::done();
            }
            if (keyword_is(value, "WINDOW")) {
                state_ = State::WindowFirst;
                Prompt p;
                p.kind = PromptKind::Point;
                p.message = "First corner";
                return Step::ask(p);
            }

            // A bare number is a scale factor, as R12 has it.
            double factor = 0.0;
            if (!distance_from(value, Vec3{0, 0, 0}, factor)) {
                return Step::failed("expected a scale factor or an option");
            }
            if (factor <= 0.0) return Step::failed("scale must be positive");
            ctx.view->zoom_scale(factor);
            return Step::done();
        }

        case State::WindowFirst: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            first_ = value.point;
            state_ = State::WindowSecond;

            Prompt p;
            p.kind = PromptKind::Point;
            p.message = "Other corner";
            p.base = first_;
            p.has_base = true;
            return Step::ask(p);
        }

        case State::WindowSecond: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            ctx.view->zoom_window(first_, value.point);
            return Step::done();
        }
    }
    return Step::failed("internal state error");
}

Step PanCommand::start(CommandContext&) {
    Prompt p;
    p.kind = PromptKind::Point;
    p.message = "Displacement";
    return Step::ask(p);
}

Step PanCommand::next(CommandContext& ctx, const InputValue& value) {
    if (!ctx.view) return Step::failed("no view to change");
    if (value.kind != InputKind::Point) return Step::failed("a point is required");

    if (!have_first_) {
        first_ = value.point;
        have_first_ = true;

        Prompt p;
        p.kind = PromptKind::Point;
        p.message = "Second point";
        p.base = first_;
        p.has_base = true;
        return Step::ask(p);
    }

    // R12 pans by dragging the drawing, so the view moves the other way: the
    // point you grabbed ends up where you dropped it.
    ctx.view->pan(first_, value.point);
    return Step::done();
}

bool command_is_transparent(std::string_view name) {
    const std::string upper = upcase(name);
    // The test is whether it changes drawing state, not whether it is useful
    // mid-command. ERASE would be very useful mid-command and must never be
    // transparent, because the outer command may be holding handles.
    return upper == "ZOOM" || upper == "PAN" || upper == "PLAN" || upper == "REDRAW" ||
           upper == "ID" || upper == "DIST";
}

// --- inquiry: DIST, ID, AREA, LIST ------------------------------------------

namespace {

// R12 prints coordinates and distances to four places by default. Fixed rather
// than %g, because a column of numbers that switches to exponent form part way
// down is much harder to read back.
std::string fmt(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    return buf;
}

std::string fmt_point(const Vec3& p) {
    return "X = " + fmt(p.x) + "  Y = " + fmt(p.y) + "  Z = " + fmt(p.z);
}

std::string fmt_degrees(double radians) {
    return fmt(radians * 180.0 / std::numbers::pi);
}

Prompt ask_point(const char* message, const Vec3* base) {
    Prompt p;
    p.kind = PromptKind::Point;
    p.message = message;
    if (base) {
        p.base = *base;
        p.has_base = true;
    }
    return p;
}

}  // namespace

Step DistCommand::start(CommandContext&) { return Step::ask(ask_point("First point", nullptr)); }

Step DistCommand::next(CommandContext&, const InputValue& value) {
    if (value.kind != InputKind::Point) return Step::failed("a point is required");

    if (!have_first_) {
        first_ = value.point;
        have_first_ = true;
        return Step::ask(ask_point("Second point", &first_));
    }

    const Vec3 d = value.point - first_;
    const double planar = std::sqrt(d.x * d.x + d.y * d.y);

    // R12 reports both angles: in the XY plane, and up from it. The second is
    // the one that tells you a line is not flat when you thought it was.
    std::string out = "Distance = " + fmt(length(d));
    out += ",  Angle in X-Y Plane = " + fmt_degrees(std::atan2(d.y, d.x));
    out += ",  Angle from X-Y Plane = " + fmt_degrees(std::atan2(d.z, planar));
    out += "\nDelta X = " + fmt(d.x) + "   Delta Y = " + fmt(d.y) + "   Delta Z = " + fmt(d.z);
    return Step::done(out);
}

Step IdCommand::start(CommandContext&) { return Step::ask(ask_point("Point", nullptr)); }

Step IdCommand::next(CommandContext&, const InputValue& value) {
    if (value.kind != InputKind::Point) return Step::failed("a point is required");
    return Step::done(fmt_point(value.point));
}

Prompt AreaCommand::point_prompt() const {
    Prompt p;
    p.kind = PromptKind::Point;
    p.message = points_.empty() ? "First point" : "Next point";
    p.keywords = {"Entity"};
    p.allow_empty = !points_.empty();
    if (!points_.empty()) {
        p.base = points_.back();
        p.has_base = true;
    }
    return p;
}

Step AreaCommand::start(CommandContext&) { return Step::ask(point_prompt()); }

Step AreaCommand::next(CommandContext& ctx, const InputValue& value) {
    if (state_ == State::Entity) {
        if (value.kind != InputKind::Entity) return Step::failed("an entity is required");
        const Entity* e = ctx.db.get(value.entity);
        if (!e) return Step::failed("no such entity");

        // Only closed entities enclose an area. A line does not, and reporting
        // zero for one would look like an answer rather than a refusal.
        if (e->type() == EntityType::Circle) {
            const double r = static_cast<const Circle*>(e)->radius();
            const double area = std::numbers::pi * r * r;
            return Step::done("Area = " + fmt(area) +
                              ",  Circumference = " + fmt(2.0 * std::numbers::pi * r));
        }
        return Step::failed("that entity encloses no area");
    }

    if (keyword_is(value, "ENTITY")) {
        state_ = State::Entity;
        Prompt p;
        p.kind = PromptKind::Entity;
        p.message = "Select circle or closed entity";
        return Step::ask(p);
    }

    if (value.kind == InputKind::None) {
        if (points_.size() < 3) return Step::failed("at least three points are needed");

        // The shoelace formula, in the XY plane. A sequence of picked points is
        // implicitly closed, as R12 closes it.
        double twice = 0.0;
        double perimeter = 0.0;
        for (std::size_t i = 0; i < points_.size(); ++i) {
            const Vec3& a = points_[i];
            const Vec3& b = points_[(i + 1) % points_.size()];
            twice += a.x * b.y - b.x * a.y;
            perimeter += length(b - a);
        }
        return Step::done("Area = " + fmt(std::abs(twice) * 0.5) +
                          ",  Perimeter = " + fmt(perimeter));
    }

    if (value.kind != InputKind::Point) return Step::failed("a point is required");
    points_.push_back(value.point);
    state_ = State::Next;
    return Step::ask(point_prompt());
}

Step ListCommand::start(CommandContext& ctx) { return Step::ask(select_.prompt(ctx)); }

Step ListCommand::next(CommandContext& ctx, const InputValue& value) {
    switch (select_.feed(ctx, value)) {
        case SelectionPrompter::Result::Selecting:
            return Step::ask(select_.prompt(ctx));
        case SelectionPrompter::Result::Rejected:
            return Step::failed("an entity is required");
        case SelectionPrompter::Result::Finished:
            break;
    }
    if (ctx.selection.empty()) return Step::done("Nothing selected");

    std::string out;
    for (const Handle h : ctx.selection.handles()) {
        const Entity* e = ctx.db.get(h);
        if (!e) continue;

        if (!out.empty()) out += "\n";
        out += std::string(e->type_name()) + "  Handle = " + std::to_string(h);

        const LayerId layer = e->props().layer;
        if (layer < ctx.db.layers().size()) out += "  Layer: " + ctx.db.layer(layer).name;

        switch (e->type()) {
            case EntityType::Line: {
                const Line* l = static_cast<const Line*>(e);
                out += "\n  from  " + fmt_point(l->start());
                out += "\n  to    " + fmt_point(l->end());
                out += "\n  Length = " + fmt(l->length());
                break;
            }
            case EntityType::Circle: {
                const Circle* c = static_cast<const Circle*>(e);
                out += "\n  center  " + fmt_point(c->center());
                out += "\n  radius " + fmt(c->radius());
                out += "\n  circumference " + fmt(2.0 * std::numbers::pi * c->radius());
                out += "\n  area " + fmt(std::numbers::pi * c->radius() * c->radius());
                break;
            }
            case EntityType::Arc: {
                const Arc* a = static_cast<const Arc*>(e);
                out += "\n  center  " + fmt_point(a->center());
                out += "\n  radius " + fmt(a->radius());
                out += "\n  start angle " + fmt_degrees(a->start_angle());
                out += "\n  end angle " + fmt_degrees(a->end_angle());
                out += "\n  length " + fmt(a->radius() * a->sweep());
                break;
            }
            default:
                break;
        }
    }
    return Step::done(out);
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
    if (upper == "AREA") return std::make_unique<AreaCommand>();
    if (upper == "ARRAY") return std::make_unique<ArrayCommand>();
    if (upper == "DIST") return std::make_unique<DistCommand>();
    if (upper == "ID") return std::make_unique<IdCommand>();
    if (upper == "LIST") return std::make_unique<ListCommand>();
    if (upper == "PAN") return std::make_unique<PanCommand>();
    if (upper == "PLAN") return std::make_unique<PlanCommand>();
    if (upper == "ZOOM") return std::make_unique<ZoomCommand>();
    if (upper == "MOVE") return std::make_unique<MoveCommand>(false);
    if (upper == "ROTATE") return std::make_unique<TransformCommand>(TransformCommand::Kind::Rotate);
    if (upper == "SCALE") return std::make_unique<TransformCommand>(TransformCommand::Kind::Scale);
    if (upper == "STRETCH") return std::make_unique<StretchCommand>();
    if (upper == "MIRROR") return std::make_unique<TransformCommand>(TransformCommand::Kind::Mirror);
    if (upper == "COPY") return std::make_unique<MoveCommand>(true);
    if (upper == "UNDO") return std::make_unique<UndoCommand>();
    if (upper == "REDO") return std::make_unique<RedoCommand>();
    return nullptr;
}

const std::vector<std::string>& command_names() {
    static const std::vector<std::string> names = {
        "AREA", "ARRAY", "CIRCLE",  "COPY", "DIST",    "DXFOUT",  "ERASE", "ID",
        "LINE", "LIST",  "MIRROR",  "MOVE", "PAN",     "PLAN",    "REDO",   "ROTATE",
        "SCALE", "STRETCH", "UNDO", "ZOOM"};
    return names;
}

const std::vector<CommandAlias>& command_aliases() {
    // The R12 acad.pgp short forms for the commands that exist so far.
    static const std::vector<CommandAlias> aliases = {
        {"C", "CIRCLE"},
        {"E", "ERASE"},
        {"AA", "AREA"},
        {"AR", "ARRAY"},
        {"DI", "DIST"},
        {"P", "PAN"},
        {"Z", "ZOOM"},
        {"LI", "LIST"},
        {"CP", "COPY"},
        {"L", "LINE"},
        {"M", "MOVE"},
        {"MI", "MIRROR"},
        {"RO", "ROTATE"},
        {"S", "STRETCH"},
        {"SC", "SCALE"},
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
