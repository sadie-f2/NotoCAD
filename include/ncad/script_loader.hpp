// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// How a command reaches the interpreter.
//
// CommandContext held no way to load AutoLISP for the same reason it held no
// view for a long time: an interface rather than the real Interp, so the
// interpreter stays out of the core's command surface the way Qt stays out of
// it behind ViewControl (view_control.hpp). Core does not, and must not,
// depend on ncad_lisp -- ncad_lisp depends on ncad_core, not the reverse, and
// a raw Interp* here would invert that.
//
// The concrete implementation lives in ncad_lisp, wrapping Interp::eval_string
// the same way `ncad`'s own startup file-loading already does, so APPLOAD and
// `ncad file.lsp` share one code path rather than two that can drift apart.
#pragma once

#include <string>

namespace ncad {

class ScriptLoader {
public:
    virtual ~ScriptLoader() = default;

    // Reads path and evaluates every form in it in the current environment,
    // exactly as if each had been typed at the prompt in turn -- a defun or a
    // setq in the file is visible afterward. Returns false on failure, having
    // filled message with why; whatever ran before the failing form has
    // already taken effect.
    virtual bool load_file(const std::string& path, std::string& message) = 0;
};

}  // namespace ncad
