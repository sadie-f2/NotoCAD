// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/dash.hpp"

#include "ncad/database.hpp"

#include <cmath>

namespace ncad {
namespace {

// A dot is a zero-length pattern element. Something has to be drawn or it is
// indistinguishable from a gap, so it becomes a very short dash -- short enough
// to read as a dot at any sane zoom.
constexpr double kDotLength = 1e-6;

}  // namespace

DashRenderer::DashRenderer(Renderer& target, const Database& db, double ltscale)
    : target_(target), db_(db), ltscale_(ltscale > 0.0 ? ltscale : 1.0) {}

void DashRenderer::begin_entity(const EntityProps& props) {
    target_.begin_entity(props);

    pattern_.clear();
    period_ = 0.0;

    // An entity's own linetype, or its layer's.
    //
    // NOTE: kLinetypeContinuous currently doubles as BYLAYER, because nothing
    // yet distinguishes "explicitly continuous" from "take the layer's". When
    // an explicit CONTINUOUS becomes possible this needs a real BYLAYER marker
    // -- recorded in SF_todo.md.
    LinetypeId id = props.linetype;
    if (id == kLinetypeContinuous && props.layer < db_.layers().size()) {
        id = db_.layer(props.layer).linetype;
    }
    if (id >= db_.linetypes().size()) return;

    for (const double d : db_.linetype(id).pattern) {
        const double scaled = (d == 0.0) ? kDotLength : d * ltscale_;
        pattern_.push_back(d < 0.0 ? -std::abs(scaled) : std::abs(scaled));
        period_ += std::abs(scaled);
    }
    // A floor, not just a zero test. The loop in polyline() spends one pattern
    // element per iteration, so the iteration count is segment_length/period_
    // -- and NOTHING bounds the period from below. LTSCALE is a Real sysvar and
    // Real sysvars are not range-checked, so `(setvar "LTSCALE" 1.0e-6)` on a
    // 1000-unit line is ~1.3e9 iterations, each emitting a two-point polyline
    // downstream. Group 49 straight out of a DXF can do the same with no user
    // action at all: the drawing simply wedges on the redraw after it loads.
    //
    // Below this a dash is far smaller than a pixel at any sane zoom, so
    // drawing the entity continuous is not a compromise -- it is what the
    // dashes would have looked like anyway.
    constexpr double kMinPeriod = 1e-4;
    if (period_ < kMinPeriod) pattern_.clear();  // degenerate: treat as continuous

    // Every entity starts at the beginning of its pattern. R12 restarts per
    // entity rather than running one continuous phase through the drawing,
    // which is why two collinear lines each begin with a dash.
    index_ = 0;
    remaining_ = pattern_.empty() ? 0.0 : std::abs(pattern_[0]);
    drawing_ = pattern_.empty() || pattern_[0] >= 0.0;
    run_.clear();
}

void DashRenderer::flush() {
    if (run_.size() >= 2) target_.polyline(run_.data(), run_.size(), false);
    run_.clear();
}

void DashRenderer::extend(const Vec3& a, const Vec3& b) {
    if (run_.empty()) run_.push_back(a);
    run_.push_back(b);
}

void DashRenderer::polyline(const Vec3* pts, std::size_t count, bool closed) {
    if (pattern_.empty() || count < 2) {
        target_.polyline(pts, count, closed);
        return;
    }

    // Walk the run as a chain of segments, spending pattern length as it goes.
    // The closing segment is walked too, and the result is emitted as open
    // pieces -- a dashed outline is not a closed polygon any more.
    const std::size_t last = closed ? count : count - 1;
    for (std::size_t i = 0; i < last; ++i) {
        Vec3 from = pts[i];
        const Vec3 to = pts[(i + 1) % count];

        double left = length(to - from);
        if (left <= 0.0) continue;
        const Vec3 dir = (to - from) / left;

        while (left > 0.0) {
            if (remaining_ <= left) {
                // The current dash or gap ends inside this segment.
                const Vec3 at = from + dir * remaining_;
                if (drawing_) {
                    extend(from, at);
                    flush();
                }
                from = at;
                left -= remaining_;

                index_ = (index_ + 1) % pattern_.size();
                remaining_ = std::abs(pattern_[index_]);
                drawing_ = pattern_[index_] >= 0.0;
            } else {
                if (drawing_) extend(from, to);
                remaining_ -= left;
                left = 0.0;
            }
        }
    }
    flush();
}

}  // namespace ncad
