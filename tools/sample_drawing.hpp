// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// The sample drawing, shared by gen_sample and the viewer.
//
// Both need the same geometry for the same reason: this is the project's
// correctness gate. gen_sample writes it out for another CAD application to
// check, and the viewer draws it directly. Two independent renderings of one
// database disagreeing is exactly the signal worth having, and it is only a
// signal if the content is identical.
//
// Deliberately not in the core: it is demonstration content, not kernel.
#pragma once

namespace ncad {
class Database;
}

// Fills `db` with entities on tilted planes -- the cases the arbitrary axis
// algorithm gets wrong if it is wrong at all.
void build_sample_drawing(ncad::Database& db);
