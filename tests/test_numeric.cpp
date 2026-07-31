// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Conditioning tests. Everything else in the suite runs at coordinates around
// 1000, where double precision is never in doubt; these deliberately run where
// it is.
//
// The construction is one circle of radius R centred at (R, 0, 0), so it passes
// through the origin exactly and is tangent to the Y axis there. That single
// shape puts every quantity of interest at a known value while forcing the
// kernel to compute it from operands R apart:
//
//   - The tangency at the origin is the degenerate case of line/circle.
//   - A chord at x = e cuts it at y = +/- sqrt(2Re - e^2), a well conditioned
//     ANSWER reached through an ill conditioned computation. The usual form
//     sqrt(r^2 - (e - R)^2) subtracts two numbers of size R^2 to get one of
//     size Re, and loses log10(R/e) digits doing it.
//
// Errors are reported in ULP rather than in drawing units, because the whole
// question is how many significant digits survive, and a fixed number of units
// means something different at R = 1e3 and R = 1e12.

#include "ncad/entities.hpp"
#include "ncad/intersect.hpp"
#include "ncad/vec3.hpp"
#include "test.hpp"

#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace ncad;

namespace {

// Distance in representable doubles. The IEEE754 bit pattern is monotonic
// within a sign, so for same-signed operands this is a subtraction.
std::int64_t ulps(double a, double b) {
    if (a == b) return 0;
    if (std::isnan(a) || std::isnan(b)) return INT64_MAX;

    std::int64_t ia = 0, ib = 0;
    std::memcpy(&ia, &a, sizeof ia);
    std::memcpy(&ib, &b, sizeof ib);

    // Opposite sides of zero: the patterns are not comparable, and for this
    // suite a sign flip is a failure whatever its magnitude.
    if ((ia < 0) != (ib < 0)) return INT64_MAX;
    return ia > ib ? ia - ib : ib - ia;
}

double rel_err(double got, double want) {
    if (got == want) return 0.0;
    const double scale = std::fabs(want);
    return scale > 0.0 ? std::fabs(got - want) / scale : std::fabs(got - want);
}

// The stable form of the chord half-height. Written this way on purpose: it is
// the reference the kernel is measured against, so it must not share the
// cancellation being tested. 2Re is one rounding, e^2 is negligible beside it,
// and sqrt is correctly rounded -- about one ULP overall. Spot checked against
// bc at 40 digits.
double chord_half_height(double radius, double x) { return std::sqrt(2.0 * radius * x - x * x); }

constexpr double kDoubleEps = 2.220446049250313e-16;

// How much relative error the PROBLEM allows, before any question of how well
// the kernel solves it -- the point being that a flat tolerance is the wrong
// shape of assertion here and asserting one only pins the wrong thing.
//
// Whatever formula is used, the offset e is known only to the spacing of a
// double at coordinate R, which is about eps*R. Pushing that through
// y = sqrt(2Re), where dy/de = R/y, gives
//
//     dy/y = (R/y^2) * eps*R = eps*R / 2e
//
// At R = 1e12 and e = 1e-3 that is 11%: the chord genuinely is not recoverable
// there, and no rearrangement makes it so. At R = 1e6 and e = 1 it is 1e-10,
// which is a real demand, and the textbook quadratic missed it by 118x.
double conditioning_bound(double radius, double x) {
    // The floor keeps the well conditioned rows from being asked for better
    // than correctly rounded arithmetic can give.
    return std::fmax(8.0 * kDoubleEps, kDoubleEps * radius / (2.0 * x));
}

const double kRadii[] = {1.0e3, 1.0e6, 1.0e9, 1.0e12};

}  // namespace

TEST_CASE("numeric: circle through the origin cuts a chord where it should") {
    std::printf("\n      chord at x=e on circle r=R centred (R,0,0)\n");
    std::printf("      %-8s %-8s %-22s %-22s %11s %9s %6s\n", "R", "e", "expected y", "got y",
                "ULP", "bound", "used");

    for (const double radius : kRadii) {
        const Circle c(Vec3{radius, 0.0, 0.0}, radius);

        for (const double x : {1.0e-3, 1.0, 1.0e3}) {
            if (x >= radius) continue;

            const double want = chord_half_height(radius, x);

            IntersectionList hits;
            intersect_line_circle(Vec3{x, -radius, 0.0}, Vec3{x, radius, 0.0}, c.center(),
                                  c.radius(), c.props().normal, hits);

            if (hits.size() != 2) {
                std::printf("      %-8.0e %-8.0e %-22.17g %-22s %11s %9s %6s\n", radius, x, want,
                            "(no chord)", "-", "-", "-");
                CHECK(hits.size() == 2);
                continue;
            }

            // Order is not guaranteed; compare the positive root.
            const double got = std::fmax(hits[0].point.y, hits[1].point.y);

            const double err = rel_err(got, want);
            const double bound = conditioning_bound(radius, x);

            // "used" is the fraction of the error budget the problem itself
            // allows that the kernel actually spends. It is the number to watch:
            // it should stay well under 1 across the whole table, and a formula
            // that starts losing digits shows up as a column drifting upward
            // long before any assertion fails.
            std::printf("      %-8.0e %-8.0e %-22.17g %-22.17g %11" PRId64 " %9.1e %6.2f\n", radius,
                        x, want, got, ulps(got, want), bound, err / bound);

            CHECK(err <= bound);
        }
    }
}

TEST_CASE("numeric: the circle stays tangent to the Y axis at the origin") {
    std::printf("\n      tangency of circle r=R centred (R,0,0) with the Y axis\n");
    std::printf("      %-10s %-8s %-24s %10s\n", "R", "hits", "worst |p|", "ULP y");

    for (const double radius : kRadii) {
        const Circle c(Vec3{radius, 0.0, 0.0}, radius);

        IntersectionList hits;
        intersect_line_circle(Vec3{0.0, -radius, 0.0}, Vec3{0.0, radius, 0.0}, c.center(),
                              c.radius(), c.props().normal, hits);

        double worst = 0.0;
        for (const Intersection& h : hits) worst = std::fmax(worst, length(h.point));

        std::printf("      %-10.0e %-8zu %-24.17g %10" PRId64 "\n", radius, hits.size(), worst,
                    hits.empty() ? std::int64_t{0} : ulps(worst, 0.0));

        // Tangency may be reported as one point or as two coincident ones --
        // both are defensible, and which it is is not what this pins. What it
        // pins is that a solution IS found and that it is at the origin.
        CHECK(!hits.empty());
        CHECK(worst < radius * 1.0e-9);
    }
}

TEST_CASE("numeric: every point of a large circle is one radius from its centre") {
    std::printf("\n      |curve_point_at(t) - centre| against R\n");
    std::printf("      %-10s %-24s %10s\n", "R", "worst rel err", "worst ULP");

    for (const double radius : kRadii) {
        const Circle c(Vec3{radius, 0.0, 0.0}, radius);

        double worst_rel = 0.0;
        std::int64_t worst_ulp = 0;

        for (int i = 0; i < 64; ++i) {
            const double t = static_cast<double>(i) / 64.0;

            Vec3 p{};
            REQUIRE(curve_point_at(c, t, &p));

            const double got = length(p - c.center());
            worst_rel = std::fmax(worst_rel, rel_err(got, radius));
            worst_ulp = std::max(worst_ulp, ulps(got, radius));
        }

        std::printf("      %-10.0e %-24.3e %10" PRId64 "\n", radius, worst_rel, worst_ulp);
        CHECK(worst_rel < 1.0e-12);
    }
}
