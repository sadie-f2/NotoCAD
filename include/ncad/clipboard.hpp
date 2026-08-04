// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The clipboard: COPYCLIP's transport, and the fragment it carries.
//
// The payload is a complete DXF document, not a private format. That is the
// whole design: the paste side is read_dxf_text in Merge mode, which already
// existed for DXFIN, so pasting is importing -- table entries merge by name
// with the drawing's own definition winning, handles cannot collide, and the
// caller's undo group makes the paste one step. It also means the clipboard is
// ordinary text: pasting into a text editor shows a DXF file, and any DXF on
// the clipboard from elsewhere pastes as geometry.
//
// The base point rides in $INSBASE, which is what the header variable is FOR:
// the point a drawing is inserted by. WBLOCK already uses it the same way.
//
// The transport is an interface for the same reason ViewControl is: the core
// cannot see Qt. The Qt shell hands the engine a QClipboard-backed one, so copy
// and paste cross windows and applications; `ncad` and the tests use the
// in-process one, a clipboard whose reach is the session.
#pragma once

#include "ncad/entity.hpp"
#include "ncad/vec3.hpp"

#include <string>
#include <vector>

namespace ncad {

class Database;

class Clipboard {
public:
    virtual ~Clipboard() = default;

    virtual bool set_text(const std::string& text) = 0;

    // False when the clipboard is empty or holds something that is not text.
    virtual bool get_text(std::string& out) const = 0;
};

// The session-local fallback: a clipboard whose reach is this process.
class InProcessClipboard final : public Clipboard {
public:
    bool set_text(const std::string& text) override {
        text_ = text;
        has_text_ = true;
        return true;
    }
    bool get_text(std::string& out) const override {
        if (!has_text_) return false;
        out = text_;
        return true;
    }

private:
    std::string text_;
    bool has_text_{false};
};

// The DXF document COPYCLIP puts on the clipboard: the given entities, every
// block definition an INSERT among them references (nested ones included), and
// the layers and linetypes any of that names -- a fragment that stands alone as
// a drawing. `base` becomes its $INSBASE.
//
// Written at R2000, not R12, deliberately: the fragment's one reader is this
// program, and AC1015 can NAME what the database holds -- a copied spline stays
// a spline instead of arriving as its tessellation. The R12 interchange
// guarantee is about files a drawing is saved to, and this is not one.
std::string clip_fragment(const Database& db, const std::vector<Handle>& handles,
                          const Vec3& base);

// Reads $INSBASE out of a DXF fragment's header, for the paste side. Merge
// reads deliberately leave the header alone -- the drawing being pasted into
// owns its own header -- so the base point is scanned out separately here.
// Returns the origin when the text carries no $INSBASE, which makes a fragment
// from elsewhere paste by its own coordinates.
Vec3 clip_base(const std::string& text);

// True when the text looks like a DXF document -- the question the Qt shell
// asks to decide whether Cmd-V means geometry or characters. A group-code
// scan, not a parse: the honest answer to "could paste read this" without
// reading it twice.
bool clip_looks_like_dxf(const std::string& text);

}  // namespace ncad
