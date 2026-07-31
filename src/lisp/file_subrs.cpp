// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Drawing file I/O exposed to AutoLISP.
//
// In R12 DXFOUT is a command, not a LISP function, and would normally be
// reached through (command "DXFOUT" ...). Commands are resumable state machines
// that do not exist yet, and waiting for them would mean the interpreter could
// build a drawing but never save one. So dxfout is a function for now. When the
// command layer lands it becomes a thin wrapper over this, and the LISP-visible
// name can stay.
#include "file_subrs.hpp"

#include "ncad/database.hpp"
#include "ncad/dxf.hpp"

#include <string>

namespace ncad::lisp {

// (dxfout "path") -> T on success, nil if the file could not be written.
//
// A path that cannot be opened is a condition a script can reasonably test for
// -- a read-only directory, a full disk -- so it returns nil rather than
// raising. An argument of the wrong type is a bug in the caller, and errors.
bool subr_dxfout(Interp& in, const Value* args, std::size_t, Value& out) {
    Database* db = in.database();
    if (!db) return in.fail(EvalStatus::BadArgumentType, "dxfout: no drawing is attached");

    if (args[0].type != Type::Str) {
        return in.fail(EvalStatus::BadArgumentType,
                       "dxfout: path is not a string: " + prin1(args[0]));
    }

    const std::string path(args[0].str->view());
    out = write_dxf_file(*db, path) ? make_true() : make_nil();
    return true;
}

// (quit) / (exit). Not an error: the host decides whether to stop.
bool subr_quit(Interp& in, const Value*, std::size_t, Value& out) {
    in.request_quit();
    out = make_nil();
    return true;
}

}  // namespace ncad::lisp
