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
(`-DNCAD_WITH_DWG=ON`) for DWG **import** only. This keeps GPLv3 out of the core and
avoids depending on LibreDWG's weaker R12 write path. DWG export is not planned.

Consequence: there is no `serialize_dwg` slot on entities. DWG I/O converts at the
boundary through LibreDWG's own structs.

**License: BSD-3-Clause.** Chosen over MIT for the no-endorsement clause and the
more explicit binary-redistribution terms. The README states that the license covers
documentation as well, since BSD-3 — unlike MIT — does not name documentation as
licensed subject matter.

Incorporating GPLv3 code makes the *distributed binary* GPLv3, not the project. That
is why DWG is a compile-time option rather than a dependency: the default build links
no GPL code and stays BSD-3, and only a `NCAD_WITH_DWG=ON` binary must be conveyed
under GPLv3. Permissive source flows into GPLv3 cleanly, so nothing is lost. This is
the wall FreeCAD and LibreCAD hit from the other side, being GPLv2.

Consequence: LibreDWG headers must not leak into core headers, and DWG lives in its
own build target so the linkage is visible in the build graph rather than inferred.

**Headless core first.** The core builds and tests with no GUI and no display. The
Qt6 shell comes later and stays thin: windowing, GL context, input events only,
behind a stable core API. Correctness is verified by opening emitted DXF in other
CAD software.

## The Qt shell — decided, not yet built

**Qt6 is LGPLv3, so link it dynamically.** Static linking creates relinking
obligations the BSD-3 core is meant to avoid. Same reasoning as the DWG decision:
the boundary has to be visible in the build graph, not inferred. `src/gui/` is
the only target that sees Qt, and its CMakeLists aborts the configure if it
finds a static Qt — the licensing decision is enforced, not just documented.

**Phase order.** Each phase is usable on its own, and the risk climbs steeply at
the end:

1. *(done)* Command state machine — `include/ncad/command.hpp`.
2. *(done)* Read-only viewer: `draw()` on the entity vtable (`render.hpp`,
   `scene.hpp`), viewport/camera (`viewport.hpp`), pan/zoom/orbit. The second
   independent check on the geometry after AutoCAD 2026, and it earned its
   keep immediately: tilted circles render edge-on in plan and as ellipses
   perpendicular to their normals under orbit.
3. *(done)* Interactive: input events routed to `CommandEngine::supply()`, and a
   command-line widget. The engine's design paid off exactly as intended — the
   viewport is an `InputSource` whose `next_value` always returns false, and a
   click calls `supply()` from the mouse handler. Prompt semantics were lifted
   out of `ncad`'s `std::cin` loop into `app::PromptSession` so the window and
   the terminal run the same code over a `PromptOutput` sink.
4. Usable: pick box, entity hit-testing, osnap cursor tracking, grips. Most of it
   is geometry work rather than Qt work, so it split in two.

   *(4a, done)* Picking and osnap. System variables arrived with it — `OSMODE`,
   `PICKBOX`, `APERTURE` in `sysvar.hpp`, reachable from AutoLISP through
   `getvar`/`setvar`, because there was no mechanism at all and three constants
   would only have to be re-plumbed later. Hit-testing (`pick.hpp`) measures
   against the flattened wireframe in pixels, so it needs no switch on entity
   type and picks exactly what is drawn. `osnap_search.hpp` is where a snap
   acquires its type, its source handle and its screen distance, and where
   discrete snaps are ranked above continuous ones — without that tier rule
   NEAREST buries ENDPOINT and the feature is useless.

   *(4b, next)* The grip/stretch vtable. `transform(Mat4)` moves a whole entity
   and STRETCH cannot be expressed with it; grips are the same mechanism. It
   touches every entity class, so it is far cheaper at three of them than at
   eight. Then selection sets, the editing commands, and interactive grips.

   See `SF_todo.md` for the ordering beyond that and the open questions, and
   `features.md` for capabilities that are wanted but not scheduled.

**QPainter before OpenGL.** R12-era display is wireframe: lines, arcs, text. QPainter
does that in a fraction of the code with no shader pipeline, no GL context management
and no driver variability. Move to `QOpenGLWidget` when 3D orbit performance demands
it, behind the same `draw()` interface. Starting with GL means writing shader plumbing
before a single line appears on screen.

**The hooks already exist.** A viewport is just another `InputSource` that always
returns false from `next_value` and calls `CommandEngine::supply()` from its event
handler instead; there is a test using a source that never yields, pinning that the
engine suspends rather than blocking. `Prompt::kind` says whether to rubber-band a
line or show an aperture, `Prompt::base` is the rubber-band origin, and
`Prompt::last_point` is LASTPOINT.

**No terminal UI work.** The GUI is not driven from a terminal, so raw mode, termios
and libedit are all off the table. `ncad` resolving abbreviations on Enter is enough
for the CLI; live completion is a Qt widget concern where it is free.

**Entity vtable.** `create / free / clone / transform(Mat4) / bbox / osnap_points /
dxf_write / dxf_read / draw`. `transform` is the important one — MOVE, COPY, SCALE,
MIRROR, ARRAY, ROTATE, ROTATE3D, ALIGN and block insertion all route through it.

**ECS / arbitrary axis is foundational, not a feature.** R12 stores CIRCLE, ARC, 2D
POLYLINE, TEXT and SOLID as 2D coordinates in their own entity coordinate system plus
an extrusion vector (DXF group 210). Without the Arbitrary Axis Algorithm in the
kernel from day one, any such entity not parallel to world XY serialises wrong, osnaps
land in the wrong space, and UCS has nowhere to live. See `include/ncad/ecs.hpp`.

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

**Out, for now:** grid snap (SNAP/F9) — later, rarely used. Dynamic blocks, ActiveX/.NET,
ribbon UI, cloud/collaboration — excluded by definition.

**R12 is the starting point, not the ceiling.** Where a modern method is plainly
better, take it — the R12 target exists to give the tool a coherent shape and a
finished feel, not to reproduce 1992's limitations. Sadie's framing, and it
settles a question that had been answered case by case up to now.

What that does *not* license is drifting: each divergence should be a decision
with a reason, recorded where the thing lives. The ones taken so far all have
that shape — CURSORSIZE is AutoCAD's rather than R12's because R12 gave no way
to shorten the crosshair; MEASUREGEOM exists because R12's answer was "draw a
dimension and erase it"; REDRAW and REGEN are *not* built because they manage a
display list this design does not have.

Two guardrails, both about not losing what R12 fidelity buys:

- **A divergence must not break DXF R12 interchange.** Modern geometry may be
  richer in the database than AC1009 can name, but it has to degrade honestly on
  the way out — the same boundary discipline as DWG being a compile-time module.
- **Where R12's behaviour is a considered design choice rather than a
  limitation, keep it.** Escape preserving committed work, the counterclockwise
  arc convention, the negative-radius-means-major-arc rule: those are not old,
  they are right.

**R13 is a stated future direction**, including a solid modeling kernel. That is not
being built and does not change the R12 target, but it does mean the design should
avoid showstoppers. The places it would actually bite are recorded in `SF_todo.md`
rather than pre-solved here; the entity vtable, stable handles and `transform(Mat4)`
already carry over, and a solids kernel is accepted as possibly warranting a fresh
start rather than being retrofitted.

**TEXT is drawn with a bundled Hershey stroke font.** R12's SHX fonts — `romans`,
`romand`, `italicc` — descend from the Hershey set, and a Hershey glyph is a list of
polylines, which is exactly what `draw()` already emits. So this is not an
approximation of what R12 did but the same thing from the same ancestry, and the core
stays headless: no Qt fonts, so screen text and DXF text come from one source. The
data lives in `third_party/hershey/` with the attribution its licence requires, and
is embedded in the binary so there is no runtime data path. An SHX parser, which
would let a drawing use the user's own AutoCAD fonts, sits behind the same interface
later — the same layering as DXF-first with DWG optional.

R12's `%%` control codes resolve in the font layer, at **layout** time and never at
read time: the entity keeps the raw string, so DXF still carries the escape AutoCAD
expects. `decode_text` is shared by `StrokeFont::width` and `draw_text_line` — two
copies is how the measured width and the drawn width come to disagree, which is
justification that is wrong for exactly the strings that contain a code. The degree,
diameter and plus-minus glyphs are drawn rather than vendored, the same argument the
toolbar icons make.

**MTEXT is held exactly and degrades to a run of TEXT records.** AC1009 has TEXT
only — one line per entity, no wrapping, no inline formatting — so a modern
drawing's annotation, which is nearly all MTEXT, used to arrive as Proxy and draw
as nothing. The raw string including its inline codes is what the entity holds;
the formatting is discarded at layout time, not at read time, so opening and
saving a file cannot quietly destroy it. Wrapping uses the font's real advance
widths, which is why this could not exist before TEXT had a font.

**Dimensioning is built, and non-associative as R12's are.** A `Dimension` holds
what it measures and GENERATES its line work; `draw()` and `dxf_write()` call the
same `regenerate()`, so the screen and the file cannot disagree. On the way out it
becomes a DIMENSION record plus the anonymous `*D<n>` block R12 puts the geometry
in — and that block is synthesised in the WRITER only, so nothing else in the
program has to know about anonymous blocks or a name generator.

The style is baked into the entity when it is made, from DIMSCALE/DIMTXT/DIMASZ/
DIMEXO/DIMEXE. `draw()` is handed no database and could not read them later, and
R12 behaves the same way: a dimension keeps the style it was drawn with. The DIM
variables ride in the DXF header so a drawing reopened elsewhere annotates itself
at its own sizes rather than the reader's.

Linear (rotated, and horizontal/vertical inferred from where the dimension line is
put), aligned, radius, diameter and angular. `DIM` is R12's mode and owns no
geometry code: each subcommand builds the real command object and forwards to it,
so there is one implementation and two front doors.

Angular is held in the three-point form — a vertex and a point on each arm —
because it is the general one: two picked lines reduce to it by intersecting their
carriers, and an arc reduces to it by construction. Which of the two angles is
meant is decided by the side the arc is placed on, so the same corner dimensions
as 90 or as 270 and neither is a mistake.

**LEADER is R13's design, and that is a deliberate divergence.** R12 had no LEADER
command and no LEADER entity — it reached one through `DIM`'s `LEader` subcommand
and stored the result in the dimension family. R13 gave leaders an entity of their
own and made the annotation a separate object the leader points at, which is what
lets a leader carry an MTEXT and lets the note be edited after it is drawn. Sadie's
call, and the reason it is right here is that **a leader measures nothing**: folding
it in as a `DimKind` would make `measurement()` lie for one kind and grow a
do-nothing case in every switch over the enum.

The annotation is **owned**, not referenced by handle. R13 binds it with a hard
pointer to a separate database entity; nothing in this program holds a handle to
another entity, and introducing that brings dangling references, clipboard
remapping and erase ordering with it. Ownership buys the whole point of the split —
the note is a real `Text` or `MText` — without the reference machinery.

R12's prompt sequence is kept exactly, including the part that explains why leaders
lived in DIM: the note defaults to the **last dimension's measurement**, which is
session state and lives in `CommandMemory`. The horizontal shoulder is appended by
the entity rather than prompted for, as R12 does it.

DXF **degrades to line work at both versions**, and that is the honest choice rather
than the lazy one: writing R13's real LEADER record means a hard pointer to an
annotation record, and our reader does not know LEADER — so writing it would open a
round trip we could not close. Producing a file we can only half read is worse than
degrading. Same bargain ELLIPSE, SPLINE and MTEXT already take.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
./build/tests/ncad_tests        # or: ctest --test-dir build
```

Options: `NCAD_BUILD_GUI` (off), `NCAD_WITH_DWG` (off), `NCAD_BUILD_TESTS` (on).

Tests use a minimal in-tree harness (`tests/test.hpp`) with doctest-compatible macro
names, so it can be swapped for doctest or Catch2 without touching test bodies.

Sanitizer build, worth running after any arena or interpreter work:

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan && ./build-asan/tests/ncad_tests
```

`./build/tools/ncad_gen_sample out.dxf` emits a drawing containing entities on
tilted planes. Opening it in another CAD application is the real correctness gate
for the DXF and ECS code — headless tests only prove self-consistency.

`ABOUT` prints the version, the build's git hash and the licence of every
component the binary actually carries — conditioned on the build, so a
`NCAD_WITH_DWG=ON` binary says it must be conveyed under GPLv3 and a default one
says it links no GPL code. The Hershey acknowledgements are there because that
licence requires them to travel with the font data, and the data is compiled in;
a test asserts they are present, so falling out of compliance fails the suite.

The version's patch number is the command count — 0.2.72 means 72 commands —
and `tests/test_registry.cpp` asserts the two agree. Adding a command means
raising the literal there and `project(VERSION)` in the root CMakeLists.

**Every registered command counts**, whether or not R12 had it: MEASUREGEOM,
ROTATE3D, SPLINE, ELLIPSE, REDO and UCSICON are all in the total. The number
says how much of the tool exists, not how much of the 1992 manual is covered.
QUIT is the one thing you can type that is *not* counted — it ends the session
rather than acting on the drawing, so `prompt.cpp` handles it beside EXIT and it
owns no `Command`. That is why `?` lists 73 names against a registry of 72.

**The minor number marks a milestone reached**, and is bumped deliberately
rather than by any rule a test can check:

- **0.1** — the R12 command set became usable: entities, editing, blocks, UCS,
  layers and linetypes, undo, DXF both ways.
- **0.2** — **macOS**, and R2000 confirmed in a real reader. The port built and
  passed on the first attempt from a written handoff, which made the second
  platform a day rather than a phase. Alongside it: AC1015 output verified
  entity by entity in AutoCAD 2026, the `noto` → `ncad` rename, and iperl
  reachable on any machine rather than one.

`cmake -B build -DNCAD_BUILD_GUI=ON` adds `./build/src/gui/ncad_gui`, the Qt
shell: the same drawing, engine and interpreter as `ncad`, with a viewport.
Middle-drag pans, shift+middle orbits, wheel zooms about the cursor, Home is
extents and Ctrl+Home is plan. Typing anywhere goes to the command line, and a
left click answers a point prompt. Rendering the same database two ways is a
correctness check worth having — the two disagreeing is a real signal.

Executables are `ncad*`; libraries are `ncad_*`, except `ncad_gui_lib`, which is
suffixed `_lib` to avoid colliding with the `ncad_gui` executable.

## Layout

```
include/ncad/        vec3, mat4, ecs, bbox, osnap, entity, entities, database, dxf,
                     command, commands, font, input_text, osnap_derived,
                     osnap_search, pick, render, scene, sysvar, viewport
include/ncad/lisp/   arena, value, reader, eval
src/core/            geometry kernel, entities, database, DXF writer, commands
src/lisp/            interpreter: arena, values, reader, eval, builtins, subrs
src/app/             ncad: the R12 command prompt, and PromptSession, which the
                     Qt command line runs too
src/gui/             ncad_gui: the Qt shell. The only target that sees Qt.
tools/               gen_sample, and the sample drawing the viewer also renders
tests/               in-tree harness + suites; tests/acad/ holds the AutoLISP
                     fixtures the AutoCAD comparison is driven from
packaging/           mac_bundle.sh: build, verify, sign and package NotoCAD.app
```

`scripts/` is ignored and is Sadie's scratch space for AutoLISP and its output.
Nothing that the build, the tests or a release depends on belongs there --
anything put there is invisible to git, which is what the ignore rule is for
and also the trap it sets.
