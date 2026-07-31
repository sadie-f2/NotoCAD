// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/pick.hpp"

#include "ncad/database.hpp"
#include "ncad/render.hpp"
#include "ncad/scene.hpp"

#include <cmath>
#include <limits>

namespace ncad {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

bool finite(const ScreenPoint& p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

// Squared distance from `p` to the segment `a`-`b`, in pixels. Squared
// throughout: the caller takes one square root at the end rather than one per
// segment, and a tessellated circle is hundreds of segments.
double segment_distance_sq(const ScreenPoint& p, const ScreenPoint& a, const ScreenPoint& b) {
    const double vx = b.x - a.x;
    const double vy = b.y - a.y;
    const double wx = p.x - a.x;
    const double wy = p.y - a.y;

    const double len_sq = vx * vx + vy * vy;
    double t = 0.0;
    if (len_sq > 0.0) {
        t = (wx * vx + wy * vy) / len_sq;
        // Clamped, so a segment is measured as a segment and not as the
        // infinite line through it.
        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    }

    const double dx = wx - t * vx;
    const double dy = wy - t * vy;
    return dx * dx + dy * dy;
}

// Drives Entity::draw and measures what comes out. One level under Renderer,
// alongside QPainterRenderer and the recording sink in the tests.
class PickProbe : public Renderer {
public:
    PickProbe(const Viewport& vp, const ScreenPoint& cursor) : vp_(vp), cursor_(cursor) {}

    // Styling is not this probe's business: an entity is picked by where it is,
    // not by what colour it would have been.
    void begin_entity(const EntityProps&) override {}

    void polyline(const Vec3* pts, std::size_t count, bool closed) override {
        if (count == 0) return;

        ScreenPoint prev = vp_.project(pts[0]);
        if (!finite(prev)) return;

        // A single point still has a distance to the cursor. Entities that
        // degenerate to a point should be pickable at it.
        if (count == 1) {
            consider(segment_distance_sq(cursor_, prev, prev));
            return;
        }

        for (std::size_t i = 1; i < count; ++i) {
            const ScreenPoint cur = vp_.project(pts[i]);
            if (!finite(cur)) return;
            consider(segment_distance_sq(cursor_, prev, cur));
            prev = cur;
        }

        if (closed) {
            const ScreenPoint first = vp_.project(pts[0]);
            if (finite(first)) consider(segment_distance_sq(cursor_, prev, first));
        }
    }

    bool measured() const { return best_sq_ < kInf; }
    double distance_px() const { return std::sqrt(best_sq_); }

private:
    void consider(double d_sq) {
        if (d_sq < best_sq_) best_sq_ = d_sq;
    }

    const Viewport& vp_;
    ScreenPoint cursor_;
    double best_sq_{kInf};
};

}  // namespace

bool entity_pick_distance(const Entity& e, const Viewport& vp, const ScreenPoint& cursor,
                          double* out_px) {
    PickProbe probe(vp, cursor);
    e.draw(vp.draw_context(), probe);
    if (!probe.measured()) return false;
    *out_px = probe.distance_px();
    return true;
}

bool entity_screen_box(const Entity& e, const Viewport& vp, double pad_px, double* min_x,
                       double* min_y, double* max_x, double* max_y) {
    const BBox b = e.bbox();
    if (!b.valid()) return false;

    double lo_x = kInf, lo_y = kInf, hi_x = -kInf, hi_y = -kInf;

    // All eight corners, not just min and max: under an orbited view the box's
    // screen extent is set by whichever corners happen to face the viewer.
    for (int i = 0; i < 8; ++i) {
        const Vec3 corner{(i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y,
                          (i & 4) ? b.max.z : b.min.z};
        const ScreenPoint sp = vp.project(corner);
        if (!finite(sp)) return false;
        lo_x = std::min(lo_x, sp.x);
        lo_y = std::min(lo_y, sp.y);
        hi_x = std::max(hi_x, sp.x);
        hi_y = std::max(hi_y, sp.y);
    }

    *min_x = lo_x - pad_px;
    *min_y = lo_y - pad_px;
    *max_x = hi_x + pad_px;
    *max_y = hi_y + pad_px;
    return true;
}

bool entity_near_cursor(const Entity& e, const Viewport& vp, const ScreenPoint& cursor,
                        double pad_px) {
    double lo_x = 0.0, lo_y = 0.0, hi_x = 0.0, hi_y = 0.0;
    if (!entity_screen_box(e, vp, pad_px, &lo_x, &lo_y, &hi_x, &hi_y)) return false;
    return cursor.x >= lo_x && cursor.x <= hi_x && cursor.y >= lo_y && cursor.y <= hi_y;
}

PickResult pick_entity(const Database& db, const Viewport& vp, const ScreenPoint& cursor,
                       double pickbox_px) {
    PickResult result;
    const std::vector<Handle>& order = db.order();

    // Backwards: the last entity drawn is the one on top, and it wins.
    for (std::size_t i = order.size(); i-- > 0;) {
        const Entity* e = db.get(order[i]);
        if (!e) continue;
        if (!entity_visible(db, *e)) continue;
        if (!entity_near_cursor(*e, vp, cursor, pickbox_px)) continue;

        double d = 0.0;
        if (!entity_pick_distance(*e, vp, cursor, &d)) continue;
        if (d > pickbox_px) continue;

        result.entity = order[i];
        result.distance_px = d;
        return result;
    }
    return result;
}


// --- window and crossing selection ------------------------------------------

namespace {

// Region coordinates: how far along the region's own two axes a world point
// lies. Depth is dropped, matching SelectionRegion::contains.
struct RegionUV {
    double u, v;
};

RegionUV region_uv(const SelectionRegion& r, const Vec3& p) {
    const Vec3 d = p - r.origin;
    return RegionUV{dot(d, r.ax), dot(d, r.ay)};
}

bool uv_inside(const SelectionRegion& r, const RegionUV& p) {
    return p.u >= 0.0 && p.u <= r.width && p.v >= 0.0 && p.v <= r.height;
}

// Does the segment a-b touch the axis-aligned rectangle [0,w] x [0,h]?
// Liang-Barsky: clip the segment against the four slabs and see whether any of
// it survives. Endpoints inside fall out of the same test.
bool segment_hits_rect(const SelectionRegion& r, const RegionUV& a, const RegionUV& b) {
    double t0 = 0.0, t1 = 1.0;
    const double dx = b.u - a.u;
    const double dy = b.v - a.v;

    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {a.u, r.width - a.u, a.v, r.height - a.v};

    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            // Parallel to this edge: outside it means no overlap at all.
            if (q[i] < 0.0) return false;
            continue;
        }
        const double t = q[i] / p[i];
        if (p[i] < 0.0) {
            if (t > t1) return false;
            if (t > t0) t0 = t;
        } else {
            if (t < t0) return false;
            if (t < t1) t1 = t;
        }
    }
    return t0 <= t1;
}

// Walks the flattened wireframe and answers both questions at once, so an
// entity is tessellated once however it is being tested.
class RegionProbe : public Renderer {
public:
    explicit RegionProbe(const SelectionRegion& r) : r_(r) {}

    void begin_entity(const EntityProps&) override {}

    void polyline(const Vec3* pts, std::size_t count, bool closed) override {
        if (count == 0) return;
        any_geometry_ = true;

        RegionUV prev = region_uv(r_, pts[0]);
        note_point(prev);
        if (count == 1) return;

        for (std::size_t i = 1; i < count; ++i) {
            const RegionUV cur = region_uv(r_, pts[i]);
            note_point(cur);
            if (segment_hits_rect(r_, prev, cur)) touches_ = true;
            prev = cur;
        }
        if (closed) {
            const RegionUV first = region_uv(r_, pts[0]);
            if (segment_hits_rect(r_, prev, first)) touches_ = true;
        }
    }

    bool within() const { return any_geometry_ && all_inside_; }
    bool crosses() const { return any_geometry_ && touches_; }

private:
    void note_point(const RegionUV& p) {
        if (uv_inside(r_, p)) {
            touches_ = true;
        } else {
            all_inside_ = false;
        }
    }

    const SelectionRegion& r_;
    bool any_geometry_{false};
    bool all_inside_{true};
    bool touches_{false};
};

bool region_hit(const Entity& e, const DrawContext& ctx, const SelectionRegion& r, bool crossing) {
    RegionProbe probe(r);
    e.draw(ctx, probe);
    return crossing ? probe.crosses() : probe.within();
}

std::size_t apply_region(const Database& db, const DrawContext& ctx, const SelectionRegion& r,
                         bool crossing, SelectionSet& out, bool add) {
    std::size_t n = 0;
    for (const Handle h : db.order()) {
        const Entity* e = db.get(h);
        if (!e || !entity_visible(db, *e)) continue;
        if (!region_hit(*e, ctx, r, crossing)) continue;
        if (add ? out.add(h) : out.remove(h)) ++n;
    }
    return n;
}

}  // namespace

bool entity_within_region(const Entity& e, const DrawContext& ctx, const SelectionRegion& r) {
    return region_hit(e, ctx, r, false);
}

bool entity_crosses_region(const Entity& e, const DrawContext& ctx, const SelectionRegion& r) {
    return region_hit(e, ctx, r, true);
}

std::size_t select_by_region(const Database& db, const DrawContext& ctx, const SelectionRegion& r,
                             bool crossing, SelectionSet& out) {
    return apply_region(db, ctx, r, crossing, out, true);
}

std::size_t deselect_by_region(const Database& db, const DrawContext& ctx,
                               const SelectionRegion& r, bool crossing, SelectionSet& out) {
    return apply_region(db, ctx, r, crossing, out, false);
}
}  // namespace ncad
