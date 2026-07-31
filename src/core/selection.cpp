// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/selection.hpp"

#include <algorithm>

namespace ncad {

bool SelectionRegion::contains(const Vec3& p) const {
    const Vec3 d = p - origin;
    // Depth deliberately dropped: only the two in-plane components are tested,
    // so the rectangle behaves like the screen-space box it was dragged as.
    const double u = dot(d, ax);
    const double v = dot(d, ay);
    return u >= 0.0 && u <= width && v >= 0.0 && v <= height;
}

bool SelectionSet::add(Handle h) {
    if (h == kNullHandle) return false;
    if (contains(h)) return false;
    handles_.push_back(h);
    return true;
}

bool SelectionSet::remove(Handle h) {
    const auto it = std::find(handles_.begin(), handles_.end(), h);
    if (it == handles_.end()) return false;
    handles_.erase(it);
    return true;
}

bool SelectionSet::contains(Handle h) const {
    return std::find(handles_.begin(), handles_.end(), h) != handles_.end();
}

void SelectionSet::clear() {
    handles_.clear();
    has_region_ = false;
}

void SelectionSet::set_region(const SelectionRegion& r) {
    region_ = r;
    has_region_ = true;
}

}  // namespace ncad
