// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// How a command reaches the view.
//
// CommandContext held no view for a long time, deliberately, and this is what
// finally arrives in its place -- an interface rather than a Viewport. The
// distinction matters: `ncad` has no view at all, and handing it a dummy
// Viewport would be a lie that works right up until something reads one back.
// A null ViewControl is the truth, and PLAN in `ncad` says so.
//
// The Qt shell implements this over its real Viewport. Everything here is one
// or two lines there; the interface exists to keep Qt and the view classes out
// of the core's command surface, not because the operations are complicated.
//
// Note the two accessors at the bottom. They exist because some things are
// screen-space rather than world-space -- a selection window is dragged on the
// screen, and curve flattening tolerance depends on zoom -- and those callers
// need to ask the view rather than assume world XY.
#pragma once

#include "noto/ecs.hpp"  // Basis
#include "noto/render.hpp"
#include "noto/vec3.hpp"

namespace noto {

class ViewControl {
public:
    virtual ~ViewControl() = default;

    // PLAN. Look straight down `normal`, which is the current construction
    // plane's -- world Z until UCS exists.
    virtual void set_plan_view(const Vec3& normal) = 0;

    // ZOOM Extents, ZOOM Window, and ZOOM <scale>. A window is given as two
    // world points on the construction plane, not as a screen rectangle,
    // because the command collected them from point prompts.
    virtual void zoom_extents() = 0;
    virtual void zoom_window(const Vec3& a, const Vec3& b) = 0;
    virtual void zoom_scale(double factor) = 0;

    // ZOOM Previous. False when there is nothing to go back to.
    //
    // What is remembered is the whole view state, not a zoom rectangle: whether
    // a change of view direction should be undone by Previous is a policy
    // question, and storing only an extent would answer it permanently and by
    // accident.
    virtual bool zoom_previous() = 0;

    // PAN, as a displacement between two world points.
    virtual void pan(const Vec3& from, const Vec3& to) = 0;

    // The screen frame: ax is right, ay is up, az points at the eye. A
    // selection window is screen-aligned, so it is built on these rather than
    // on world axes.
    virtual Basis view_basis() const = 0;

    // Flattening tolerance for anything that has to agree with what is drawn.
    virtual DrawContext draw_context() const = 0;
};

}  // namespace noto
