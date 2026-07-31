// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The symbol tables, as AutoLISP reads them.
//
// `tblsearch` answers "is there a layer called this, and what is it?" and
// `tblnext` walks a table entry by entry. Between them they are how a script
// finds out what a drawing already contains before adding to it -- checking a
// layer exists before drawing on it, or listing the blocks available to insert.
//
// Entries come back as alists in the same shape `entget` uses, because a script
// that can read one can then read the other without learning a second form.
//
// TABLES THAT EXIST HERE: LAYER, LTYPE, BLOCK, UCS. R12 also has VIEW, STYLE,
// VPORT, APPID and DIMSTYLE; none of those has a table in this program yet, so
// naming one is an error rather than an empty walk -- "no such table" and "an
// empty table" are different answers and a script can act on the difference.
#pragma once

#include "ncad/lisp/eval.hpp"

namespace ncad::lisp {

// (tblsearch "LAYER" "WALLS" [setnext]) -> alist, or nil if there is no such
// entry. A non-nil third argument makes the next (tblnext ...) continue from
// what was found, which is R12's way of starting a walk in the middle.
bool subr_tblsearch(Interp& in, const Value* args, std::size_t n, Value& out);

// (tblnext "LAYER" [rewind]) -> the next entry's alist, or nil at the end.
// A non-nil second argument restarts from the first entry.
bool subr_tblnext(Interp& in, const Value* args, std::size_t n, Value& out);

}  // namespace ncad::lisp
