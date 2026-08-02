// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "ncad/lisp/interp_script_loader.hpp"

#include "file_subrs.hpp"

namespace ncad::lisp {

bool InterpScriptLoader::load_file(const std::string& path, std::string& message) {
    const LoadResult r = load_lisp_file(in_, path);
    if (!r.ok) {
        message = r.message;
        return false;
    }
    return true;
}

}  // namespace ncad::lisp
