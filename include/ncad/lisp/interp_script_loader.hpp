// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The concrete ScriptLoader (ncad/script_loader.hpp) a command actually gets
// handed: a thin wrapper over Interp::eval_string, sharing the same
// load_lisp_file used by (load ...) and by `ncad`'s own startup file loading
// -- see file_subrs.hpp. This is the one file allowed to know both what a
// ScriptLoader is and what an Interp is; that is the whole reason it exists
// rather than CommandContext holding an Interp* directly.
#pragma once

#include "ncad/lisp/eval.hpp"
#include "ncad/script_loader.hpp"

namespace ncad::lisp {

class InterpScriptLoader final : public ScriptLoader {
public:
    explicit InterpScriptLoader(Interp& in) : in_(in) {}

    bool load_file(const std::string& path, std::string& message) override;

private:
    Interp& in_;
};

}  // namespace ncad::lisp
