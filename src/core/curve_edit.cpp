// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Cutting curves at parameters.
//
// The polyline case is the only fiddly one, and the reason is bulges: cutting
// an arc segment part way along changes its included angle, so the piece keeps
// a different bulge from the one it was cut out of. Getting that wrong is
// invisible on a straight polyline and obvious on a curved one, which is the
// worst combination, so it is tested directly.
#include "noto/curve_edit.hpp"

#include "noto/ecs.hpp"
#include "noto/entities.hpp"
#include "noto/intersect.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace noto {
namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kSpanEps = 1e-9;

void carry_properties(const Entity& from, Entity& to) {
    to.props() = from.props();
}

// --- polyline ---------------------------------------------------------------

// Which segment a polyline parameter falls in, and how far along it.
void polyline_split(const Polyline& p, double t, std::size_t* segment, double* fraction) {
    const std::size_t n = p.segment_count();
    if (n == 0) {
        *segment = 0;
        *fraction = 0.0;
        return;
    }
    const double scaled = std::clamp(t, 0.0, 1.0) * static_cast<double>(n);
    std::size_t index = static_cast<std::size_t>(scaled);
    if (index >= n) index = n - 1;  // t == 1 belongs to the last segment
    *segment = index;
    *fraction = scaled - static_cast<double>(index);
}

// The bulge of the piece of an arc segment covering [from, to] of its length.
//
// A bulge is the quarter-angle tangent of the included angle, so a fraction of
// the segment carries that fraction of the angle -- and the bulge is NOT simply
// scaled, it is recomputed from the shortened angle.
double partial_bulge(double bulge, double from, double to) {
    if (bulge == 0.0) return 0.0;
    const double included = 4.0 * std::atan(bulge);
    return std::tan(included * (to - from) * 0.25);
}

// Appends the vertices of the polyline span running forward from `ta` to `tb`.
// `tb` above 1 wraps through the start, which is how a closed polyline's
// surviving piece is asked for.
void append_span(const Polyline& p, double ta, double tb, std::vector<PolyVertex>& out) {
    const std::size_t n = p.segment_count();
    if (n == 0) return;

    const bool wraps = tb > 1.0 + kSpanEps;
    const double end = wraps ? tb - 1.0 : tb;

    std::size_t seg_a = 0;
    std::size_t seg_b = 0;
    double frac_a = 0.0;
    double frac_b = 0.0;
    polyline_split(p, ta, &seg_a, &frac_a);
    polyline_split(p, end, &seg_b, &frac_b);

    Vec3 start_point{};
    Vec3 end_point{};
    curve_point_at(p, ta, &start_point);
    curve_point_at(p, end, &end_point);

    // How many segments the span covers, walking forward and wrapping.
    std::size_t steps = 0;
    if (wraps) {
        steps = (seg_b + n - seg_a);
    } else {
        steps = seg_b - seg_a;
    }

    const std::vector<PolyVertex>& verts = p.vertices();

    if (steps == 0) {
        // Both ends inside one segment: a single piece of it.
        PolyVertex first = verts[seg_a];
        first.pos = start_point;
        first.bulge = partial_bulge(verts[seg_a].bulge, frac_a, frac_b);
        out.push_back(first);
        out.push_back(PolyVertex{end_point, 0.0, first.start_width, first.end_width});
        return;
    }

    // The partial first segment.
    PolyVertex first = verts[seg_a];
    first.pos = start_point;
    first.bulge = partial_bulge(verts[seg_a].bulge, frac_a, 1.0);
    out.push_back(first);

    // Every whole segment in between, taken as they are.
    for (std::size_t k = 1; k < steps; ++k) {
        const std::size_t index = (seg_a + k) % p.size();
        out.push_back(verts[index]);
    }

    // The partial last segment, if it is entered at all.
    const std::size_t last = seg_b % p.size();
    if (frac_b > kSpanEps) {
        PolyVertex tail = verts[last];
        tail.bulge = partial_bulge(verts[last].bulge, 0.0, frac_b);
        out.push_back(tail);
        out.push_back(PolyVertex{end_point, 0.0, tail.start_width, tail.end_width});
    } else {
        // The span ends exactly on a vertex, so that vertex closes it and
        // carries no bulge onward.
        PolyVertex tail = verts[last];
        tail.bulge = 0.0;
        out.push_back(tail);
    }
}

EntityPtr polyline_span(const Polyline& p, double ta, double tb) {
    auto out = std::make_unique<Polyline>();
    carry_properties(p, *out);
    append_span(p, ta, tb, out->vertices());
    if (out->size() < 2) return nullptr;
    // Always open: a loop with a piece removed is not a loop.
    out->set_closed(false);
    return out;
}

// --- circular ---------------------------------------------------------------

EntityPtr circular_span(const Entity& e, double ta, double tb) {
    Vec3 centre{};
    double radius = 0.0;
    double base_angle = 0.0;
    double sweep = kTwoPi;

    if (e.type() == EntityType::Circle) {
        const Circle& c = static_cast<const Circle&>(e);
        centre = c.center();
        radius = c.radius();
        base_angle = 0.0;
        sweep = kTwoPi;
    } else {
        const Arc& a = static_cast<const Arc&>(e);
        centre = a.center();
        radius = a.radius();
        base_angle = a.start_angle();
        sweep = a.sweep();
    }

    const double span = tb - ta;
    if (span <= kSpanEps) return nullptr;

    auto out = std::make_unique<Arc>(centre, radius, base_angle + sweep * ta,
                                     base_angle + sweep * tb, e.props().normal);
    carry_properties(e, *out);
    return out;
}

}  // namespace

EntityPtr extract_curve_span(const Entity& e, double ta, double tb) {
    switch (e.type()) {
        case EntityType::Line: {
            const Line& l = static_cast<const Line&>(e);
            if (tb - ta <= kSpanEps) return nullptr;
            const Vec3 d = l.end() - l.start();
            auto out = std::make_unique<Line>(l.start() + d * ta, l.start() + d * tb);
            carry_properties(e, *out);
            return out;
        }

        case EntityType::Circle:
        case EntityType::Arc: {
            // A wrapping span on a circle is asked for as ta > tb; lifting the
            // end past one turn expresses the same arc going forward.
            if (tb < ta) return circular_span(e, ta, tb + 1.0);
            return circular_span(e, ta, tb);
        }

        case EntityType::Polyline: {
            const Polyline& p = static_cast<const Polyline&>(e);
            if (tb < ta) return polyline_span(p, ta, tb + 1.0);
            if (tb - ta <= kSpanEps) return nullptr;
            return polyline_span(p, ta, tb);
        }

        default: return nullptr;  // no curve to cut
    }
}

std::size_t break_curve(const Entity& e, double t0, double t1, std::vector<EntityPtr>& out) {
    const std::size_t before = out.size();

    if (curve_is_closed(e)) {
        // The order is the answer here, not an accident of which was picked
        // first: R12 removes the span running counterclockwise from the first
        // point to the second, so what survives is the rest of the loop --
        // which is the span from the second point round to the first.
        if (std::abs(t1 - t0) <= kSpanEps) return 0;  // nothing to open the loop at
        if (EntityPtr piece = extract_curve_span(e, t1, t0)) out.push_back(std::move(piece));
        return out.size() - before;
    }

    // Open: "between" is symmetric, so the two points sort.
    double lo = std::min(t0, t1);
    double hi = std::max(t0, t1);
    lo = std::clamp(lo, 0.0, 1.0);
    hi = std::clamp(hi, 0.0, 1.0);

    // A break reaching an end shortens the curve rather than leaving a
    // zero-length stub beside it.
    if (lo > kSpanEps) {
        if (EntityPtr head = extract_curve_span(e, 0.0, lo)) out.push_back(std::move(head));
    }
    if (hi < 1.0 - kSpanEps) {
        if (EntityPtr tail = extract_curve_span(e, hi, 1.0)) out.push_back(std::move(tail));
    }
    return out.size() - before;
}

}  // namespace noto
