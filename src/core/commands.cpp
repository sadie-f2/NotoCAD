// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/commands.hpp"

#include "noto/curve_edit.hpp"
#include "noto/intersect.hpp"
#include "noto/pick.hpp"
#include "noto/scene.hpp"

#include "noto/dxf.hpp"
#include "noto/dxf_read.hpp"
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
// Everything drawn takes the current layer, colour and linetype. One helper so
// a new command cannot forget one of the three, and so that adding a fourth
// current property later is a change in one place.
EntityPtr with_current_props(const Database& db, EntityPtr e) {
    if (!e) return e;
    // An extrusion is geometry, not a current setting, so it survives.
    const Vec3 normal = e->props().normal;
    e->props() = db.current_props();
    e->props().normal = normal;
    return e;
}

// R12 prints coordinates and distances to four places by default. Fixed rather
// than %g, because a column of numbers that switches to exponent form part way
// down is much harder to read back.
std::string fmt(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    return buf;
}

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

// The normal of the current construction plane. World Z until UCS exists; this
// is the single place that has to learn about UCS later.
Vec3 construction_normal() {
    return kWorldZ;
}

// Signed angle from `a` to `b` measured about `n`, in (-pi, pi]. The sign is
// what distinguishes a clockwise arc from a counterclockwise one, so every
// bulge in PLINE ultimately comes from here.
double signed_angle(const Vec3& a, const Vec3& b, const Vec3& n) {
    return std::atan2(dot(cross(a, b), n), dot(a, b));
}

// R12's group 42, from the arc's included angle. The quarter-angle tangent is
// the definition; see polyline.cpp for the arithmetic that reads it back.
double bulge_from_included(double included) {
    return std::tan(included * 0.25);
}

// Turns `v` by `radians` about `n`, for carrying an arc's tangent direction
// from one segment to the next.
Vec3 rotate_about(const Vec3& v, const Vec3& n, double radians) {
    return Mat4::rotation(Vec3{}, n, radians).transform_vector(v);
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
        ctx.db.add(with_current_props(ctx.db, std::make_unique<Line>(previous_, first_)));
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

    segments_.push_back(
        ctx.db.add(with_current_props(ctx.db, std::make_unique<Line>(previous_, value.point))));
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

            ctx.db.add(with_current_props(ctx.db, std::make_unique<Circle>(centre_, radius)));
            return Step::done();
        }
    }
    return Step::failed("internal state error");
}

// --- PLINE ------------------------------------------------------------------

Step PlineCommand::start(CommandContext&) {
    Prompt p;
    p.kind = PromptKind::Point;
    p.message = "From point";
    return Step::ask(p);
}

Prompt PlineCommand::line_prompt() const {
    Prompt p;
    p.kind = PromptKind::Point;
    p.message = "Endpoint of line";
    p.allow_empty = true;
    p.base = current();
    p.has_base = true;
    p.keywords = {"Arc", "Halfwidth", "Length", "Width"};
    // Close needs two segments to be worth anything, the same rule LINE uses.
    if (vertices_.size() >= 3) p.keywords.push_back("Close");
    if (vertices_.size() >= 2) p.keywords.push_back("Undo");
    return p;
}

Prompt PlineCommand::arc_prompt() const {
    Prompt p;
    p.kind = PromptKind::Point;
    p.message = "Endpoint of arc";
    p.allow_empty = true;
    p.base = current();
    p.has_base = true;
    p.keywords = {"Angle", "CEnter", "Direction", "Halfwidth",
                  "Line",  "Radius", "Second",    "Width"};
    if (vertices_.size() >= 3) p.keywords.push_back("CLose");
    if (vertices_.size() >= 2) p.keywords.push_back("Undo");
    return p;
}

Prompt PlineCommand::width_prompt(bool half, bool ending) const {
    Prompt p;
    p.kind = PromptKind::Distance;
    const char* what = half ? "half-width" : "width";
    p.message = std::string(ending ? "Ending " : "Starting ") + what;
    p.allow_empty = true;
    p.base = current();
    p.has_base = true;
    return p;
}

Step PlineCommand::resume() {
    state_ = arc_mode_ ? State::Arc : State::Line;
    return Step::ask(arc_mode_ ? arc_prompt() : line_prompt());
}

void PlineCommand::flush(CommandContext& ctx) {
    if (vertices_.size() < 2) return;

    auto poly = std::make_unique<Polyline>();
    poly->vertices() = vertices_;

    if (handle_ == kNullHandle) {
        handle_ = ctx.db.add(with_current_props(ctx.db, std::move(poly)));
        return;
    }
    // Replace rather than erase-and-add, so the polyline keeps its handle and
    // its place in the drawing order while it is being built.
    ctx.db.replace(handle_, with_current_props(ctx.db, std::move(poly)));
}

Step PlineCommand::add_vertex(CommandContext& ctx, const Vec3& p, double included, bool is_arc) {
    // The widths and the bulge belong to the segment LEAVING the current
    // vertex, so they are written onto the vertex already in the list.
    vertices_.back().bulge = is_arc ? bulge_from_included(included) : 0.0;
    vertices_.back().start_width = start_width_;
    vertices_.back().end_width = end_width_;

    const Vec3 from = current();
    vertices_.push_back(PolyVertex{p, 0.0, start_width_, end_width_});

    // The direction the next arc leaves in: along a straight segment, or the
    // turned tangent of an arc. This is what makes a run of arcs flow rather
    // than kink at every vertex.
    if (is_arc) {
        const Vec3 t = have_tangent_ ? tangent_ : normalize(p - from);
        tangent_ = normalize(rotate_about(t, construction_normal(), included));
    } else {
        const Vec3 d = p - from;
        if (!is_zero(d)) tangent_ = normalize(d);
    }
    have_tangent_ = true;

    flush(ctx);
    return resume();
}

Step PlineCommand::close_it(CommandContext& ctx, bool with_arc) {
    if (vertices_.size() < 3) return Step::failed("nothing to close");

    const Vec3 from = current();
    const Vec3 to = vertices_.front().pos;
    double included = 0.0;
    if (with_arc && have_tangent_) {
        const Vec3 chord = to - from;
        if (!is_zero(chord)) {
            included = 2.0 * signed_angle(tangent_, normalize(chord), construction_normal());
        }
    }
    vertices_.back().bulge = with_arc ? bulge_from_included(included) : 0.0;
    vertices_.back().start_width = start_width_;
    vertices_.back().end_width = end_width_;

    auto poly = std::make_unique<Polyline>();
    poly->vertices() = vertices_;
    poly->set_closed(true);
    if (handle_ == kNullHandle) {
        ctx.db.add(with_current_props(ctx.db, std::move(poly)));
    } else {
        ctx.db.replace(handle_, with_current_props(ctx.db, std::move(poly)));
    }
    return Step::done();
}

Step PlineCommand::undo_vertex(CommandContext& ctx) {
    if (vertices_.size() < 2) return Step::failed("nothing to undo");

    vertices_.pop_back();
    // The segment that led here no longer exists, so neither does its bulge.
    vertices_.back().bulge = 0.0;

    if (vertices_.size() < 2 && handle_ != kNullHandle) {
        // Back to a single point: there is no polyline left to be.
        ctx.db.erase(handle_);
        handle_ = kNullHandle;
    } else {
        flush(ctx);
    }

    have_tangent_ = false;
    if (vertices_.size() >= 2) {
        const Vec3 d = vertices_.back().pos - vertices_[vertices_.size() - 2].pos;
        if (!is_zero(d)) {
            tangent_ = normalize(d);
            have_tangent_ = true;
        }
    }
    return resume();
}

Step PlineCommand::next(CommandContext& ctx, const InputValue& value) {
    switch (state_) {
        case State::First: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            vertices_.push_back(PolyVertex{value.point, 0.0, start_width_, end_width_});
            state_ = State::Line;
            return Step::ask(line_prompt());
        }

        case State::Line:
        case State::Arc: {
            if (value.kind == InputKind::None) return Step::done();

            if (keyword_is(value, "ARC")) {
                arc_mode_ = true;
                return resume();
            }
            if (keyword_is(value, "LINE")) {
                arc_mode_ = false;
                return resume();
            }
            // "Close" and the arc mode's "CLose" upcase to the same word, which
            // is why one test serves both modes.
            if (keyword_is(value, "CLOSE")) return close_it(ctx, arc_mode_);
            if (keyword_is(value, "UNDO")) return undo_vertex(ctx);
            if (keyword_is(value, "WIDTH")) {
                state_ = State::WidthStart;
                return Step::ask(width_prompt(false, false));
            }
            if (keyword_is(value, "HALFWIDTH")) {
                state_ = State::HalfStart;
                return Step::ask(width_prompt(true, false));
            }
            if (keyword_is(value, "LENGTH")) {
                state_ = State::Length;
                Prompt p;
                p.kind = PromptKind::Distance;
                p.message = "Length of line";
                p.base = current();
                p.has_base = true;
                return Step::ask(p);
            }

            // The arc sub-mode's own options.
            if (arc_mode_) {
                if (keyword_is(value, "ANGLE")) {
                    state_ = State::ArcAngle;
                    Prompt p;
                    p.kind = PromptKind::Angle;
                    p.message = "Included angle";
                    p.base = current();
                    p.has_base = true;
                    return Step::ask(p);
                }
                if (keyword_is(value, "CENTER")) {
                    state_ = State::ArcCentre;
                    Prompt p;
                    p.kind = PromptKind::Point;
                    p.message = "Center point";
                    p.base = current();
                    p.has_base = true;
                    return Step::ask(p);
                }
                if (keyword_is(value, "RADIUS")) {
                    state_ = State::ArcRadius;
                    Prompt p;
                    p.kind = PromptKind::Distance;
                    p.message = "Radius";
                    p.base = current();
                    p.has_base = true;
                    return Step::ask(p);
                }
                if (keyword_is(value, "SECOND")) {
                    state_ = State::ArcSecond;
                    Prompt p;
                    p.kind = PromptKind::Point;
                    p.message = "Second point";
                    p.base = current();
                    p.has_base = true;
                    return Step::ask(p);
                }
                if (keyword_is(value, "DIRECTION")) {
                    state_ = State::ArcDirection;
                    Prompt p;
                    p.kind = PromptKind::Angle;
                    p.message = "Direction from starting point";
                    p.base = current();
                    p.has_base = true;
                    return Step::ask(p);
                }
            }

            if (value.kind != InputKind::Point) return Step::failed("a point is required");

            if (!arc_mode_) return add_vertex(ctx, value.point, 0.0, false);

            // A plain arc endpoint: the arc leaves along the current tangent
            // and ends where you pointed, which fixes the included angle at
            // twice the angle between the tangent and the chord.
            const Vec3 chord = value.point - current();
            if (is_zero(chord)) return Step::failed("zero-length arc");
            double included = 0.0;
            if (have_tangent_) {
                included = 2.0 * signed_angle(tangent_, normalize(chord), construction_normal());
            }
            // With no previous segment there is no tangent to continue, so the
            // arc degenerates to a straight segment rather than guessing.
            return add_vertex(ctx, value.point, included, have_tangent_);
        }

        case State::WidthStart:
        case State::HalfStart: {
            const bool half = state_ == State::HalfStart;
            double w = 0.0;
            if (value.kind == InputKind::None) {
                w = half ? start_width_ * 0.5 : start_width_;
            } else if (!distance_from(value, current(), w)) {
                return Step::failed("a width is required");
            }
            if (w < 0.0) return Step::failed("width must not be negative");
            start_width_ = half ? w * 2.0 : w;
            state_ = half ? State::HalfEnd : State::WidthEnd;
            return Step::ask(width_prompt(half, true));
        }

        case State::WidthEnd:
        case State::HalfEnd: {
            const bool half = state_ == State::HalfEnd;
            double w = 0.0;
            if (value.kind == InputKind::None) {
                // R12 offers the starting width as the default, which is what
                // makes a constant-width polyline two keystrokes.
                w = half ? start_width_ * 0.5 : start_width_;
            } else if (!distance_from(value, current(), w)) {
                return Step::failed("a width is required");
            }
            if (w < 0.0) return Step::failed("width must not be negative");
            end_width_ = half ? w * 2.0 : w;
            return resume();
        }

        case State::Length: {
            double len = 0.0;
            if (!signed_distance_from(value, current(), len)) {
                return Step::failed("a length is required");
            }
            if (!have_tangent_) return Step::failed("no direction to continue");
            return add_vertex(ctx, current() + tangent_ * len, 0.0, false);
        }

        case State::ArcAngle: {
            if (!angle_from(value, current(), pending_angle_)) {
                return Step::failed("an angle is required");
            }
            state_ = State::ArcAngleEnd;
            Prompt p;
            p.kind = PromptKind::Point;
            p.message = "Endpoint of arc";
            p.base = current();
            p.has_base = true;
            return Step::ask(p);
        }

        case State::ArcAngleEnd: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            if (is_zero(value.point - current())) return Step::failed("zero-length arc");
            // The angle was given outright, so it IS the included angle and no
            // tangent is consulted -- this is the option you reach for when the
            // continuation tangent is not what you want.
            return add_vertex(ctx, value.point, pending_angle_, true);
        }

        case State::ArcCentre: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            pending_centre_ = value.point;
            state_ = State::ArcCentreEnd;
            Prompt p;
            p.kind = PromptKind::Point;
            p.message = "Endpoint of arc";
            p.base = pending_centre_;
            p.has_base = true;
            return Step::ask(p);
        }

        case State::ArcCentreEnd: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            const Vec3 from = current() - pending_centre_;
            const Vec3 to = value.point - pending_centre_;
            if (is_zero(from) || is_zero(to)) return Step::failed("degenerate arc");
            // The shorter way round. R12 offers Angle and Length beside the
            // endpoint for when the major arc is wanted; the minor arc is what
            // an endpoint alone means.
            const double included =
                signed_angle(normalize(from), normalize(to), construction_normal());
            if (std::abs(included) < 1e-12) return Step::failed("degenerate arc");
            return add_vertex(ctx, value.point, included, true);
        }

        case State::ArcRadius: {
            if (!distance_from(value, current(), pending_radius_)) {
                return Step::failed("a radius is required");
            }
            if (pending_radius_ <= 0.0) return Step::failed("radius must be positive");
            state_ = State::ArcRadiusEnd;
            Prompt p;
            p.kind = PromptKind::Point;
            p.message = "Endpoint of arc";
            p.base = current();
            p.has_base = true;
            return Step::ask(p);
        }

        case State::ArcRadiusEnd: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            const Vec3 chord = value.point - current();
            const double chord_len = length(chord);
            if (chord_len < 1e-12) return Step::failed("zero-length arc");
            // |chord| = 2r sin(theta/2), so a chord longer than the diameter
            // has no arc of this radius at all.
            const double ratio = chord_len / (2.0 * pending_radius_);
            if (ratio > 1.0) return Step::failed("radius too small for that endpoint");
            double included = 2.0 * std::asin(ratio);
            // Which of the two ways round: follow the current tangent when
            // there is one, so a run of arcs keeps its sense.
            if (have_tangent_ &&
                signed_angle(tangent_, normalize(chord), construction_normal()) < 0.0) {
                included = -included;
            }
            return add_vertex(ctx, value.point, included, true);
        }

        case State::ArcSecond: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            pending_second_ = value.point;
            state_ = State::ArcSecondEnd;
            Prompt p;
            p.kind = PromptKind::Point;
            p.message = "Endpoint of arc";
            p.base = pending_second_;
            p.has_base = true;
            return Step::ask(p);
        }

        case State::ArcSecondEnd: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            // Three points on the arc. The included angle is the sum of the two
            // signed turns, which is what carries a major arc correctly: going
            // start -> second -> end the long way gives two same-signed turns
            // adding past pi, where the chord alone could not say so.
            const Vec3 a = pending_second_ - current();
            const Vec3 b = value.point - pending_second_;
            if (is_zero(a) || is_zero(b)) return Step::failed("degenerate arc");
            const Vec3 n = construction_normal();
            const double turn = signed_angle(normalize(a), normalize(b), n);
            if (std::abs(turn) < 1e-12) return Step::failed("three points are collinear");
            // The inscribed-angle relation: the turn between the two chords is
            // half the total included angle.
            return add_vertex(ctx, value.point, 2.0 * turn, true);
        }

        case State::ArcDirection: {
            double dir = 0.0;
            if (!angle_from(value, current(), dir)) return Step::failed("an angle is required");
            tangent_ = normalize(rotate_about(Vec3{1, 0, 0}, construction_normal(), dir));
            have_tangent_ = true;
            state_ = State::ArcDirectionEnd;
            Prompt p;
            p.kind = PromptKind::Point;
            p.message = "Endpoint of arc";
            p.base = current();
            p.has_base = true;
            return Step::ask(p);
        }

        case State::ArcDirectionEnd: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            const Vec3 chord = value.point - current();
            if (is_zero(chord)) return Step::failed("zero-length arc");
            const double included =
                2.0 * signed_angle(tangent_, normalize(chord), construction_normal());
            return add_vertex(ctx, value.point, included, true);
        }
    }
    return Step::failed("internal state error");
}

// --- POINT ------------------------------------------------------------------

Step PointCommand::start(CommandContext&) {
    Prompt p;
    p.kind = PromptKind::Point;
    p.message = "Point";
    return Step::ask(p);
}

Step PointCommand::next(CommandContext& ctx, const InputValue& value) {
    if (value.kind != InputKind::Point) return Step::failed("a point is required");
    ctx.db.add(with_current_props(ctx.db, std::make_unique<PointEntity>(value.point)));
    return Step::done();
}

// --- SOLID and 3DFACE -------------------------------------------------------

Prompt SolidCommand::ask(const char* message, bool allow_empty) const {
    Prompt p;
    p.kind = PromptKind::Point;
    p.message = message;
    p.allow_empty = allow_empty;
    return p;
}

void SolidCommand::emit(CommandContext& ctx) {
    auto e = std::make_unique<Face>(face3d_ ? EntityType::Face3d : EntityType::Solid);
    for (int i = 0; i < 4; ++i) e->set_corner(i, corner_[i]);
    ctx.db.add(with_current_props(ctx.db, std::move(e)));
    ++emitted_;
}

Step SolidCommand::start(CommandContext&) {
    return Step::ask(ask("First point", false));
}

Step SolidCommand::next(CommandContext& ctx, const InputValue& value) {
    switch (state_) {
        case State::First: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            corner_[0] = value.point;
            state_ = State::Second;
            return Step::ask(ask("Second point", false));
        }

        case State::Second: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            corner_[1] = value.point;
            state_ = State::Third;
            return Step::ask(ask("Third point", false));
        }

        case State::Third: {
            // Enter here ends a strip cleanly, which is how you stop after a
            // quadrilateral rather than being forced into a triangle.
            if (value.kind == InputKind::None) {
                return emitted_ == 0 ? Step::failed("nothing drawn") : Step::done();
            }
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            corner_[2] = value.point;
            state_ = State::Fourth;
            return Step::ask(ask("Fourth point", true));
        }

        case State::Fourth: {
            // Enter means a triangle, which the format spells as the fourth
            // corner repeating the third.
            corner_[3] = (value.kind == InputKind::None) ? corner_[2] : value.point;
            if (value.kind != InputKind::None && value.kind != InputKind::Point) {
                return Step::failed("a point is required");
            }
            emit(ctx);

            // The strip continues: this quadrilateral's far edge becomes the
            // next one's near edge.
            corner_[0] = corner_[2];
            corner_[1] = corner_[3];
            state_ = State::Third;
            return Step::ask(ask("Third point", true));
        }
    }
    return Step::failed("internal state error");
}

// --- TEXT -------------------------------------------------------------------

namespace {

// R12's justification keywords, and the pair of DXF codes each one means.
struct JustifyRow {
    const char* keyword;
    TextHAlign h;
    TextVAlign v;
};

constexpr JustifyRow kJustify[] = {
    {"ALIGN", TextHAlign::Aligned, TextVAlign::Baseline},
    {"FIT", TextHAlign::Fit, TextVAlign::Baseline},
    {"CENTER", TextHAlign::Center, TextVAlign::Baseline},
    {"MIDDLE", TextHAlign::Middle, TextVAlign::Baseline},
    {"RIGHT", TextHAlign::Right, TextVAlign::Baseline},
    {"TL", TextHAlign::Left, TextVAlign::Top},
    {"TC", TextHAlign::Center, TextVAlign::Top},
    {"TR", TextHAlign::Right, TextVAlign::Top},
    {"ML", TextHAlign::Left, TextVAlign::Middle},
    {"MC", TextHAlign::Center, TextVAlign::Middle},
    {"MR", TextHAlign::Right, TextVAlign::Middle},
    {"BL", TextHAlign::Left, TextVAlign::Bottom},
    {"BC", TextHAlign::Center, TextVAlign::Bottom},
    {"BR", TextHAlign::Right, TextVAlign::Bottom},
};

}  // namespace

Step TextCommand::start(CommandContext&) {
    Prompt p;
    p.kind = PromptKind::Point;
    p.message = "Start point of text";
    p.keywords = {"Justify"};
    return Step::ask(p);
}

Step TextCommand::ask_height() {
    state_ = State::Height;
    Prompt p;
    p.kind = PromptKind::Distance;
    p.message = "Height";
    p.allow_empty = true;
    p.base = start_;
    p.has_base = true;
    return Step::ask(p);
}

Step TextCommand::ask_rotation() {
    state_ = State::Rotation;
    Prompt p;
    p.kind = PromptKind::Angle;
    p.message = "Rotation angle";
    p.allow_empty = true;
    p.base = start_;
    p.has_base = true;
    return Step::ask(p);
}

Step TextCommand::ask_value() {
    state_ = State::Value;
    Prompt p;
    p.kind = PromptKind::String;
    p.message = "Text";
    p.allow_empty = true;
    return Step::ask(p);
}

Step TextCommand::build(CommandContext& ctx, const std::string& value) {
    if (value.empty()) return Step::done();  // R12 draws nothing for empty text

    auto text = std::make_unique<Text>(start_, value, height_);
    text->set_rotation(rotation_);
    text->set_align(h_align_, v_align_);
    if (text->is_justified()) text->set_align_point(start_);

    // Align and Fit both span two points, so the rotation comes from the pair
    // rather than from a typed angle. What differs is which property gives way
    // to make the text span the distance: Align scales the height, Fit squeezes
    // the width factor and leaves the height where it was put.
    if (h_align_ == TextHAlign::Aligned || h_align_ == TextHAlign::Fit) {
        const Vec3 span = second_ - start_;
        const double distance = length(span);
        if (distance < 1e-12) return Step::failed("the two points are the same");
        text->set_rotation(std::atan2(span.y, span.x));

        // The nominal width of the string at the current height and width
        // factor; both modes solve for whichever factor makes it equal the
        // distance. Without a font this is the placeholder metric, which makes
        // the result approximate in the same way the drawing already is.
        const double nominal = text->approximate_width();
        if (nominal < 1e-12) return Step::failed("cannot fit empty text");

        if (h_align_ == TextHAlign::Aligned) {
            text->set_height(height_ * distance / nominal);
        } else {
            text->set_width_factor(distance / nominal);
        }
    }

    ctx.db.add(with_current_props(ctx.db, std::move(text)));
    return Step::done();
}

Step TextCommand::next(CommandContext& ctx, const InputValue& value) {
    switch (state_) {
        case State::Start: {
            if (keyword_is(value, "JUSTIFY")) {
                state_ = State::JustifyOption;
                Prompt p;
                p.kind = PromptKind::String;
                p.message = "Align/Fit/Center/Middle/Right/TL/TC/TR/ML/MC/MR/BL/BC/BR";
                for (const JustifyRow& row : kJustify) p.keywords.push_back(row.keyword);
                return Step::ask(p);
            }
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            start_ = value.point;

            // Align and Fit span two points, so the second one is asked for
            // before anything else -- it is what the rotation, and one of the
            // height or the width factor, will be solved from.
            if (h_align_ == TextHAlign::Aligned || h_align_ == TextHAlign::Fit) {
                state_ = State::SecondPoint;
                Prompt p;
                p.kind = PromptKind::Point;
                p.message = "Second text line point";
                p.base = start_;
                p.has_base = true;
                return Step::ask(p);
            }
            return ask_height();
        }

        case State::JustifyOption: {
            const std::string word =
                (value.kind == InputKind::Keyword || value.kind == InputKind::String)
                    ? upcase(value.text)
                    : std::string();
            const JustifyRow* found = nullptr;
            for (const JustifyRow& row : kJustify) {
                if (word == row.keyword) found = &row;
            }
            if (!found) return Step::failed("unknown justification");
            h_align_ = found->h;
            v_align_ = found->v;

            state_ = State::Start;
            Prompt p;
            p.kind = PromptKind::Point;
            // R12 names the point after the justification, because for Align
            // and Fit it is one end of a span rather than an origin.
            p.message = (h_align_ == TextHAlign::Aligned || h_align_ == TextHAlign::Fit)
                            ? "First text line point"
                            : "Text point";
            return Step::ask(p);
        }

        case State::SecondPoint: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            second_ = value.point;
            // Align derives its height, so it never asks; Fit keeps the height
            // question and solves for width instead.
            return (h_align_ == TextHAlign::Aligned) ? ask_value() : ask_height();
        }

        case State::Height: {
            if (value.kind != InputKind::None) {
                double h = 0.0;
                if (!distance_from(value, start_, h)) return Step::failed("a height is required");
                if (h <= 0.0) return Step::failed("height must be positive");
                height_ = h;
            }
            // Align and Fit take their angle from the second point instead.
            if (h_align_ == TextHAlign::Aligned || h_align_ == TextHAlign::Fit) {
                return ask_value();
            }
            return ask_rotation();
        }

        case State::Rotation: {
            if (value.kind != InputKind::None) {
                if (!angle_from(value, start_, rotation_)) {
                    return Step::failed("an angle is required");
                }
            }
            return ask_value();
        }

        case State::Value: {
            if (value.kind == InputKind::None) return Step::done();
            if (value.kind != InputKind::String) return Step::failed("text is required");
            return build(ctx, value.text);
        }
    }
    return Step::failed("internal state error");
}

// --- BREAK ------------------------------------------------------------------

Step BreakCommand::start(CommandContext&) {
    Prompt p;
    p.kind = PromptKind::Entity;
    p.message = "Select object";
    return Step::ask(p);
}

Prompt BreakCommand::second_prompt() const {
    Prompt p;
    p.kind = PromptKind::Point;
    p.message = "Enter second point (or F for first point)";
    p.keywords = {"First"};
    p.base = first_;
    p.has_base = true;
    return p;
}

Step BreakCommand::apply(CommandContext& ctx, const Vec3& first, const Vec3& second,
                         bool single) {
    const Entity* e = ctx.db.get(target_);
    if (!e) return Step::failed("the object is gone");

    double t0 = 0.0;
    double t1 = 0.0;
    if (!curve_parameter_at(*e, first, &t0)) return Step::failed("that object cannot be broken");
    if (single) {
        t1 = t0;
    } else if (!curve_parameter_at(*e, second, &t1)) {
        return Step::failed("that object cannot be broken");
    }

    if (single && curve_is_closed(*e)) {
        // A single point cannot open a loop: there would be nothing to remove
        // and the curve would still be closed. R12 refuses too.
        return Step::failed("a closed object needs two break points");
    }

    std::vector<EntityPtr> pieces;
    break_curve(*e, t0, t1, pieces);

    if (pieces.empty()) {
        // The break spanned the whole curve. Erasing it is the honest result --
        // nothing of it survives -- and is what R12 does.
        ctx.db.erase(target_);
        return Step::done();
    }

    // The first piece takes the original's handle, so anything holding that
    // ename still refers to something recognisable. R12 keeps the first half
    // as the original object for the same reason.
    ctx.db.replace(target_, std::move(pieces[0]));
    for (std::size_t i = 1; i < pieces.size(); ++i) {
        ctx.db.add(std::move(pieces[i]));
    }
    return Step::done();
}

Step BreakCommand::next(CommandContext& ctx, const InputValue& value) {
    switch (state_) {
        case State::Select: {
            if (value.kind != InputKind::Entity) return Step::failed("select an object");
            const Entity* e = ctx.db.get(value.entity);
            if (!e) return Step::failed("no such entity");

            double probe = 0.0;
            if (!curve_parameter_at(*e, Vec3{}, &probe)) {
                return Step::failed("that object cannot be broken");
            }
            target_ = value.entity;

            // Pointing at the object is also the first break point, which is
            // R12's sequence and the reason the First option exists at all.
            // A typed handle or a LISP ename carries no location, so those go
            // straight to asking for the first point rather than silently
            // breaking at wherever the origin projects to.
            if (!value.has_point) {
                state_ = State::FirstAgain;
                Prompt p;
                p.kind = PromptKind::Point;
                p.message = "Enter first point";
                return Step::ask(p);
            }

            first_ = value.point;
            state_ = State::Second;
            return Step::ask(second_prompt());
        }

        case State::Second: {
            if (keyword_is(value, "FIRST")) {
                state_ = State::FirstAgain;
                Prompt p;
                p.kind = PromptKind::Point;
                p.message = "Enter first point";
                return Step::ask(p);
            }
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            return apply(ctx, first_, value.point, false);
        }

        case State::FirstAgain: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            first_ = value.point;
            state_ = State::SecondAfterFirst;

            Prompt p;
            p.kind = PromptKind::Point;
            p.message = "Enter second point (@ for none)";
            p.keywords = {"At"};
            p.base = first_;
            p.has_base = true;
            return Step::ask(p);
        }

        case State::SecondAfterFirst: {
            // R12's "@" means the second point equals the first: split the
            // curve in two without removing anything.
            if (keyword_is(value, "AT")) return apply(ctx, first_, first_, true);
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            if (near_equal(value.point, first_, 1e-12)) {
                return apply(ctx, first_, first_, true);
            }
            return apply(ctx, first_, value.point, false);
        }
    }
    return Step::failed("internal state error");
}

// --- BLOCK ------------------------------------------------------------------

Step BlockCommand::start(CommandContext&) {
    Prompt p;
    p.kind = PromptKind::String;
    p.message = "Block name";
    return Step::ask(p);
}

Step BlockCommand::next(CommandContext& ctx, const InputValue& value) {
    switch (state_) {
        case State::Name: {
            if (value.kind != InputKind::String || value.text.empty()) {
                return Step::failed("a block name is required");
            }
            block_name_ = upcase(value.text);
            state_ = State::Base;

            Prompt p;
            p.kind = PromptKind::Point;
            p.message = "Insertion base point";
            return Step::ask(p);
        }

        case State::Base: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            base_ = value.point;
            state_ = State::Selecting;
            return Step::ask(select_.prompt(ctx));
        }

        case State::Selecting: {
            const SelectionPrompter::Result r = select_.feed(ctx, value);
            if (r == SelectionPrompter::Result::Rejected) {
                return Step::failed("not a valid selection");
            }
            if (r == SelectionPrompter::Result::Selecting) {
                return Step::ask(select_.prompt(ctx));
            }

            if (ctx.selection.empty()) return Step::failed("nothing selected");

            // A block cannot be defined in terms of itself. Redefining one that
            // is currently inserted is fine and is the point of redefinition;
            // what is refused is a definition containing its own reference,
            // which would be a cycle rather than a nesting.
            const BlockId existing = ctx.db.find_block(block_name_);
            if (existing != kInvalidBlock) {
                for (Handle h : ctx.selection.handles()) {
                    const Entity* e = ctx.db.get(h);
                    if (!e || e->type() != EntityType::Insert) continue;
                    if (static_cast<const Insert&>(*e).definition() == ctx.db.block(existing)) {
                        return Step::failed("a block cannot contain itself");
                    }
                }
            }

            BlockDef def;
            def.name = block_name_;
            // The definition is stored around its own origin, so an insertion
            // is a plain placement rather than a placement plus a correction.
            def.base = Vec3{};

            const Mat4 to_definition = Mat4::translation(base_ * -1.0);
            for (Handle h : ctx.selection.handles()) {
                const Entity* e = ctx.db.get(h);
                if (!e) continue;
                EntityPtr copy = e->clone();
                copy->transform(to_definition);
                def.entities.push_back(std::move(copy));
            }

            const std::size_t count = def.entities.size();
            ctx.db.add_block(std::move(def));

            // R12 removes the originals: BLOCK is not "copy this into a
            // definition", it is "this geometry is now a definition". One UNDO
            // brings them back, since the whole command is a single group.
            for (Handle h : ctx.selection.handles()) ctx.db.erase(h);
            ctx.selection.clear();

            return Step::done(std::to_string(count) + " entities in block " + block_name_);
        }
    }
    return Step::failed("internal state error");
}

// --- INSERT and MINSERT -----------------------------------------------------

Step InsertCommand::start(CommandContext&) {
    Prompt p;
    p.kind = PromptKind::String;
    p.message = "Block name";
    return Step::ask(p);
}

Step InsertCommand::ask_rotation() {
    state_ = State::Rotation;
    Prompt p;
    p.kind = PromptKind::Angle;
    p.message = "Rotation angle";
    p.allow_empty = true;
    p.base = point_;
    p.has_base = true;
    return Step::ask(p);
}

Step InsertCommand::place(CommandContext& ctx) {
    const BlockDef* def = ctx.db.block(block_);
    if (!def) return Step::failed("no such block");

    InsertPlacement p;
    p.insertion = point_;
    p.scale = scale_;
    p.rotation = rotation_;
    p.normal = construction_normal();

    auto e = std::make_unique<Insert>(def, compose_placement(p, def->base));
    if (multiple_) e->set_array(rows_, columns_, row_spacing_, column_spacing_);

    ctx.db.add(with_current_props(ctx.db, std::move(e)));
    return Step::done();
}

Step InsertCommand::next(CommandContext& ctx, const InputValue& value) {
    switch (state_) {
        case State::Name: {
            if (value.kind != InputKind::String || value.text.empty()) {
                return Step::failed("a block name is required");
            }
            block_ = ctx.db.find_block(upcase(value.text));
            if (block_ == kInvalidBlock) return Step::failed("no such block");

            state_ = State::Point;
            Prompt p;
            p.kind = PromptKind::Point;
            p.message = "Insertion point";
            return Step::ask(p);
        }

        case State::Point: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            point_ = value.point;

            state_ = State::Scale;
            Prompt p;
            p.kind = PromptKind::Distance;
            p.message = "X scale factor <1>";
            p.allow_empty = true;
            p.keywords = {"Corner", "XYZ"};
            p.base = point_;
            p.has_base = true;
            return Step::ask(p);
        }

        case State::Scale: {
            if (value.kind == InputKind::None) return ask_rotation();

            if (keyword_is(value, "CORNER") || keyword_is(value, "XYZ")) {
                // Corner takes the scale from a rectangle; XYZ asks for all
                // three separately. Both end up asking for a second number, so
                // both land in the same state.
                state_ = State::YScale;
                Prompt p;
                p.kind = PromptKind::Distance;
                p.message = "Y scale factor";
                p.base = point_;
                p.has_base = true;
                return Step::ask(p);
            }

            double s = 0.0;
            if (!signed_distance_from(value, point_, s)) {
                return Step::failed("a scale factor is required");
            }
            if (s == 0.0) return Step::failed("scale cannot be zero");

            // R12's default: one number scales all three axes. The Y prompt
            // that follows offers "<default=X>" for exactly this reason.
            scale_ = {s, s, s};
            state_ = State::YScale;

            Prompt p;
            p.kind = PromptKind::Distance;
            p.message = "Y scale factor (default=X)";
            p.allow_empty = true;
            p.base = point_;
            p.has_base = true;
            return Step::ask(p);
        }

        case State::YScale: {
            if (value.kind != InputKind::None) {
                double s = 0.0;
                if (!signed_distance_from(value, point_, s)) {
                    return Step::failed("a scale factor is required");
                }
                if (s == 0.0) return Step::failed("scale cannot be zero");
                scale_.y = s;
            }
            return ask_rotation();
        }

        case State::Rotation: {
            if (value.kind != InputKind::None) {
                if (!angle_from(value, point_, rotation_)) {
                    return Step::failed("an angle is required");
                }
            }
            if (!multiple_) return place(ctx);

            state_ = State::Rows;
            Prompt p;
            p.kind = PromptKind::Integer;
            p.message = "Number of rows (---)";
            return Step::ask(p);
        }

        case State::Rows: {
            if (value.kind != InputKind::Integer || value.integer < 1) {
                return Step::failed("a positive count is required");
            }
            rows_ = static_cast<std::int16_t>(value.integer);

            state_ = State::Columns;
            Prompt p;
            p.kind = PromptKind::Integer;
            p.message = "Number of columns (|||)";
            return Step::ask(p);
        }

        case State::Columns: {
            if (value.kind != InputKind::Integer || value.integer < 1) {
                return Step::failed("a positive count is required");
            }
            columns_ = static_cast<std::int16_t>(value.integer);

            // A single row or column needs no spacing for that axis, which is
            // the one place R12 skips a question.
            if (rows_ == 1 && columns_ == 1) return place(ctx);

            state_ = State::RowSpacing;
            Prompt p;
            p.kind = PromptKind::Distance;
            p.message = "Unit cell or distance between rows (---)";
            p.base = point_;
            p.has_base = true;
            return Step::ask(p);
        }

        case State::RowSpacing: {
            if (!signed_distance_from(value, point_, row_spacing_)) {
                return Step::failed("a distance is required");
            }
            state_ = State::ColumnSpacing;
            Prompt p;
            p.kind = PromptKind::Distance;
            p.message = "Distance between columns (|||)";
            p.base = point_;
            p.has_base = true;
            return Step::ask(p);
        }

        case State::ColumnSpacing: {
            if (!signed_distance_from(value, point_, column_spacing_)) {
                return Step::failed("a distance is required");
            }
            return place(ctx);
        }
    }
    return Step::failed("internal state error");
}

// --- EXPLODE ----------------------------------------------------------------

Step ExplodeCommand::start(CommandContext& ctx) { return Step::ask(select_.prompt(ctx)); }

Step ExplodeCommand::next(CommandContext& ctx, const InputValue& value) {
    const SelectionPrompter::Result r = select_.feed(ctx, value);
    if (r == SelectionPrompter::Result::Rejected) return Step::failed("not a valid selection");
    if (r == SelectionPrompter::Result::Selecting) return Step::ask(select_.prompt(ctx));

    if (ctx.selection.empty()) return Step::failed("nothing selected");

    std::size_t exploded = 0;
    std::size_t skipped = 0;

    for (Handle h : ctx.selection.handles()) {
        const Entity* e = ctx.db.get(h);
        if (!e) continue;
        if (e->type() != EntityType::Insert) {
            // R12 also explodes polylines into lines and arcs, and dimensions
            // into their parts. Not built: polyline explosion is the inverse of
            // PEDIT Join and belongs beside it, and dimensions do not exist.
            ++skipped;
            continue;
        }

        const Insert& ins = static_cast<const Insert&>(*e);
        const BlockDef* def = ins.definition();
        if (!def) {
            ++skipped;
            continue;
        }

        // One level only, as R12 does: a nested reference comes out as a
        // reference, not as its contents. Taking an assembly apart a layer at a
        // time is the point rather than a limitation.
        const EntityProps inherited = ins.props();
        for (std::int16_t row = 0; row < ins.rows(); ++row) {
            for (std::int16_t col = 0; col < ins.columns(); ++col) {
                const Mat4 m = ins.placement_for(row, col);
                for (const EntityPtr& child : def->entities) {
                    if (!child) continue;
                    EntityPtr copy = child->clone();
                    copy->transform(m);
                    // BYBLOCK resolves against the reference that is going
                    // away, so anything carrying it inherits the reference's
                    // own properties rather than becoming BYLAYER by accident.
                    if (copy->props().color == kColorByBlock) {
                        copy->props().color = inherited.color;
                    }
                    ctx.db.add(std::move(copy));
                }
            }
        }

        ctx.db.erase(h);
        ++exploded;
    }

    ctx.selection.clear();
    if (exploded == 0) return Step::failed("nothing that can be exploded was selected");

    std::string msg = std::to_string(exploded) + " exploded";
    if (skipped > 0) msg += ", " + std::to_string(skipped) + " skipped";
    return Step::done(msg);
}

// --- WBLOCK -----------------------------------------------------------------

Step WblockCommand::start(CommandContext&) {
    Prompt p;
    p.kind = PromptKind::String;
    p.message = "File name";
    return Step::ask(p);
}

Step WblockCommand::next(CommandContext& ctx, const InputValue& value) {
    switch (state_) {
        case State::File: {
            if (value.kind != InputKind::String || value.text.empty()) {
                return Step::failed("a file name is required");
            }
            path_ = value.text;
            if (path_.size() < 4 || upcase(path_.substr(path_.size() - 4)) != ".DXF") {
                path_ += ".dxf";
            }

            state_ = State::Block;
            Prompt p;
            p.kind = PromptKind::String;
            p.message = "Block name (* for the whole drawing)";
            return Step::ask(p);
        }

        case State::Block: {
            if (value.kind != InputKind::String || value.text.empty()) {
                return Step::failed("a block name is required");
            }

            if (value.text == "*") {
                if (!write_dxf_file(ctx.db, path_)) return Step::failed("cannot write " + path_);
                return Step::done(path_ + " written");
            }

            const BlockId id = ctx.db.find_block(upcase(value.text));
            const BlockDef* def = ctx.db.block(id);
            if (!def) return Step::failed("no such block");

            // A fresh database holding the definition's contents as ordinary
            // entities, which is what a written block is: the file is a drawing
            // whose geometry happens to have come from one.
            Database out;
            for (const EntityPtr& e : def->entities) {
                if (e) out.add(e->clone());
            }
            out.sysvars().set("INSBASE", SysvarValue::of_point(def->base));

            if (!write_dxf_file(out, path_)) return Step::failed("cannot write " + path_);
            return Step::done(path_ + " written");
        }
    }
    return Step::failed("internal state error");
}

// --- BASE -------------------------------------------------------------------

Step BaseCommand::start(CommandContext& ctx) {
    Prompt p;
    p.kind = PromptKind::Point;
    p.message = "Base point";
    p.allow_empty = true;
    p.base = ctx.db.sysvars().get_point(Sysvar::InsBase);
    p.has_base = true;
    return Step::ask(p);
}

Step BaseCommand::next(CommandContext& ctx, const InputValue& value) {
    if (value.kind == InputKind::None) return Step::done();
    if (value.kind != InputKind::Point) return Step::failed("a point is required");

    ctx.db.sysvars().set("INSBASE", SysvarValue::of_point(value.point));
    return Step::done();
}

// --- PEDIT ------------------------------------------------------------------

namespace {

// How close two endpoints must be to count as the same point. R12 joins only
// on exact coincidence by default, so this is a floating-point tolerance rather
// than a fuzz distance -- picking with an object snap lands exactly, and
// anything looser would join things the user did not mean.
constexpr double kJoinTol = 1e-9;

// The vertex chain an entity contributes to a polyline it is joined to, or
// empty when it cannot be joined at all.
//
// The bulge convention is the polyline's, not the entity's: an arc traversed
// from its start point to its end point sweeps counterclockwise in its OWN
// plane, which is the opposite sense when that plane faces the other way.
std::vector<PolyVertex> join_chain(const Entity& e, const Vec3& poly_normal) {
    std::vector<PolyVertex> chain;
    switch (e.type()) {
        case EntityType::Line: {
            const Line& l = static_cast<const Line&>(e);
            chain.push_back(PolyVertex{l.start(), 0.0, 0.0, 0.0});
            chain.push_back(PolyVertex{l.end(), 0.0, 0.0, 0.0});
            break;
        }
        case EntityType::Arc: {
            const Arc& a = static_cast<const Arc&>(e);
            double bulge = std::tan(a.sweep() * 0.25);
            if (dot(a.props().normal, poly_normal) < 0.0) bulge = -bulge;
            chain.push_back(PolyVertex{a.start_point(), bulge, 0.0, 0.0});
            chain.push_back(PolyVertex{a.end_point(), 0.0, 0.0, 0.0});
            break;
        }
        case EntityType::Polyline: {
            const Polyline& p = static_cast<const Polyline&>(e);
            // A closed polyline has no free ends, so there is nothing to join to.
            if (p.closed() || p.size() < 2) break;
            chain = p.vertices();
            break;
        }
        default: break;
    }
    return chain;
}

// Reverses a chain's direction of travel. The bulges move as well as negate:
// a bulge describes the segment LEAVING its vertex, so reversing hands each
// one to the vertex at the other end of its own segment.
void reverse_chain(std::vector<PolyVertex>& chain) {
    if (chain.size() < 2) return;
    std::vector<PolyVertex> out;
    out.reserve(chain.size());
    for (std::size_t i = chain.size(); i-- > 0;) {
        PolyVertex v = chain[i];
        v.bulge = (i == 0) ? 0.0 : -chain[i - 1].bulge;
        // The widths belong to the same segment as the bulge and move with it.
        if (i > 0) {
            v.start_width = chain[i - 1].end_width;
            v.end_width = chain[i - 1].start_width;
        } else {
            v.start_width = 0.0;
            v.end_width = 0.0;
        }
        out.push_back(v);
    }
    chain.swap(out);
}

}  // namespace

Polyline* PeditCommand::target(CommandContext& ctx) const {
    Entity* e = ctx.db.get(handle_);
    if (!e || e->type() != EntityType::Polyline) return nullptr;
    return static_cast<Polyline*>(e);
}

void PeditCommand::push_snapshot(CommandContext& ctx) {
    const Entity* e = ctx.db.get(handle_);
    if (e) snapshots_.push_back(e->clone());
}

Step PeditCommand::do_undo(CommandContext& ctx) {
    if (snapshots_.empty()) return Step::failed("nothing to undo");
    ctx.db.replace(handle_, std::move(snapshots_.back()));
    snapshots_.pop_back();
    note_.clear();
    return ask_option(ctx);
}

Step PeditCommand::ask_option(CommandContext& ctx) {
    state_ = State::Option;
    const Polyline* p = target(ctx);
    if (!p) return Step::failed("the polyline is gone");

    Prompt prompt;
    prompt.kind = PromptKind::String;
    prompt.message = note_.empty() ? "Enter an option" : note_;
    prompt.allow_empty = true;  // Enter is eXit
    // R12 shows Open when the polyline is already closed, which is the same
    // option reading the other way round rather than a second one.
    prompt.keywords = {p->closed() ? "Open" : "Close", "Join", "Width", "Undo", "eXit"};
    note_.clear();
    return Step::ask(prompt);
}

Step PeditCommand::do_join(CommandContext& ctx) {
    Polyline* poly = target(ctx);
    if (!poly) return Step::failed("the polyline is gone");
    if (poly->closed()) return Step::failed("a closed polyline has no ends to join to");

    // Work on a copy and write it back once, so the database sees one change
    // and undo records one before/after pair.
    std::vector<PolyVertex> verts = poly->vertices();
    const Vec3 normal = poly->props().normal;

    std::vector<Handle> candidates;
    for (Handle h : ctx.selection.handles()) {
        if (h != handle_) candidates.push_back(h);
    }

    std::vector<bool> used(candidates.size(), false);
    std::size_t added = 0;

    // Repeated passes, because joining one entity can expose an endpoint that
    // lets an earlier candidate join too. R12 does the same, which is what
    // makes joining a scattered set of segments work in one go.
    bool progress = true;
    while (progress) {
        progress = false;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (used[i]) continue;
            const Entity* e = ctx.db.get(candidates[i]);
            if (!e) continue;

            std::vector<PolyVertex> chain = join_chain(*e, normal);
            if (chain.size() < 2) continue;

            const Vec3& head = verts.front().pos;
            const Vec3& tail = verts.back().pos;

            bool attached = false;
            if (near_equal(chain.front().pos, tail, kJoinTol)) {
                // The tail's bulge belongs to the segment it already has; the
                // incoming chain's first bulge takes over from the join point.
                verts.back().bulge = chain.front().bulge;
                verts.back().start_width = chain.front().start_width;
                verts.back().end_width = chain.front().end_width;
                verts.insert(verts.end(), chain.begin() + 1, chain.end());
                attached = true;
            } else if (near_equal(chain.back().pos, tail, kJoinTol)) {
                reverse_chain(chain);
                verts.back().bulge = chain.front().bulge;
                verts.back().start_width = chain.front().start_width;
                verts.back().end_width = chain.front().end_width;
                verts.insert(verts.end(), chain.begin() + 1, chain.end());
                attached = true;
            } else if (near_equal(chain.back().pos, head, kJoinTol)) {
                verts.insert(verts.begin(), chain.begin(), chain.end() - 1);
                attached = true;
            } else if (near_equal(chain.front().pos, head, kJoinTol)) {
                reverse_chain(chain);
                verts.insert(verts.begin(), chain.begin(), chain.end() - 1);
                attached = true;
            }

            if (attached) {
                used[i] = true;
                ++added;
                progress = true;
                ctx.db.erase(candidates[i]);
            }
        }
    }

    if (added == 0) {
        note_ = "0 segments added to polyline";
        return ask_option(ctx);
    }

    push_snapshot(ctx);
    auto merged = std::make_unique<Polyline>();
    merged->vertices() = verts;
    merged->props() = poly->props();
    ctx.db.replace(handle_, std::move(merged));

    note_ = std::to_string(added) + " segments added to polyline";
    return ask_option(ctx);
}

Step PeditCommand::start(CommandContext&) {
    Prompt p;
    p.kind = PromptKind::Entity;
    p.message = "Select polyline";
    return Step::ask(p);
}

Step PeditCommand::next(CommandContext& ctx, const InputValue& value) {
    switch (state_) {
        case State::Select: {
            if (value.kind != InputKind::Entity) return Step::failed("select a polyline");
            const Entity* e = ctx.db.get(value.entity);
            if (!e) return Step::failed("no such entity");
            if (e->type() != EntityType::Polyline) {
                // R12 offers to turn a line or arc into a one-segment polyline
                // here. Not built: it is a creation path rather than an edit,
                // and it wants deciding alongside the curve options.
                return Step::failed("that is not a polyline");
            }
            handle_ = value.entity;
            return ask_option(ctx);
        }

        case State::Option: {
            if (value.kind == InputKind::None || keyword_is(value, "EXIT")) return Step::done();

            if (keyword_is(value, "CLOSE") || keyword_is(value, "OPEN")) {
                Polyline* p = target(ctx);
                if (!p) return Step::failed("the polyline is gone");
                if (!p->closed() && p->size() < 3) {
                    return Step::failed("a polyline needs three vertices to close");
                }
                push_snapshot(ctx);
                auto edited = static_cast<Polyline*>(p->clone().release());
                edited->set_closed(!p->closed());
                ctx.db.replace(handle_, EntityPtr(edited));
                return ask_option(ctx);
            }

            if (keyword_is(value, "WIDTH")) {
                state_ = State::WidthValue;
                Prompt p;
                p.kind = PromptKind::Distance;
                p.message = "Enter new width for all segments";
                return Step::ask(p);
            }

            if (keyword_is(value, "JOIN")) {
                state_ = State::JoinSelect;
                ctx.selection.clear();
                return Step::ask(select_.prompt(ctx));
            }

            if (keyword_is(value, "UNDO")) return do_undo(ctx);

            return Step::failed("unknown option");
        }

        case State::WidthValue: {
            double w = 0.0;
            if (!distance_from(value, Vec3{}, w)) return Step::failed("a width is required");
            if (w < 0.0) return Step::failed("width must not be negative");

            Polyline* p = target(ctx);
            if (!p) return Step::failed("the polyline is gone");
            push_snapshot(ctx);
            auto edited = static_cast<Polyline*>(p->clone().release());
            edited->set_uniform_width(w);
            ctx.db.replace(handle_, EntityPtr(edited));
            return ask_option(ctx);
        }

        case State::JoinSelect: {
            const SelectionPrompter::Result r = select_.feed(ctx, value);
            if (r == SelectionPrompter::Result::Rejected) {
                return Step::failed("not a valid selection");
            }
            if (r == SelectionPrompter::Result::Selecting) {
                return Step::ask(select_.prompt(ctx));
            }
            return do_join(ctx);
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

// --- ROTATE3D ---------------------------------------------------------------

namespace {

// The axis an entity defines: a line is its own direction, a circle or arc is
// the axis it turns about. Nothing else here has an axis worth the name.
bool entity_axis(const Entity& e, Vec3& origin, Vec3& direction) {
    switch (e.type()) {
        case EntityType::Line: {
            const Line& l = static_cast<const Line&>(e);
            origin = l.start();
            direction = l.direction();
            return !is_zero(direction);
        }
        case EntityType::Circle: {
            const Circle& c = static_cast<const Circle&>(e);
            origin = c.center();
            direction = c.props().normal;
            return true;
        }
        case EntityType::Arc: {
            const Arc& a = static_cast<const Arc&>(e);
            origin = a.center();
            direction = a.props().normal;
            return true;
        }
        default:
            return false;
    }
}

}  // namespace

Step Rotate3dCommand::start(CommandContext& ctx) { return Step::ask(select_.prompt(ctx)); }

Step Rotate3dCommand::ask_angle() {
    state_ = State::Angle;
    Prompt p;
    p.kind = PromptKind::Angle;
    p.message = "Rotation angle";
    p.base = origin_;
    p.has_base = true;
    return Step::ask(p);
}

Step Rotate3dCommand::apply(CommandContext& ctx, double radians) {
    if (is_zero(direction_)) return Step::failed("the axis has no direction");

    const Mat4 m = Mat4::rotation(origin_, normalize(direction_), radians);
    const std::vector<Handle> handles = ctx.selection.handles();

    std::size_t n = 0;
    for (const Handle h : handles) {
        const Entity* e = ctx.db.get(h);
        if (!e) continue;
        EntityPtr moved = e->clone();
        moved->transform(m);
        // In place, so the handle survives an AutoLISP ename.
        ctx.db.replace(h, std::move(moved));
        ++n;
    }
    return Step::done(std::to_string(n) + " rotated");
}

Step Rotate3dCommand::next(CommandContext& ctx, const InputValue& value) {
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

            state_ = State::AxisOption;
            Prompt p;
            p.kind = PromptKind::Point;
            p.message = "Axis by Entity/View/Xaxis/Yaxis/Zaxis/<2points>";
            p.keywords = {"Entity", "View", "Xaxis", "Yaxis", "Zaxis"};
            return Step::ask(p);
        }

        case State::AxisOption: {
            if (keyword_is(value, "ENTITY")) {
                state_ = State::AxisEntity;
                Prompt p;
                p.kind = PromptKind::Entity;
                p.message = "Pick a line, circle or arc";
                return Step::ask(p);
            }
            if (keyword_is(value, "XAXIS") || keyword_is(value, "YAXIS") ||
                keyword_is(value, "ZAXIS")) {
                named_axis_ = keyword_is(value, "XAXIS")   ? Vec3{1, 0, 0}
                              : keyword_is(value, "YAXIS") ? Vec3{0, 1, 0}
                                                           : Vec3{0, 0, 1};
                state_ = State::PointOnAxis;
                Prompt p;
                p.kind = PromptKind::Point;
                p.message = "Point on axis";
                return Step::ask(p);
            }
            if (keyword_is(value, "VIEW")) {
                if (!ctx.view) return Step::failed("no view to take an axis from");
                // Straight into the screen, so the rotation looks like a plain
                // spin from where you are standing.
                named_axis_ = ctx.view->view_basis().az;
                state_ = State::PointOnAxis;
                Prompt p;
                p.kind = PromptKind::Point;
                p.message = "Point on axis";
                return Step::ask(p);
            }

            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            origin_ = value.point;
            state_ = State::SecondPoint;

            Prompt p;
            p.kind = PromptKind::Point;
            p.message = "Second point on axis";
            p.base = origin_;
            p.has_base = true;
            return Step::ask(p);
        }

        case State::SecondPoint: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            direction_ = value.point - origin_;
            if (is_zero(direction_)) return Step::failed("the two points are the same");
            return ask_angle();
        }

        case State::PointOnAxis: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");
            origin_ = value.point;
            direction_ = named_axis_;
            return ask_angle();
        }

        case State::AxisEntity: {
            if (value.kind != InputKind::Entity) return Step::failed("an entity is required");
            const Entity* e = ctx.db.get(value.entity);
            if (!e) return Step::failed("no such entity");
            if (!entity_axis(*e, origin_, direction_)) {
                return Step::failed("that entity defines no axis");
            }
            return ask_angle();
        }

        case State::Angle: {
            double radians = 0.0;
            if (!angle_from(value, origin_, radians)) return Step::failed("an angle is required");
            return apply(ctx, radians);
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

// --- LAYER ------------------------------------------------------------------

namespace {

// R12 takes comma-separated names and wildcards at these prompts. Only the
// comma-separated part is here; wildcards want a matcher that PURGE will want
// too, so they wait until there is a second caller.
std::vector<std::string> split_names(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (c == ',' || c == ' ') {
            if (!current.empty()) out.push_back(upcase(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) out.push_back(upcase(current));
    return out;
}

}  // namespace

Step LayerCommand::ask_option(CommandContext&) {
    state_ = State::Option;
    Prompt p;
    p.kind = PromptKind::String;
    p.message = "?/Make/Set/New/ON/OFF/Color/Ltype/Freeze/Thaw";
    p.keywords = {"?",      "Make", "Set",  "New",    "ON",
                  "OFF",    "Color", "Ltype", "Freeze", "Thaw"};
    p.allow_empty = true;  // Enter leaves
    return Step::ask(p);
}

Step LayerCommand::ask_name(State next_state, const char* message) {
    state_ = next_state;
    Prompt p;
    p.kind = PromptKind::String;
    p.message = message;
    return Step::ask(p);
}

Step LayerCommand::apply_to_names(CommandContext& ctx, const std::string& text) {
    const std::vector<std::string> names = split_names(text);
    if (names.empty()) return ask_option(ctx);

    for (const std::string& name : names) {
        // Make and New create; everything else works on what exists, and says
        // so rather than creating a layer as a side effect of trying to freeze
        // one whose name was mistyped.
        const bool creating = (state_ == State::NameForMake || state_ == State::NameForNew);
        LayerId id = ctx.db.find_layer(name);
        if (id == kInvalidLayer) {
            if (!creating) {
                report_ = "Layer " + name + " not found";
                continue;
            }
            id = ctx.db.add_layer(name);
        }

        switch (state_) {
            case State::NameForMake:
            case State::NameForSet:
                // Set refuses a frozen layer: R12 will not make you draw onto
                // something you cannot see.
                if (ctx.db.layer(id).frozen) {
                    report_ = "Layer " + name + " is frozen";
                    break;
                }
                ctx.db.sysvars().set_string(Sysvar::CLayer, ctx.db.layer(id).name);
                break;

            case State::NameForNew:
                break;  // created above and nothing more

            case State::NameForOn:
                ctx.db.set_layer_color(id, ctx.db.layer(id).visible_color());
                break;
            case State::NameForOff:
                // Off is a negative colour in R12, so the colour survives being
                // turned off and comes back when it is turned on.
                ctx.db.set_layer_color(id, static_cast<std::int16_t>(-ctx.db.layer(id).visible_color()));
                break;

            case State::NameForFreeze:
                if (id == ctx.db.current_layer()) {
                    report_ = "Cannot freeze the current layer";
                    break;
                }
                ctx.db.set_layer_frozen(id, true);
                break;
            case State::NameForThaw:
                ctx.db.set_layer_frozen(id, false);
                break;

            case State::NameForColor: {
                // Setting a colour on a layer that is off keeps it off, or
                // changing a colour would silently turn layers back on.
                const bool was_off = ctx.db.layer(id).off();
                ctx.db.set_layer_color(id, was_off ? static_cast<std::int16_t>(-pending_color_)
                                                   : pending_color_);
                break;
            }

            case State::NameForLtype: {
                const LinetypeId lt = ctx.db.find_linetype(pending_ltype_);
                if (lt == kInvalidLinetype) {
                    report_ = "Linetype " + pending_ltype_ + " not loaded";
                    break;
                }
                ctx.db.set_layer_linetype(id, lt);
                break;
            }

            default:
                break;
        }
    }
    return ask_option(ctx);
}

Step LayerCommand::start(CommandContext& ctx) { return ask_option(ctx); }

Step LayerCommand::next(CommandContext& ctx, const InputValue& value) {
    switch (state_) {
        case State::Option: {
            if (value.kind == InputKind::None) {
                return Step::done(report_);
            }
            if (keyword_is(value, "?")) {
                std::string list;
                for (const Layer& l : ctx.db.layers()) {
                    if (!list.empty()) list += "\n";
                    list += l.name;
                    list += l.off() ? "  Off" : "  On";
                    if (l.frozen) list += "  Frozen";
                    if (l.locked) list += "  Locked";
                    list += "  Color " + std::to_string(l.visible_color());
                    if (l.linetype < ctx.db.linetypes().size()) {
                        list += "  " + ctx.db.linetype(l.linetype).name;
                    }
                }
                report_ = list;
                return ask_option(ctx);
            }
            if (keyword_is(value, "MAKE")) return ask_name(State::NameForMake, "New current layer");
            if (keyword_is(value, "SET")) return ask_name(State::NameForSet, "New current layer");
            if (keyword_is(value, "NEW")) return ask_name(State::NameForNew, "New layer name(s)");
            if (keyword_is(value, "ON")) return ask_name(State::NameForOn, "Layer name(s) to turn On");
            if (keyword_is(value, "OFF")) {
                return ask_name(State::NameForOff, "Layer name(s) to turn Off");
            }
            if (keyword_is(value, "FREEZE")) {
                return ask_name(State::NameForFreeze, "Layer name(s) to Freeze");
            }
            if (keyword_is(value, "THAW")) {
                return ask_name(State::NameForThaw, "Layer name(s) to Thaw");
            }
            if (keyword_is(value, "COLOR")) {
                state_ = State::ColorValue;
                Prompt p;
                p.kind = PromptKind::Integer;
                p.message = "Color";
                return Step::ask(p);
            }
            if (keyword_is(value, "LTYPE")) {
                state_ = State::LtypeValue;
                Prompt p;
                p.kind = PromptKind::String;
                p.message = "Linetype";
                return Step::ask(p);
            }
            return Step::failed("unknown option");
        }

        case State::ColorValue: {
            if (value.kind != InputKind::Integer) return Step::failed("a colour number is required");
            if (value.integer < 1 || value.integer > 255) {
                return Step::failed("colour must be between 1 and 255");
            }
            pending_color_ = static_cast<std::int16_t>(value.integer);
            return ask_name(State::NameForColor, "Layer name(s) for color");
        }

        case State::LtypeValue: {
            if (value.kind != InputKind::String) return Step::failed("a linetype name is required");
            pending_ltype_ = upcase(value.text);
            return ask_name(State::NameForLtype, "Layer name(s) for linetype");
        }

        default: {
            if (value.kind != InputKind::String) return Step::failed("a layer name is required");
            return apply_to_names(ctx, value.text);
        }
    }
}

// --- LTYPE ------------------------------------------------------------------

bool builtin_linetype(std::string_view name, std::string& description,
                      std::vector<double>& pattern) {
    // Straight out of R12's acad.lin. Positive is a dash, negative a gap, zero
    // a dot -- the same convention the file uses and that Linetype stores.
    struct Def {
        const char* name;
        const char* description;
        std::initializer_list<double> pattern;
    };
    static const Def kDefs[] = {
        {"CONTINUOUS", "Solid line", {}},
        {"DASHED", "Dashed __ __ __ __ __ __ __ __ __ __", {0.5, -0.25}},
        {"HIDDEN", "Hidden __ __ __ __ __ __ __ __ __ __", {0.25, -0.125}},
        {"CENTER", "Center ____ _ ____ _ ____ _ ____", {1.25, -0.25, 0.25, -0.25}},
        {"PHANTOM", "Phantom ____ _ _ ____ _ _ ____", {1.25, -0.25, 0.25, -0.25, 0.25, -0.25}},
        {"DOT", "Dot . . . . . . . . . . . . . . . .", {0.0, -0.25}},
        {"DASHDOT", "Dash dot __ . __ . __ . __ . __", {0.5, -0.25, 0.0, -0.25}},
        {"DIVIDE", "Divide __ . . __ . . __ . . __", {0.5, -0.25, 0.0, -0.25, 0.0, -0.25}},
        {"BORDER", "Border __ __ . __ __ . __ __", {0.5, -0.25, 0.5, -0.25, 0.0, -0.25}},
    };

    const std::string upper = upcase(name);
    for (const Def& d : kDefs) {
        if (upper == d.name) {
            description = d.description;
            pattern.assign(d.pattern.begin(), d.pattern.end());
            return true;
        }
    }
    return false;
}

Step LtypeCommand::ask_option(CommandContext&) {
    state_ = State::Option;
    Prompt p;
    p.kind = PromptKind::String;
    p.message = "?/Create/Load/Set";
    p.keywords = {"?", "Create", "Load", "Set"};
    p.allow_empty = true;
    return Step::ask(p);
}

Step LtypeCommand::start(CommandContext& ctx) { return ask_option(ctx); }

Step LtypeCommand::next(CommandContext& ctx, const InputValue& value) {
    switch (state_) {
        case State::Option: {
            if (value.kind == InputKind::None) return Step::done(report_);

            if (keyword_is(value, "?")) {
                std::string list;
                for (const Linetype& lt : ctx.db.linetypes()) {
                    if (!list.empty()) list += "\n";
                    list += lt.name + "  " + lt.description;
                }
                report_ = list;
                return ask_option(ctx);
            }
            if (keyword_is(value, "LOAD")) {
                state_ = State::LoadName;
                Prompt p;
                p.kind = PromptKind::String;
                p.message = "Linetype(s) to load";
                return Step::ask(p);
            }
            if (keyword_is(value, "SET")) {
                state_ = State::SetName;
                Prompt p;
                p.kind = PromptKind::String;
                p.message = "New entity linetype (or ?) <BYLAYER>";
                p.allow_empty = true;
                return Step::ask(p);
            }
            if (keyword_is(value, "CREATE")) {
                state_ = State::CreateName;
                Prompt p;
                p.kind = PromptKind::String;
                p.message = "Name of linetype to create";
                return Step::ask(p);
            }
            return Step::failed("unknown option");
        }

        case State::LoadName: {
            if (value.kind != InputKind::String) return Step::failed("a linetype name is required");
            std::size_t loaded = 0;
            for (const std::string& name : split_names(value.text)) {
                std::string description;
                std::vector<double> pattern;
                if (!builtin_linetype(name, description, pattern)) {
                    report_ = "Linetype " + name + " is not defined";
                    continue;
                }
                ctx.db.add_linetype(name, description, std::move(pattern));
                ++loaded;
            }
            if (loaded != 0) report_ = std::to_string(loaded) + " loaded";
            return ask_option(ctx);
        }

        case State::SetName: {
            // Enter or BYLAYER means new entities follow their layer, which is
            // the default and the thing you go back to.
            if (value.kind == InputKind::None) {
                ctx.db.sysvars().set_string(Sysvar::CELtype, "BYLAYER");
                return ask_option(ctx);
            }
            if (value.kind != InputKind::String) return Step::failed("a linetype name is required");

            const std::string name = upcase(value.text);
            if (name == "BYLAYER" || name == "BYBLOCK") {
                ctx.db.sysvars().set_string(Sysvar::CELtype, name);
                return ask_option(ctx);
            }
            if (ctx.db.find_linetype(name) == kInvalidLinetype) {
                report_ = "Linetype " + name + " not loaded";
                return ask_option(ctx);
            }
            ctx.db.sysvars().set_string(Sysvar::CELtype, name);
            return ask_option(ctx);
        }

        case State::CreateName: {
            if (value.kind != InputKind::String) return Step::failed("a linetype name is required");
            pending_name_ = upcase(value.text);
            state_ = State::CreatePattern;

            Prompt p;
            p.kind = PromptKind::String;
            p.message = "Pattern (e.g. .5,-.25)";
            return Step::ask(p);
        }

        case State::CreatePattern: {
            if (value.kind != InputKind::String) return Step::failed("a pattern is required");

            std::vector<double> pattern;
            std::string token;
            const std::string text = value.text + ",";
            for (const char c : text) {
                if (c == ',' || c == ' ') {
                    if (!token.empty()) {
                        try {
                            pattern.push_back(std::stod(token));
                        } catch (...) {
                            // Exceptions are not control flow in this program;
                            // this one is caught at the boundary and turned
                            // into a status, which is the rule.
                            return Step::failed("pattern must be numbers separated by commas");
                        }
                        token.clear();
                    }
                } else {
                    token.push_back(c);
                }
            }
            if (pattern.empty()) return Step::failed("pattern must not be empty");

            ctx.db.add_linetype(pending_name_, "Created by LTYPE", std::move(pattern));
            report_ = pending_name_ + " created";
            return ask_option(ctx);
        }
    }
    return Step::failed("internal state error");
}

// --- COLOR / LTSCALE / LIMITS -----------------------------------------------

Step ColorCommand::start(CommandContext& ctx) {
    const std::int32_t current = ctx.db.sysvars().get_int(Sysvar::CEColor);
    std::string shown = std::to_string(current);
    if (current == kColorByLayer) shown = "BYLAYER";
    if (current == kColorByBlock) shown = "BYBLOCK";

    Prompt p;
    p.kind = PromptKind::Integer;
    p.message = "New entity color <" + shown + ">";
    p.keywords = {"BYLAYER", "BYBLOCK"};
    p.allow_empty = true;
    return Step::ask(p);
}

Step ColorCommand::next(CommandContext& ctx, const InputValue& value) {
    if (value.kind == InputKind::None) return Step::done();  // Enter keeps it

    std::int32_t colour = 0;
    if (keyword_is(value, "BYLAYER")) {
        colour = kColorByLayer;
    } else if (keyword_is(value, "BYBLOCK")) {
        colour = kColorByBlock;
    } else if (value.kind == InputKind::Integer) {
        colour = value.integer;
        if (colour < 1 || colour > 255) return Step::failed("colour must be between 1 and 255");
    } else {
        return Step::failed("a colour number, BYLAYER or BYBLOCK is required");
    }

    ctx.db.sysvars().set_int(Sysvar::CEColor, colour);
    return Step::done();
}

Step LtScaleCommand::start(CommandContext& ctx) {
    Prompt p;
    p.kind = PromptKind::Distance;
    p.message = "New scale factor <" + fmt(ctx.db.sysvars().get_real(Sysvar::LtScale)) + ">";
    p.allow_empty = true;
    return Step::ask(p);
}

Step LtScaleCommand::next(CommandContext& ctx, const InputValue& value) {
    if (value.kind == InputKind::None) return Step::done();

    double scale = 0.0;
    if (!distance_from(value, Vec3{0, 0, 0}, scale)) return Step::failed("a number is required");
    // Zero or less would make every dash zero-length, which draws nothing and
    // looks like the linetype having been lost.
    if (scale <= 0.0) return Step::failed("scale must be positive");

    ctx.db.sysvars().set_real(Sysvar::LtScale, scale);
    return Step::done("Regenerating drawing.");
}

Step LimitsCommand::start(CommandContext& ctx) {
    const Vec3 lower = ctx.db.sysvars().get_point(Sysvar::LimMin);

    Prompt p;
    p.kind = PromptKind::Point;
    p.message = "ON/OFF/<Lower left corner> <" + fmt(lower.x) + "," + fmt(lower.y) + ">";
    p.keywords = {"ON", "OFF"};
    p.allow_empty = true;
    return Step::ask(p);
}

Step LimitsCommand::next(CommandContext& ctx, const InputValue& value) {
    switch (state_) {
        case State::Lower: {
            // ON and OFF are about limit checking, not about the limits
            // themselves, so they answer and finish rather than continuing.
            if (keyword_is(value, "ON")) {
                ctx.db.sysvars().set_int(Sysvar::LimCheck, 1);
                return Step::done();
            }
            if (keyword_is(value, "OFF")) {
                ctx.db.sysvars().set_int(Sysvar::LimCheck, 0);
                return Step::done();
            }
            if (value.kind == InputKind::None) return Step::done();
            if (value.kind != InputKind::Point) return Step::failed("a point is required");

            lower_ = value.point;
            state_ = State::Upper;

            const Vec3 upper = ctx.db.sysvars().get_point(Sysvar::LimMax);
            Prompt p;
            p.kind = PromptKind::Point;
            p.message = "Upper right corner <" + fmt(upper.x) + "," + fmt(upper.y) + ">";
            return Step::ask(p);
        }

        case State::Upper: {
            if (value.kind != InputKind::Point) return Step::failed("a point is required");

            // Ordered, so that a corner pair given the other way round still
            // describes a rectangle rather than an inside-out one.
            const Vec3 a = lower_;
            const Vec3 b = value.point;
            const Vec3 lo{std::min(a.x, b.x), std::min(a.y, b.y), 0.0};
            const Vec3 hi{std::max(a.x, b.x), std::max(a.y, b.y), 0.0};
            if (lo.x == hi.x || lo.y == hi.y) return Step::failed("limits must enclose an area");

            ctx.db.sysvars().set_point(Sysvar::LimMin, lo);
            ctx.db.sysvars().set_point(Sysvar::LimMax, hi);
            return Step::done();
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
            if (keyword_is(value, "EXTENTS")) {
                ctx.view->zoom_extents();
                return Step::done();
            }
            if (keyword_is(value, "ALL")) {
                // All shows the limits, or the extents when something has been
                // drawn outside them -- otherwise geometry off the paper would
                // become invisible with no way to find it.
                BBox box = ctx.db.extents();
                box.expand(ctx.db.sysvars().get_point(Sysvar::LimMin));
                box.expand(ctx.db.sysvars().get_point(Sysvar::LimMax));
                if (!box.valid()) {
                    ctx.view->zoom_extents();
                    return Step::done();
                }
                ctx.view->zoom_window(box.min, box.max);
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

// --- DXFIN ------------------------------------------------------------------

Step DxfInCommand::start(CommandContext&) {
    Prompt p;
    p.kind = PromptKind::String;
    p.message = "File name";
    return Step::ask(p);
}

Step DxfInCommand::next(CommandContext& ctx, const InputValue& value) {
    if (value.kind != InputKind::String || value.text.empty()) {
        return Step::failed("a file name is required");
    }

    std::string path = value.text;
    if (path.size() < 4 || path.compare(path.size() - 4, 4, ".dxf") != 0) path += ".dxf";

    const DxfReadResult r = read_dxf_file(ctx.db, path);
    if (!r.ok) return Step::failed(r.error.empty() ? "could not read the file" : r.error);

    // The selection named entities in a drawing that no longer exists.
    ctx.selection.clear();

    std::string message = std::to_string(r.entities) + " entities, " +
                          std::to_string(r.layers) + " layers";
    if (r.proxies != 0) {
        // Said plainly rather than buried: these are the parts this program
        // cannot edit, and knowing before editing beats discovering after.
        message += ", " + std::to_string(r.proxies) +
                   " kept as-is (not editable, written back unchanged)";
    }
    if (r.newer_version) message += ". Warning: file is " + r.version + ", newer than R12";
    return Step::done(message);
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
    if (upper == "COLOR") return std::make_unique<ColorCommand>();
    if (upper == "LIMITS") return std::make_unique<LimitsCommand>();
    if (upper == "LTSCALE") return std::make_unique<LtScaleCommand>();
    if (upper == "ERASE") return std::make_unique<EraseCommand>();
    if (upper == "DXFIN" || upper == "OPEN") return std::make_unique<DxfInCommand>();
    if (upper == "DXFOUT") return std::make_unique<DxfOutCommand>();
    if (upper == "AREA") return std::make_unique<AreaCommand>();
    if (upper == "ARRAY") return std::make_unique<ArrayCommand>();
    if (upper == "DIST") return std::make_unique<DistCommand>();
    if (upper == "ID") return std::make_unique<IdCommand>();
    if (upper == "LAYER") return std::make_unique<LayerCommand>();
    if (upper == "LIST") return std::make_unique<ListCommand>();
    if (upper == "LTYPE") return std::make_unique<LtypeCommand>();
    if (upper == "PAN") return std::make_unique<PanCommand>();
    if (upper == "BASE") return std::make_unique<BaseCommand>();
    if (upper == "BREAK") return std::make_unique<BreakCommand>();
    if (upper == "BLOCK") return std::make_unique<BlockCommand>();
    if (upper == "EXPLODE") return std::make_unique<ExplodeCommand>();
    if (upper == "INSERT") return std::make_unique<InsertCommand>(false);
    if (upper == "MINSERT") return std::make_unique<InsertCommand>(true);
    if (upper == "WBLOCK") return std::make_unique<WblockCommand>();
    if (upper == "PEDIT") return std::make_unique<PeditCommand>();
    if (upper == "PLINE") return std::make_unique<PlineCommand>();
    if (upper == "POINT") return std::make_unique<PointCommand>();
    if (upper == "SOLID") return std::make_unique<SolidCommand>(false);
    if (upper == "3DFACE") return std::make_unique<SolidCommand>(true);
    if (upper == "TEXT") return std::make_unique<TextCommand>();
    if (upper == "PLAN") return std::make_unique<PlanCommand>();
    if (upper == "ZOOM") return std::make_unique<ZoomCommand>();
    if (upper == "MOVE") return std::make_unique<MoveCommand>(false);
    if (upper == "ROTATE") return std::make_unique<TransformCommand>(TransformCommand::Kind::Rotate);
    if (upper == "ROTATE3D") return std::make_unique<Rotate3dCommand>();
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
        "AREA", "ARRAY", "CIRCLE", "COLOR", "COPY", "DIST", "DXFIN", "DXFOUT", "ERASE",
        "ID", "OPEN",
        "LIMITS", "LTSCALE",
        "3DFACE", "BASE", "BLOCK", "BREAK", "EXPLODE", "INSERT", "MINSERT", "WBLOCK",
        "LAYER", "LINE", "LIST", "LTYPE", "MIRROR", "MOVE", "PAN",  "PEDIT", "PLAN", "PLINE", "POINT",
        "REDO", "ROTATE", "ROTATE3D", "SCALE", "SOLID", "STRETCH", "TEXT", "UNDO", "ZOOM"};
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
        {"LA", "LAYER"},
        {"LT", "LTYPE"},
        {"LI", "LIST"},
        {"CP", "COPY"},
        {"L", "LINE"},
        {"PL", "PLINE"},
        {"PO", "POINT"},
        {"PE", "PEDIT"},
        {"B", "BLOCK"},
        {"BR", "BREAK"},
        {"I", "INSERT"},
        {"X", "EXPLODE"},
        {"W", "WBLOCK"},
        {"SO", "SOLID"},
        {"DT", "TEXT"},
        {"3F", "3DFACE"},
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
