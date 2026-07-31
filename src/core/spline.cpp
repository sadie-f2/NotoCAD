// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The NURBS curve: evaluation, interpolation, and the entity vtable.
//
// In its own file rather than in the shared entities*.cpp, the way polyline.cpp
// and text.cpp are, because the mathematics is the bulk of it and does not
// belong scattered across four files by which vtable slot happens to use it.
//
// The algorithms are the standard ones -- Piegl and Tiller, "The NURBS Book" --
// and are written out rather than pulled in: a dependency for four hundred
// lines of well-understood recurrence would cost more than it saves, and this
// project's licensing care makes an extra library a decision rather than a
// convenience.

#include "ncad/entities.hpp"

#include "ncad/dxf.hpp"
#include "ncad/ecs.hpp"
#include "ncad/render.hpp"

#include <algorithm>
#include <cmath>

namespace ncad {
namespace {

// A control point in homogeneous form, which is what makes a rational curve
// evaluate with the same recurrence as a polynomial one: interpolate (wx, wy,
// wz, w) and divide at the end.
struct Weighted {
    Vec3 p{};
    double w{1.0};
};

Weighted operator*(const Weighted& a, double k) { return Weighted{a.p * k, a.w * k}; }
Weighted operator+(const Weighted& a, const Weighted& b) {
    return Weighted{a.p + b.p, a.w + b.w};
}

// The knot span containing u: the index i with knots[i] <= u < knots[i+1],
// clamped so the last span owns the closing endpoint.
//
// Binary search rather than a scan. A spline from analysis data can carry
// thousands of control points and this is called once per flattened segment.
std::size_t find_span(int degree, const std::vector<double>& knots, std::size_t n, double u) {
    const std::size_t p = static_cast<std::size_t>(degree);
    if (u >= knots[n]) return n - 1;
    if (u <= knots[p]) return p;

    std::size_t lo = p;
    std::size_t hi = n;
    std::size_t mid = (lo + hi) / 2;
    while (u < knots[mid] || u >= knots[mid + 1]) {
        if (u < knots[mid]) {
            hi = mid;
        } else {
            lo = mid;
        }
        mid = (lo + hi) / 2;
    }
    return mid;
}

// The degree+1 non-zero basis functions at u, by the Cox-de Boor recurrence in
// its triangular form -- no divisions by zero even at repeated knots, which is
// the whole reason it is written this way rather than from the definition.
//
// STACK ONLY, and that is a performance decision with evidence behind it. This
// is called once per evaluated point of every spline in every frame, and it
// used to allocate three vectors per call -- four counting the one point_at
// made for the result. A drawing of twenty thousand splines is a million heap
// operations a frame, and gdb caught it doing exactly that: every sample of a
// wedged viewport landed in the allocator underneath this function.
//
// `out` must hold degree + 1 doubles. Spline::valid() bounds the degree, so a
// caller that has checked validity cannot overflow the buffer.
void basis_functions(std::size_t span, double u, int degree, const std::vector<double>& knots,
                     double* out) {
    const std::size_t p = static_cast<std::size_t>(degree);
    double left[kMaxSplineDegree + 1] = {};
    double right[kMaxSplineDegree + 1] = {};

    for (std::size_t i = 0; i <= p; ++i) out[i] = 0.0;
    out[0] = 1.0;
    for (std::size_t j = 1; j <= p; ++j) {
        left[j] = u - knots[span + 1 - j];
        right[j] = knots[span + j] - u;
        double saved = 0.0;
        for (std::size_t r = 0; r < j; ++r) {
            const double denom = right[r + 1] + left[j - r];
            const double temp = denom != 0.0 ? out[r] / denom : 0.0;
            out[r] = saved + right[r + 1] * temp;
            saved = left[j - r] * temp;
        }
        out[j] = saved;
    }
}

// Solves A x = b in place, by Gaussian elimination with partial pivoting.
//
// Dense, and knowingly so. The interpolation system below is banded with
// bandwidth degree+1, and a banded solver would make this O(n) instead of
// O(n^3) -- but a spline picked by hand has tens of points, where the
// difference is unmeasurable. Recorded in SF_todo rather than optimised blind:
// the case that would justify it is a spline generated from analysis data, and
// that path does not exist yet.
bool solve_dense(std::vector<std::vector<double>>& a, std::vector<Weighted>& b) {
    const std::size_t n = a.size();
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        for (std::size_t r = col + 1; r < n; ++r) {
            if (std::abs(a[r][col]) > std::abs(a[pivot][col])) pivot = r;
        }
        if (std::abs(a[pivot][col]) < 1e-12) return false;  // singular

        std::swap(a[col], a[pivot]);
        std::swap(b[col], b[pivot]);

        for (std::size_t r = col + 1; r < n; ++r) {
            const double f = a[r][col] / a[col][col];
            if (f == 0.0) continue;
            for (std::size_t c = col; c < n; ++c) a[r][c] -= f * a[col][c];
            b[r] = b[r] + b[col] * -f;
        }
    }

    for (std::size_t i = n; i-- > 0;) {
        Weighted acc = b[i];
        for (std::size_t c = i + 1; c < n; ++c) acc = acc + b[c] * -a[i][c];
        b[i] = acc * (1.0 / a[i][i]);
    }
    return true;
}

}  // namespace

Spline::Spline(int degree, std::vector<Vec3> control_points, std::vector<double> knots,
               std::vector<double> weights, const Vec3& normal)
    : Entity(EntityType::Spline),
      degree_(degree),
      control_(std::move(control_points)),
      knots_(std::move(knots)),
      weights_(std::move(weights)) {
    props().normal = normalize(normal);
}

bool Spline::valid() const {
    if (degree_ < 1) return false;
    // Bounds the evaluator's stack scratch; see kMaxSplineDegree.
    if (degree_ > kMaxSplineDegree) return false;
    if (control_.size() < static_cast<std::size_t>(degree_) + 1) return false;
    if (knots_.size() != control_.size() + static_cast<std::size_t>(degree_) + 1) return false;
    if (!weights_.empty() && weights_.size() != control_.size()) return false;
    return true;
}

double Spline::domain_min() const {
    return valid() ? knots_[static_cast<std::size_t>(degree_)] : 0.0;
}

double Spline::domain_max() const { return valid() ? knots_[control_.size()] : 0.0; }

bool Spline::is_closed() const {
    return control_.size() >= 2 && near_equal(control_.front(), control_.back(), kEps);
}

Vec3 Spline::point_at(double u) const {
    if (!valid()) return Vec3{};

    const std::size_t p = static_cast<std::size_t>(degree_);
    const double t = std::clamp(u, domain_min(), domain_max());
    const std::size_t span = find_span(degree_, knots_, control_.size(), t);

    double n[kMaxSplineDegree + 1];
    basis_functions(span, t, degree_, knots_, n);

    Weighted acc{Vec3{}, 0.0};
    for (std::size_t i = 0; i <= p; ++i) {
        const std::size_t k = span - p + i;
        const double w = weights_.empty() ? 1.0 : weights_[k];
        acc = acc + Weighted{control_[k] * w, w} * n[i];
    }
    // The projective divide. For a non-rational curve every weight is one and
    // this divides by one, so there is no separate polynomial path to keep in
    // step with this one.
    if (std::abs(acc.w) < 1e-15) return Vec3{};
    return acc.p * (1.0 / acc.w);
}

Vec3 Spline::tangent_at(double u) const {
    if (!valid()) return Vec3{};

    // By difference rather than by the analytic derivative, which for a
    // RATIONAL curve is the quotient rule over two B-spline derivatives and is
    // a great deal of code to get a direction that is then normalised away. The
    // step is scaled to the domain so it is neither lost to rounding on a tiny
    // parameter range nor coarse on a large one.
    const double lo = domain_min();
    const double hi = domain_max();
    const double span = hi - lo;
    if (span <= 0.0) return Vec3{};

    const double h = span * 1e-6;
    const double t = std::clamp(u, lo, hi);
    const Vec3 a = point_at(std::max(lo, t - h));
    const Vec3 b = point_at(std::min(hi, t + h));
    const Vec3 d = b - a;
    return is_zero(d) ? Vec3{} : normalize(d);
}

int Spline::segment_count(double chord_tolerance) const {
    if (!valid()) return 0;

    // There is no radius to size this by, so the control polygon stands in for
    // the curve: it is always at least as long, and always at least as curved,
    // which makes it a safe over-estimate rather than a guess.
    double polygon = 0.0;
    for (std::size_t i = 1; i < control_.size(); ++i) {
        polygon += length(control_[i] - control_[i - 1]);
    }
    if (polygon <= 0.0) return 0;

    // arc_segment_count treats its first argument as a radius; feeding the
    // polygon length as one is deliberately conservative, and the result is
    // clamped so a huge drawing does not emit a million segments per curve.
    const double tol = chord_tolerance > 0.0 ? chord_tolerance : polygon * 1e-3;
    int n = arc_segment_count(polygon, 1.0, tol);

    // The floor exists because a spline can wiggle between its control points,
    // and too few samples draw a curve as a straight line.
    n = std::max(n, static_cast<int>(control_.size()) * 4);

    // But it must not outlive the reason for it. NEVER MORE SEGMENTS THAN THE
    // CURVE IS PIXELS LONG: detail below a pixel cannot be seen, and the floor
    // alone had every spline emitting sixteen segments however small it was on
    // screen. In a drawing zoomed out to a million of them that is the whole
    // cost of the frame, spent on wiggles nobody can resolve.
    //
    // Sized by the BOUNDING BOX, not the control polygon. The polygon is a
    // deliberate over-estimate of the curve's length -- an interpolating spline
    // overshoots, so its control polygon can be three times the curve -- and
    // using it here would mean the bound almost never bit. The box diagonal is
    // the honest answer to "how big is this on screen".
    //
    // `tol` is half a pixel of sag, so diagonal/tol is about twice the size in
    // pixels: deliberately generous, since this bounds quality rather than
    // setting it.
    const BBox box = bbox();
    const double diagonal = box.valid() ? length(box.max - box.min) : polygon;
    const int pixels = static_cast<int>(diagonal / tol) + 1;
    n = std::min(n, pixels);

    // One segment is the least that draws anything at all.
    return std::clamp(n, 1, 4096);
}

EntityPtr Spline::interpolating(const std::vector<Vec3>& through, int degree, const Vec3& normal) {
    if (through.size() < 2) return nullptr;

    // Degree drops rather than failing when there are too few points: two
    // points are a straight line whatever degree was asked for, and refusing
    // would make the command reject its own first two picks.
    const int p = std::clamp(degree, 1, static_cast<int>(through.size()) - 1);
    const std::size_t n = through.size();
    const std::size_t pp = static_cast<std::size_t>(p);

    // CENTRIPETAL parameterisation, not chord-length. It is the one that does
    // not overshoot on sharply-turning data, which is exactly what hand-picked
    // points produce; chord-length is the textbook default and visibly loops
    // when two points sit close together after a long segment.
    std::vector<double> u(n, 0.0);
    double total = 0.0;
    for (std::size_t i = 1; i < n; ++i) {
        total += std::sqrt(length(through[i] - through[i - 1]));
    }
    if (total <= 0.0) return nullptr;  // every point identical

    for (std::size_t i = 1; i < n - 1; ++i) {
        u[i] = u[i - 1] + std::sqrt(length(through[i] - through[i - 1])) / total;
    }
    u[n - 1] = 1.0;

    // Averaged knots, which is what keeps the interpolation system banded and
    // non-singular -- the Schoenberg-Whitney condition, satisfied by
    // construction rather than checked afterwards.
    std::vector<double> knots(n + pp + 1, 0.0);
    for (std::size_t i = 0; i <= pp; ++i) knots[i] = 0.0;
    for (std::size_t i = n; i < n + pp + 1; ++i) knots[i] = 1.0;
    for (std::size_t j = 1; j + pp < n; ++j) {
        double sum = 0.0;
        for (std::size_t i = j; i < j + pp; ++i) sum += u[i];
        knots[j + pp] = sum / static_cast<double>(p);
    }

    // One row per point: the basis functions at that point's parameter.
    std::vector<std::vector<double>> a(n, std::vector<double>(n, 0.0));
    std::vector<Weighted> rhs(n);
    double basis[kMaxSplineDegree + 1];

    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t span = find_span(p, knots, n, u[i]);
        basis_functions(span, u[i], static_cast<int>(p), knots, basis);
        for (std::size_t k = 0; k <= pp; ++k) a[i][span - pp + k] = basis[k];
        rhs[i] = Weighted{through[i], 1.0};
    }

    if (!solve_dense(a, rhs)) return nullptr;

    std::vector<Vec3> control;
    control.reserve(n);
    for (const Weighted& w : rhs) control.push_back(w.p);

    auto out = std::make_unique<Spline>(p, std::move(control), std::move(knots),
                                        std::vector<double>{}, normal);
    // Kept so a grip can move a point the user actually chose and the curve be
    // re-solved, rather than handing them control points they never asked for.
    out->set_fit_points(through);
    return out;
}

void Spline::refit() {
    if (fit_.size() < 2) return;
    EntityPtr solved = interpolating(fit_, degree_, props().normal);
    if (!solved) return;

    const Spline& s = static_cast<const Spline&>(*solved);
    degree_ = s.degree_;
    control_ = s.control_;
    knots_ = s.knots_;
    weights_.clear();  // an interpolated curve is never rational
}

EntityPtr Spline::clone() const {
    auto copy = std::make_unique<Spline>(degree_, control_, knots_, weights_, props().normal);
    copy->fit_ = fit_;
    copy_common_to(*copy);
    return copy;
}

void Spline::transform(const Mat4& m) {
    // A NURBS curve transforms by transforming its control points, exactly, for
    // any affine map -- the basis functions are in parameter space and do not
    // move. That is the property that makes this the cheapest transform in the
    // kernel rather than a resampling, and it holds for the rational case too
    // because the weights are unaffected.
    for (Vec3& c : control_) c = m.transform_point(c);
    for (Vec3& f : fit_) f = m.transform_point(f);

    const Vec3 n = m.transform_vector(props().normal);
    if (!is_zero(n)) props().normal = normalize(n);
}

BBox Spline::bbox() const {
    BBox box;
    if (!valid()) return box;

    // The control polygon is the convex hull of the curve, so its box contains
    // the curve outright -- no sampling, and no risk of stepping over a bulge
    // between samples. Looser than the true extent, which a bounding box is
    // allowed to be.
    for (const Vec3& c : control_) box.expand(c);
    return box;
}

void Spline::osnap_points(std::vector<OsnapPoint>& out) const {
    if (!valid()) return;

    out.push_back({start_point(), OsnapType::Endpoint});
    out.push_back({end_point(), OsnapType::Endpoint});

    // Fit points are NODE snaps: they are points the user chose and the curve
    // demonstrably passes through, which is exactly what NODE means. Control
    // points are not offered -- the curve does not generally touch them, and a
    // snap to somewhere the geometry is not would be a lie.
    for (const Vec3& f : fit_) out.push_back({f, OsnapType::Node});

    // The midpoint is by PARAMETER, not by arc length. R12's MID on a polyline
    // is the same approximation and for the same reason: arc length needs a
    // numerical integral per query, and no snap is worth that.
    out.push_back({point_at((domain_min() + domain_max()) * 0.5), OsnapType::Midpoint});
}

void Spline::draw(const DrawContext& ctx, Renderer& r) const {
    if (!valid()) return;

    const int segments = segment_count(ctx.chord_tolerance);
    if (segments <= 0) return;

    const double lo = domain_min();
    const double hi = domain_max();

    std::vector<Vec3> pts;
    pts.reserve(static_cast<std::size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
        pts.push_back(point_at(lo + (hi - lo) * (static_cast<double>(i) / segments)));
    }
    r.polyline(pts.data(), pts.size(), false);
}

void Spline::dxf_write(DxfWriter& w) const {
    if (!valid()) return;

    // R2000 has a real SPLINE, so the curve goes out as itself: degree, knots,
    // control points and weights. Around 500 bytes where the R12 tessellation
    // below takes 1,400, and nothing is lost -- which is the whole reason a
    // later version is worth offering.
    if (dxf_has_modern_entities(w.version())) {
        w.write_common(*this);
        w.write_extrusion(props().normal);

        // Bit 8 is planar, which every spline this program makes is.
        int flags = 8;
        if (is_closed()) flags |= 1;
        if (is_rational()) flags |= 4;
        w.code(70, flags);
        w.code(71, degree_);
        w.code(72, static_cast<int>(knots_.size()));
        w.code(73, static_cast<int>(control_.size()));
        w.code(74, static_cast<int>(fit_.size()));

        for (const double k : knots_) w.code(40, k);
        for (const double wt : weights_) w.code(41, wt);
        for (const Vec3& c : control_) w.point(10, c);
        // Fit points travel too when there are any: they are what the user
        // chose, and a caller that made the curve from them gets them back.
        for (const Vec3& f : fit_) w.point(11, f);
        return;
    }

    // The same bargain Ellipse makes, and a costlier one: AC1009 has no SPLINE,
    // so this writes the tessellation. A round trip through DXF turns the curve
    // into the polyline R12 would have stored, and nothing recovers the knots.
    // SF_todo.md records AC1015 as the eventual answer.
    const int segments = segment_count(0.0);
    if (segments <= 0) return;

    const double lo = domain_min();
    const double hi = domain_max();
    const Mat4 to_ecs = world_to_ecs(props().normal);

    w.write_common_as(*this, "POLYLINE");
    w.code(66, 1);
    Vec3 elevation{0.0, 0.0, 0.0};
    elevation.z = to_ecs.transform_point(point_at(lo)).z;
    w.point(10, elevation);
    w.write_extrusion(props().normal);

    for (int i = 0; i <= segments; ++i) {
        const Vec3 p = point_at(lo + (hi - lo) * (static_cast<double>(i) / segments));
        w.code(0, "VERTEX");
        w.code(8, w.layer_name(*this));
        w.point(10, to_ecs.transform_point(p));
    }

    w.code(0, "SEQEND");
    w.code(8, w.layer_name(*this));
}

}  // namespace ncad
