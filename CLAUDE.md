# NotoCAD

An open-source, command-line-first CAD tool modeled on **AutoCAD R12** — specifically
the pre-GUI-heavy, pre-solids era. Not a modern AutoCAD clone.

Linux-first, with a macOS port as a stated goal.

## Decisions already made

These were settled deliberately; revisit them only with reason, not by drift.

**Language: C++20, restrained dialect.** Chosen over C because the project's real
content — polymorphic entities, containers, an interpreter with arena memory, a Qt
UI — is exactly the workload where C means hand-writing infrastructure first.
The restraint rules are enforced by the build (`-fno-rtti`, warnings-as-noise-free):

- Single-level inheritance only: one `Entity` base with virtuals, no hierarchies.
- No template metaprogramming. Templates for containers and allocators, nothing more.
- No RTTI / `dynamic_cast` — entities carry a type enum, which DXF needs regardless.
- Exceptions are enabled (Qt and the standard library want them) but are never
  control flow in the geometry kernel or AutoLISP hot paths; those return status codes.
- Geometry types stay trivially copyable and constexpr-friendly. Doubles throughout.

**File formats: DXF-first, DWG optional.** Native R12 DXF read/write lives in-tree —
the format is small, text, and fully documented, and it is the actual interchange
path for this project's workflow. LibreDWG is an *optional* compile-time module
(`-DNOTO_WITH_DWG=ON`) for DWG **import** only. This keeps GPLv3 out of the core and
avoids depending on LibreDWG's weaker R12 write path. DWG export is not planned.

Consequence: there is no `serialize_dwg` slot on entities. DWG I/O converts at the
boundary through LibreDWG's own structs.

**License: BSD-3-Clause.** Chosen over MIT for the no-endorsement clause and the
more explicit binary-redistribution terms. The README states that the license covers
documentation as well, since BSD-3 — unlike MIT — does not name documentation as
licensed subject matter.

Incorporating GPLv3 code makes the *distributed binary* GPLv3, not the project. That
is why DWG is a compile-time option rather than a dependency: the default build links
no GPL code and stays BSD-3, and only a `NOTO_WITH_DWG=ON` binary must be conveyed
under GPLv3. Permissive source flows into GPLv3 cleanly, so nothing is lost. This is
the wall FreeCAD and LibreCAD hit from the other side, being GPLv2.

Consequence: LibreDWG headers must not leak into core headers, and DWG lives in its
own build target so the linkage is visible in the build graph rather than inferred.

**Headless core first.** The core builds and tests with no GUI and no display. The
Qt6 shell comes later and stays thin: windowing, GL context, input events only,
behind a stable core API. Correctness is verified by opening emitted DXF in other
CAD software.

**Entity vtable.** `create / free / clone / transform(Mat4) / bbox / osnap_points /
dxf_write / dxf_read / draw`. `transform` is the important one — MOVE, COPY, SCALE,
MIRROR, ARRAY, ROTATE, ROTATE3D, ALIGN and block insertion all route through it.

**ECS / arbitrary axis is foundational, not a feature.** R12 stores CIRCLE, ARC, 2D
POLYLINE, TEXT and SOLID as 2D coordinates in their own entity coordinate system plus
an extrusion vector (DXF group 210). Without the Arbitrary Axis Algorithm in the
kernel from day one, any such entity not parallel to world XY serialises wrong, osnaps
land in the wrong space, and UCS has nowhere to live. See `include/noto/ecs.hpp`.

**Commands are resumable state machines.** Keyboard, script files, and AutoLISP
`(command ...)` are three implementations of one abstract input source. Every prompt
is a state transition, never a blocking read. Commands written as blocking
`read_point()` calls cannot be driven by LISP, and retrofitting that is a rewrite.

Implemented in `command.hpp`. A `Command` is asked for its next `Step`, hands back a
`Prompt`, and is later given the `InputValue` answering it; `CommandEngine` holds the
suspended state between steps. `InputSource::next_value` returning false means "nothing
right now" — the engine suspends and returns control, which is what lets a GUI call
`CommandEngine::supply()` from an event handler instead. Escape is handled by the
engine, not by each command, and committed work survives it as in R12.

The test that matters: a command started by one `(command ...)` call, continued by
arbitrary LISP, and finished by a later call. No blocking read can serve that, because
between the two calls control is back in the interpreter.

## AutoLISP

Targets the pre-Visual-LISP dialect: `command`, `entmake`, `entget`, `entmod`,
`entsel`/`ssget`, `getpoint`/`getdist`/`getangle`/`getstring`, `getvar`/`setvar`,
`defun`, list primitives, plus solid file I/O (`open`, `read-line`).

The intended use is not short macros — it is **driving the tool as a procedural 3D
graphics engine**, generating mesh entities from external analysis data. So the hot
path is "alist of dotted pairs → entity struct", at tens of thousands of faces. That
implies, by design and not as later optimisation: tagged compact values, interned
symbols, arena-allocated cons cells, a pre-resolved AST rather than raw list walking,
and a suppressed-regen batch mode during LISP loops.

**Scoping is dynamic, not lexical.** R12 binds dynamically and real LISP files rely
on it — a `defun` reads a variable its caller set. Bindings are shallow: a call saves
the symbol's current value, overwrites it, and restores on return, including on the
error path. Lexical scope would be cleaner and would silently break working files.

**Not derived from XLISP.** AutoLISP itself descends from David Betz's XLISP 1.0, but
reusing it here buys only a generic evaluator while imposing its mark-and-sweep GC on
the arena design above, and the CAD builtins — `command`, `entmake`, `ssget` — are
written from scratch either way. Semantics are matched deliberately; source is not
borrowed.

## Scope

**In:** core 2D entities (LINE, CIRCLE, ARC, PLINE, TEXT); object snaps
(nea/tan/mid/per/cen/end/int/qua) at the geometry-kernel level; 3D transforms
including ROTATE3D; ruled and lofted surfaces (RULESURF, TABSURF, REVSURF, EDGESURF);
PFACE polyface meshes — the mechanism for pulling external analysis results back in
for visualisation via LISP-driven `entmake`.

**Out:** solid modeling kernel (ACIS/OCCT) — not planned. Grid snap (SNAP/F9) — later,
rarely used. Dynamic blocks, MTEXT, associative dimensioning, ActiveX/.NET, ribbon UI,
cloud/collaboration — all post-R12/R14, excluded by definition.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
./build/tests/noto_tests        # or: ctest --test-dir build
```

Options: `NOTO_BUILD_GUI` (off), `NOTO_WITH_DWG` (off), `NOTO_BUILD_TESTS` (on).

Tests use a minimal in-tree harness (`tests/test.hpp`) with doctest-compatible macro
names, so it can be swapped for doctest or Catch2 without touching test bodies.

Sanitizer build, worth running after any arena or interpreter work:

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan && ./build-asan/tests/noto_tests
```

`./build/tools/noto_gen_sample out.dxf` emits a drawing containing entities on
tilted planes. Opening it in another CAD application is the real correctness gate
for the DXF and ECS code — headless tests only prove self-consistency.

## Layout

```
include/noto/        vec3, mat4, ecs, bbox, osnap, entity, entities, database, dxf,
                     command, commands, input_text
include/noto/lisp/   arena, value, reader, eval
src/core/            geometry kernel, entities, database, DXF writer, commands
src/lisp/            interpreter: arena, values, reader, eval, builtins, subrs
src/app/             ncad: the AutoLISP REPL and command-line entry point
tools/               gen_sample
tests/               in-tree harness + suites
```
