# Handoff — session ending 2026-07-31

Session-scoped and disposable. `CLAUDE.md` holds settled rules, `SF_todo.md` the
roadmap and the reasoning, `features.md` capabilities not yet built, and
`SF_strategy.md` the long horizon. **This file is only the bridge.** Delete or
rewrite it rather than letting it rot.

## Where things stand

- On **`main`**, HEAD `dfd88a8`. **1012 tests, 0 failed.** Clean under
  ASan+UBSan. `ncad_gui` links.
- **Version 0.1.58** — the patch number is the command count, and
  `tests/test_registry.cpp` asserts the two agree. Adding a command means raising
  the literal there *and* `project(VERSION)` in the root CMakeLists.
- Six commits unpushed at the time of writing; push before switching machines.
- Still outstanding from weeks back: **`ncad`'s calculator depends on an
  uncommitted `--pipe` patch in `~/src/iperl`.** It works on this machine and
  silently degrades everywhere else. Smallest item on any list, highest
  embarrassment if a release ships with it.

## The AC1015 writer — built and VERIFIED IN AUTOCAD

`SETVAR DXFVERSION R2000`, then any of SAVE / SAVEAS / QSAVE / DXFOUT. DXFOUT
names the revision it wrote in its reply.

Confirmed by Sadie in AutoCAD 2026: a spline written at R2000 lists as a real
`SPLINE`, order 4, planar, non-rational, with its parametric range intact. The
curve goes out as itself and comes back as itself — which is the whole reason
the revision exists, and it makes the ellipse-becomes-a-polyline problem go away.

**Measured:** a drawing holding one of everything is 8,397 bytes at R12 and
4,525 at R2000. But **R2000 is not always smaller** — handles, owner pointers
and subclass markers are pure overhead when a drawing holds nothing they enable.
`Drawing8.dxf` went 387K at R12 to 449K at R2000. R2000 pays when the drawing
contains entities R12 cannot name, and costs when it does not.

### The five rejections, and what each turned out to be

Every one was AC1015 boilerplate. **Not one was geometry** — the entities
themselves were right from the first attempt. Each is pinned by a test, because
every one of them reads like an inconsistency somebody would tidy away.

| AutoCAD said | It was |
|---|---|
| *(nothing — "corrupt")* | Duplicate handles. VERTEX and SEQEND carried the parent's. **Also affected R12**, see below. |
| `Did not receive PlotStyleName` | R2000 needs group 390 on every LAYER, a **hard** pointer — so the object must exist. Hence the plot-style chain in OBJECTS. |
| `Class separator for class AcDbDimStyleTable expected` | DIMSTYLE's table **header** needs a second subclass marker and a count in group 71. No other table does. |
| `Bad handle 13: already in use` | DIMSTYLE **records** carry their handle in group **105**, not 5 — group 5 is taken there by a dimension block name. |
| `Missing Default entry ByLayer in SymbolTable:LTYPE` | R2000 requires `ByLayer` and `ByBlock` LTYPE entries. They are synthesised at write time; the database has no reason to own them. |

### Two structural things worth not rediscovering

**Handle ordering drove the writer's shape.** `$HANDSEED` must clear every handle
issued and it lives in the HEADER, which is written first — so the document is
written **twice**, once to a null sink to count and then for real. Two passes
rather than buffering, because a gigabyte drawing should not need a second
gigabyte to save. Similarly the plot-style handles are **reserved at the top of
`write_document`** because layers reference them long before OBJECTS is written.

**`DXFVERSION` is deliberately not saved in the drawing.** It says how to *write*
a file, not anything about its contents. As a drawing variable, OPEN resets it —
so choosing R2000 and then opening something silently reverted to R12. It did
exactly that once.

### What needs testing next, and Sadie expects a lot of it

Only **one** R2000 file has been through AutoCAD, containing splines on four
layers. Untested through a real reader: MTEXT, ELLIPSE, blocks and nested
INSERTs, TEXT, tilted planes, polylines with bulges, and anything with a
non-CONTINUOUS linetype. The R12 path is much better exercised.

The cheap way to do this is the **LISP conformance drawing** discussed and not
yet written: one script in `tests/acad/` emitting one of every entity, so each
iteration is one command and the target is fixed between attempts rather than
redrawn. `tests/acad/t2_chain.lsp` is the precedent.

## R12 writes no handles at all, on purpose

This fixed AutoCAD rejecting our R12 files. VERTEX and SEQEND are not database
entities and have none of their own, so they were emitted carrying the parent's —
a degraded ellipse wrote eighteen records all claiming handle 6. R12 makes
handles optional, `$HANDLING` defaults to 0, and **nothing reads group 5**:
`dxf_read` ignores it and assigns fresh handles on load.

**R13 and later require handles**, which is why the R2000 path allocates them
properly. Do not "restore" them to R12.

Verified end to end: an AutoCAD-written R12 DXF opened in NotoCAD, saved, and
reopened in AutoCAD — 19 block definitions, 1,643 nested INSERTs, 4
POLYLINE/SEQEND pairs and 1,408 VERTEX records identical on both sides.

## Rendering — 2.5x, and where the next win is

Found by Sadie arraying a spline into a million copies and taking three gdb
samples of the wedged process. All three landed in `Spline::draw`.

- **`basis_functions` allocated three vectors per call**, `point_at` a fourth.
  Four heap operations per evaluated point of every spline in every frame. Now
  stack arrays, bounded by `kMaxSplineDegree`.
- **`Viewport::project` recomputed `basis()` per point.** Now cached against the
  two angles it derives from — the cache **validates itself** rather than being
  invalidated by mutators, because the one setter that forgot would render the
  whole drawing through a stale basis.
- **`segment_count` had a floor and no ceiling**, so a curve three pixels long
  still emitted sixteen segments. Now bounded by on-screen size — and that bound
  had to be sized by the **bounding box**, not the control polygon, which is an
  over-estimate that made the first version never bite.
- **View culling** added: `draw_database` skipped nothing before. Zoomed into
  200,000 splines, 200,000 polylines became 252 and the frame went ~200ms to
  35.7ms. The clip is **view-space**, because a rotated orthographic view has no
  useful world AABB.
- **The skip list was quadratic** — a `std::find` per entity over
  `flight.suppressed`, which is non-empty exactly while dragging a selection.

**Next:** the remaining 35.7ms is almost entirely the linear `bbox()` scan
deciding what to skip. That is the spatial index, open question 5, which now has
a number against it. At extents a million entities is still ~2M points and about
a second; culling cannot help there, and that case wants level-of-detail.

AutoCAD chokes on comparable drawings too, which suggests the gap left is
architectural rather than anything unusual.

## Traps

Carried forward and still true:

- **`Mat4::from_basis(origin, ax, ay, az)` builds world-TO-basis** — axes in the
  rows.
- **A positive bulge arcs BELOW a left-to-right chord.** `test_polyline.cpp:116`.
- **A test calling `InputValue::of_point()` bypasses UCS conversion.**
- **`ncad_gui` cannot be launched from here** (X11 over SSH). Verify GUI work by
  reasoning and headless tests, and say in the commit that you did not see it.
- **Commit straight to `main`.** No branches.

New this session:

- **`gen_sample`'s drawing contains no polylines**, which is why "verified in
  AutoCAD" hid the handle bug for weeks. The README now says which drawing was
  tested and what was in it; keep it that specific.
- **A LISP `defun` stores into the FUNCTION cell**, not the value cell. A value
  lookup finds nothing, which is why `*error*` silently never ran.
- **The `--lisp` REPL evaluates forms directly**, not through `eval_string`, so
  anything hooked into the latter needs wiring there too.
- **Grouping costs nothing in the undo journal.** It stores clones per *change*;
  a group is only a marker. Per-change undo would not save memory.

## What is next

In the order I would take it:

1. **The iperl dependency** — the only thing already broken for other people.
2. **R2000 conformance testing**, via the LISP drawing above.
3. **INT on ellipse and spline**, and therefore TRIM/BREAK/EXTEND on them —
   `decompose()` has no case for either, so those commands silently ignore them.
4. **The spatial index.**
5. **Tab completion** in the Qt command line, GUI-only by decision — asked for
   with the file operations and the one piece not delivered.

Two decisions waiting on Sadie, no work attached:

- **Whether TANGENT deserves an exception** to discrete-beats-continuous in the
  snap ranking. With CEN and QUA running, a deferred tangent can never win.
- **Whether to remove the deferred tangent on SPLINE.** AutoCAD does not offer
  it, Sadie finds ours chaotic there, and `SF_todo.md` records why it is inherent
  to the curve rather than a bug.
