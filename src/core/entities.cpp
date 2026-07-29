// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/entities.hpp"

#include "noto/ecs.hpp"

#include <cmath>
#include <cstddef>
#include <numbers>
#include <string_view>

namespace noto {
namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

double normalize_angle(double a) {
    a = std::fmod(a, kTwoPi);
    return (a < 0.0) ? a + kTwoPi : a;
}

// Angle of a world-space offset from the entity centre, measured in the
// entity's own plane.
double angle_in_basis(const Vec3& offset, const Basis& b) {
    return normalize_angle(std::atan2(dot(offset, b.ay), dot(offset, b.ax)));
}

// Transforms a circular entity's frame. Returns the new centre, normal and
// radius. The normal is rebuilt from the transformed basis vectors rather than
// by transforming the normal directly, so that a mirroring transform correctly
// flips the plane's orientation and the counterclockwise sense is preserved.
struct TransformedFrame {
    Vec3 center;
    Vec3 normal;
    double radius;
};

TransformedFrame transform_frame(const Mat4& m, const Vec3& center, const Vec3& normal,
                                 double radius) {
    const Basis b = arbitrary_axis(normal);
    const Vec3 new_center = m.transform_point(center);
    const Vec3 tx = m.transform_vector(b.ax);
    const Vec3 ty = m.transform_vector(b.ay);

    Vec3 new_normal = normalize(cross(tx, ty));
    if (is_zero(new_normal)) new_normal = normalize(normal);

    // Uniform scale is assumed: R12 has no ELLIPSE entity, so a non-uniform
    // scale cannot be represented and is approximated by the X-axis factor.
    const double new_radius = radius * length(tx);
    return {new_center, new_normal, new_radius};
}

// Exact axis-aligned extent of a full circle lying in an arbitrary plane: along
// each world axis the circle reaches radius * sqrt(1 - n_i^2).
BBox circle_bbox(const Vec3& center, double radius, const Vec3& normal) {
    const Vec3 n = normalize(normal);
    const Vec3 ext{radius * std::sqrt(std::max(0.0, 1.0 - n.x * n.x)),
                   radius * std::sqrt(std::max(0.0, 1.0 - n.y * n.y)),
                   radius * std::sqrt(std::max(0.0, 1.0 - n.z * n.z))};
    BBox box;
    box.expand(center - ext);
    box.expand(center + ext);
    return box;
}

}  // namespace

const char* entity_type_name(EntityType t) {
    switch (t) {
        case EntityType::Line: return "LINE";
        case EntityType::Circle: return "CIRCLE";
        case EntityType::Arc: return "ARC";
        case EntityType::Ellipse: return "ELLIPSE";
        case EntityType::Spline: return "SPLINE";
        case EntityType::Polyline: return "POLYLINE";
        case EntityType::Text: return "TEXT";
        case EntityType::Point: return "POINT";
        case EntityType::Solid: return "SOLID";
        case EntityType::Face3d: return "3DFACE";
        case EntityType::Insert: return "INSERT";
        case EntityType::Proxy: return "PROXY";
    }
    return "UNKNOWN";
}

const char* osnap_name(OsnapType t) {
    switch (t) {
        case OsnapType::Endpoint: return "END";
        case OsnapType::Midpoint: return "MID";
        case OsnapType::Center: return "CEN";
        case OsnapType::Quadrant: return "QUA";
        case OsnapType::Node: return "NOD";
        case OsnapType::Insert: return "INS";
        case OsnapType::Perpendicular: return "PER";
        case OsnapType::Tangent: return "TAN";
        case OsnapType::Nearest: return "NEA";
        case OsnapType::Intersection: return "INT";
    }
    return "???";
}

const char* grip_kind_name(GripKind k) {
    switch (k) {
        case GripKind::Stretch: return "STRETCH";
        case GripKind::Move: return "MOVE";
        case GripKind::Radius: return "RADIUS";
    }
    return "???";
}

namespace {

struct OsnapNameRow {
    const char* abbrev;  // what R12 shows, and what people type
    const char* full;
    OsnapMask bit;
};

// NON has no bit: it is the absence of every snap, and is handled by the caller
// through the parse succeeding with an empty mask.
constexpr OsnapNameRow kOsnapNames[] = {
    {"END", "ENDPOINT", kOsnapEndpoint},         {"MID", "MIDPOINT", kOsnapMidpoint},
    {"CEN", "CENTER", kOsnapCenter},             {"NOD", "NODE", kOsnapNode},
    {"QUA", "QUADRANT", kOsnapQuadrant},         {"INT", "INTERSECTION", kOsnapIntersection},
    {"INS", "INSERT", kOsnapInsert},             {"PER", "PERPENDICULAR", kOsnapPerpendicular},
    {"TAN", "TANGENT", kOsnapTangent},           {"NEA", "NEAREST", kOsnapNearest},
    {"QUI", "QUICK", kOsnapQuick},
};

char upcase(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

bool is_prefix_of(std::string_view token, const char* word) {
    for (std::size_t i = 0; i < token.size(); ++i) {
        if (word[i] == '\0' || upcase(token[i]) != word[i]) return false;
    }
    return true;
}

// One token to one bit. False if it matches nothing, or -- deliberately -- if it
// matches more than one snap, since silently picking one of two would put the
// cursor somewhere the user did not ask for.
bool osnap_token(std::string_view token, OsnapMask* out, bool* is_none) {
    // Three characters minimum. R12's snap names are three-letter codes and
    // nothing shorter is one, which also keeps them clear of single-letter
    // command keywords -- "C" at a point prompt offering Close and Center is
    // an ambiguous keyword, and must stay one rather than becoming CEN.
    if (token.size() < 3) return false;

    if (is_prefix_of(token, "NONE")) {
        *is_none = true;
        return true;
    }

    OsnapMask found = kOsnapNone;
    int matches = 0;
    for (const OsnapNameRow& r : kOsnapNames) {
        if (is_prefix_of(token, r.abbrev) || is_prefix_of(token, r.full)) {
            if (found != r.bit) ++matches;
            found = r.bit;
        }
    }
    if (matches != 1) return false;
    *out = found;
    return true;
}

}  // namespace

bool parse_osnap_mask(std::string_view text, OsnapMask* out) {
    OsnapMask mask = kOsnapNone;
    bool any = false;
    bool none_seen = false;

    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ',' || text[i] == ' ' || text[i] == '\t')) ++i;
        const std::size_t start = i;
        while (i < text.size() && text[i] != ',' && text[i] != ' ' && text[i] != '\t') ++i;
        if (i == start) break;

        OsnapMask bit = kOsnapNone;
        bool is_none = false;
        if (!osnap_token(text.substr(start, i - start), &bit, &is_none)) return false;
        if (is_none) {
            none_seen = true;
        } else {
            mask = static_cast<OsnapMask>(mask | bit);
        }
        any = true;
    }

    if (!any) return false;
    // NONE anywhere in the list wins: "cen,none" is a contradiction, and the
    // safe reading of a contradiction is to snap to nothing.
    *out = none_seen ? kOsnapNone : mask;
    return true;
}

// Written out rather than shifted: R12's OSMODE bit order is not this enum's
// declaration order. See the comment in osnap.hpp.
OsnapMask osnap_bit(OsnapType t) {
    switch (t) {
        case OsnapType::Endpoint: return kOsnapEndpoint;
        case OsnapType::Midpoint: return kOsnapMidpoint;
        case OsnapType::Center: return kOsnapCenter;
        case OsnapType::Quadrant: return kOsnapQuadrant;
        case OsnapType::Node: return kOsnapNode;
        case OsnapType::Insert: return kOsnapInsert;
        case OsnapType::Perpendicular: return kOsnapPerpendicular;
        case OsnapType::Tangent: return kOsnapTangent;
        case OsnapType::Nearest: return kOsnapNearest;
        case OsnapType::Intersection: return kOsnapIntersection;
    }
    return kOsnapNone;
}

bool osnap_enabled(OsnapMask mask, OsnapType t) {
    const OsnapMask bit = osnap_bit(t);
    return bit != kOsnapNone && (mask & bit) != 0;
}

bool osnap_type_from_bit(OsnapMask bit, OsnapType* out) {
    switch (bit) {
        case kOsnapEndpoint: *out = OsnapType::Endpoint; return true;
        case kOsnapMidpoint: *out = OsnapType::Midpoint; return true;
        case kOsnapCenter: *out = OsnapType::Center; return true;
        case kOsnapNode: *out = OsnapType::Node; return true;
        case kOsnapQuadrant: *out = OsnapType::Quadrant; return true;
        case kOsnapIntersection: *out = OsnapType::Intersection; return true;
        case kOsnapInsert: *out = OsnapType::Insert; return true;
        case kOsnapPerpendicular: *out = OsnapType::Perpendicular; return true;
        case kOsnapTangent: *out = OsnapType::Tangent; return true;
        case kOsnapNearest: *out = OsnapType::Nearest; return true;
        default: return false;
    }
}

// --- Line -------------------------------------------------------------------

EntityPtr Line::clone() const {
    auto copy = std::make_unique<Line>(start_, end_);
    copy_common_to(*copy);
    return copy;
}

void Line::transform(const Mat4& m) {
    start_ = m.transform_point(start_);
    end_ = m.transform_point(end_);
}

BBox Line::bbox() const {
    BBox box;
    box.expand(start_);
    box.expand(end_);
    return box;
}

void Line::osnap_points(std::vector<OsnapPoint>& out) const {
    out.push_back({start_, OsnapType::Endpoint});
    out.push_back({end_, OsnapType::Endpoint});
    out.push_back({midpoint(), OsnapType::Midpoint});
}

// --- Circle -----------------------------------------------------------------

void Circle::quadrants(Vec3 out[4]) const {
    const Basis b = arbitrary_axis(props().normal);
    out[0] = center_ + b.ax * radius_;
    out[1] = center_ + b.ay * radius_;
    out[2] = center_ - b.ax * radius_;
    out[3] = center_ - b.ay * radius_;
}

EntityPtr Circle::clone() const {
    auto copy = std::make_unique<Circle>(center_, radius_, props().normal);
    copy_common_to(*copy);
    return copy;
}

void Circle::transform(const Mat4& m) {
    const TransformedFrame f = transform_frame(m, center_, props().normal, radius_);
    center_ = f.center;
    radius_ = f.radius;
    props().normal = f.normal;
}

BBox Circle::bbox() const { return circle_bbox(center_, radius_, props().normal); }

void Circle::osnap_points(std::vector<OsnapPoint>& out) const {
    out.push_back({center_, OsnapType::Center});
    Vec3 q[4];
    quadrants(q);
    for (const Vec3& p : q) out.push_back({p, OsnapType::Quadrant});
}

// --- Ellipse ----------------------------------------------------------------

Vec3 Ellipse::minor_axis() const {
    // Perpendicular to the major axis IN THE ENTITY'S PLANE, which is what the
    // normal is for: cross with it rather than picking an arbitrary
    // perpendicular, or a tilted ellipse would get a minor axis out of plane.
    const Vec3 perp = cross(props().normal, major_);
    if (is_zero(perp)) return Vec3{};
    return normalize(perp) * (length(major_) * ratio_);
}

double Ellipse::sweep() const {
    const double d = normalize_angle(end_param_ - start_param_);
    // Start and end coinciding means a whole ellipse, the same reading Arc
    // gives a zero sweep.
    return (d < kEps) ? kTwoPi : d;
}

bool Ellipse::is_full() const { return sweep() >= kTwoPi - kEps; }

Vec3 Ellipse::point_at(double param) const {
    return center_ + major_ * std::cos(param) + minor_axis() * std::sin(param);
}

void Ellipse::axis_points(Vec3 out[4]) const {
    const Vec3 minor = minor_axis();
    out[0] = center_ + major_;
    out[1] = center_ + minor;
    out[2] = center_ - major_;
    out[3] = center_ - minor;
}

EntityPtr Ellipse::clone() const {
    auto copy = std::make_unique<Ellipse>(center_, major_, ratio_, start_param_, end_param_,
                                          props().normal);
    copy_common_to(*copy);
    return copy;
}

void Ellipse::transform(const Mat4& m) {
    // Both axis vectors go through, and the frame is rebuilt from what comes
    // out. Transforming the centre and the normal separately would be wrong
    // under a mirror, where the normal has to follow the handedness of the axes
    // rather than be carried along.
    const Vec3 c = m.transform_point(center_);
    const Vec3 a = m.transform_point(center_ + major_) - c;
    const Vec3 b = m.transform_point(center_ + minor_axis()) - c;

    center_ = c;
    if (is_zero(a) || is_zero(b)) return;

    major_ = a;
    ratio_ = length(b) / length(a);

    const Vec3 n = cross(a, b);
    if (!is_zero(n)) props().normal = normalize(n);

    // The parameters are untouched, and that is only correct because every
    // transform this program can produce is a similarity: MOVE, ROTATE, MIRROR
    // and a UNIFORM SCALE. A non-uniform scale would turn the major axis into
    // something that is no longer the major axis and shift the parameters with
    // it, and R12 has no command that produces one.
}

BBox Ellipse::bbox() const {
    BBox box;
    const Vec3 minor = minor_axis();

    if (is_full()) {
        // Exact, and cheaper than sampling: for p(t) = c + u*cos t + v*sin t
        // the extent along any axis is sqrt(u_i^2 + v_i^2).
        const Vec3 half{std::sqrt(major_.x * major_.x + minor.x * minor.x),
                        std::sqrt(major_.y * major_.y + minor.y * minor.y),
                        std::sqrt(major_.z * major_.z + minor.z * minor.z)};
        box.expand(center_ - half);
        box.expand(center_ + half);
        return box;
    }

    // A partial ellipse could be solved the same way per axis, but the stationary
    // points have to be clipped to the span and there are six of them. Sampling
    // is a few lines instead of forty and a bounding box has no need to be tight
    // to the last ULP -- only to contain the curve, which this does.
    constexpr int kSamples = 64;
    const double s = sweep();
    for (int i = 0; i <= kSamples; ++i) {
        box.expand(point_at(start_param_ + s * (static_cast<double>(i) / kSamples)));
    }
    return box;
}

void Ellipse::osnap_points(std::vector<OsnapPoint>& out) const {
    out.push_back({center_, OsnapType::Center});

    Vec3 q[4];
    axis_points(q);
    for (const Vec3& p : q) out.push_back({p, OsnapType::Quadrant});

    // An elliptical arc has ends; a whole one does not, and offering its start
    // as an endpoint would put an ENDPOINT snap somewhere the curve does not
    // stop.
    if (!is_full()) {
        out.push_back({start_point(), OsnapType::Endpoint});
        out.push_back({end_point(), OsnapType::Endpoint});
    }
}

// --- Arc --------------------------------------------------------------------

double Arc::sweep() const {
    const double d = normalize_angle(end_angle_ - start_angle_);
    // A zero sweep means start and end coincide, which R12 treats as a full turn.
    return (d < kEps) ? kTwoPi : d;
}

Vec3 Arc::point_at_angle(double angle) const {
    const Basis b = arbitrary_axis(props().normal);
    return center_ + (b.ax * std::cos(angle) + b.ay * std::sin(angle)) * radius_;
}

bool Arc::contains_angle(double angle) const {
    return normalize_angle(angle - start_angle_) <= sweep() + kEps;
}

EntityPtr Arc::clone() const {
    auto copy = std::make_unique<Arc>(center_, radius_, start_angle_, end_angle_, props().normal);
    copy_common_to(*copy);
    return copy;
}

void Arc::transform(const Mat4& m) {
    // Capture the endpoints in world space before the frame moves, then re-derive
    // the angles in the new plane. Doing it this way keeps mirrored arcs correct:
    // the flipped normal reverses the sense, and the endpoints follow.
    const Vec3 p_start = start_point();
    const Vec3 p_end = end_point();

    const TransformedFrame f = transform_frame(m, center_, props().normal, radius_);
    center_ = f.center;
    radius_ = f.radius;
    props().normal = f.normal;

    const Basis nb = arbitrary_axis(f.normal);
    start_angle_ = angle_in_basis(m.transform_point(p_start) - center_, nb);
    end_angle_ = angle_in_basis(m.transform_point(p_end) - center_, nb);
}

BBox Arc::bbox() const {
    BBox box;
    box.expand(start_point());
    box.expand(end_point());

    // Only the quadrant points actually inside the sweep can extend the box.
    for (int i = 0; i < 4; ++i) {
        const double a = static_cast<double>(i) * (kTwoPi / 4.0);
        if (contains_angle(a)) box.expand(point_at_angle(a));
    }
    return box;
}

void Arc::osnap_points(std::vector<OsnapPoint>& out) const {
    out.push_back({start_point(), OsnapType::Endpoint});
    out.push_back({end_point(), OsnapType::Endpoint});
    out.push_back({midpoint(), OsnapType::Midpoint});
    out.push_back({center_, OsnapType::Center});
    for (int i = 0; i < 4; ++i) {
        const double a = static_cast<double>(i) * (kTwoPi / 4.0);
        if (contains_angle(a)) out.push_back({point_at_angle(a), OsnapType::Quadrant});
    }
}

}  // namespace noto
