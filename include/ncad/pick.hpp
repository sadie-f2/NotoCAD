// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Entity hit-testing: which entity is the cursor pointing at.
//
// The answer is in pixels, not world units, because that is the question a
// pick box asks. PICKBOX is a screen-space tolerance and stays the same size
// however far you zoom, so a world-space distance would mean something
// different at every magnification.
//
// The measurement is taken against the entity's *flattened wireframe* -- the
// same polylines the renderer draws -- rather than against per-type analytic
// geometry. render.hpp already argued for that split, and the numbers make it
// safe: DrawContext::chord_tolerance is half a pixel of sag, while PICKBOX
// defaults to three, so the flattening error sits an order of magnitude below
// the tolerance it is measured against. You pick exactly what you can see. It
// also needs no switch on EntityType, so POLYLINE, TEXT and the mesh entities
// are picked correctly the day they can be drawn.
//
// Because it measures the wireframe, an unfilled circle is not picked at its
// centre. That is R12's behaviour and it is deliberate.
//
// There is no spatial index behind this: pick_entity is linear in the drawing.
// The broad phase is eight projections and a rectangle test, which holds to
// thousands of entities. An index slots in later without changing either
// signature here.
#pragma once

#include <cstdint>

#include "ncad/entity.hpp"
#include "ncad/selection.hpp"
#include "ncad/viewport.hpp"

namespace ncad {

class Database;

// Pixel distance from `cursor` to the entity's projected wireframe. False when
// the entity projects to nothing measurable -- degenerate geometry, or a
// viewport that cannot produce finite coordinates.
bool entity_pick_distance(const Entity& e, const Viewport& vp, const ScreenPoint& cursor,
                          double* out_px);

struct PickResult {
    Handle entity{kNullHandle};
    double distance_px{0.0};

    bool hit() const { return entity != kNullHandle; }
};

// The topmost visible entity within `pickbox_px` of the cursor.
//
// Topmost, not nearest: the search runs backwards through the drawing order, so
// the last entity drawn wins. Nearest-wins would be ambiguous for coincident
// geometry and would make pick cycling incoherent once it exists.
//
// Entities on layers that are off or frozen are never returned. Locked layers
// are pickable -- R12 allows the selection and refuses the modification.
PickResult pick_entity(const Database& db, const Viewport& vp, const ScreenPoint& cursor,
                       double pickbox_px);

// Broad phase, exposed because the osnap search needs the same test against a
// different tolerance. The screen-space bounds of the entity's world bounding
// box, inflated by `pad_px`.
//
// Conservative: the projection is affine, so the projected corners bound the
// projection of everything inside the box. False for an entity with no valid
// bbox.
bool entity_screen_box(const Entity& e, const Viewport& vp, double pad_px, double* min_x,
                       double* min_y, double* max_x, double* max_y);

// Whether the cursor falls inside those inflated bounds. The cheap rejection
// every candidate search starts with.
bool entity_near_cursor(const Entity& e, const Viewport& vp, const ScreenPoint& cursor,
                        double pad_px);

// --- window and crossing selection ------------------------------------------
//
// Both work on the entity's flattened wireframe, like the pick does, so they
// agree with what is drawn and need no per-type geometry. Neither takes a
// Viewport: SelectionRegion already carries the frame it was dragged in.
//
// Window: every part of the entity lies inside the region. Crossing: any part
// does, whether by a point inside or by an edge cutting through. The difference
// is the whole difference between the two selection modes, and getting it
// backwards is the classic way to delete more than you meant to.
bool entity_within_region(const Entity& e, const DrawContext& ctx, const SelectionRegion& r);
bool entity_crosses_region(const Entity& e, const DrawContext& ctx, const SelectionRegion& r);

// Adds every visible entity the region selects. `crossing` picks which test.
// Returns how many were added.
std::size_t select_by_region(const Database& db, const DrawContext& ctx,
                             const SelectionRegion& r, bool crossing, SelectionSet& out);

// Removes them instead, for Remove mode.
std::size_t deselect_by_region(const Database& db, const DrawContext& ctx,
                               const SelectionRegion& r, bool crossing, SelectionSet& out);

}  // namespace ncad
