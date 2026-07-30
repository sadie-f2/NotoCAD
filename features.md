# NotoCAD — features

Capabilities the program should have, and what each one would actually cost.

**What this file is not.** `SF_todo.md` is the engineering roadmap — work items,
decisions taken, and the traps found along the way. `SF_strategy.md` is
long-horizon direction: the solids core, remote operation, portability.
`CLAUDE.md` is how the code is written. This one is about what the program can
*do*, from the outside, and is deliberately allowed to contain things nobody has
committed to building.

Nothing here is scheduled. An item earns a place in `SF_todo.md` when it is
actually next.

---

## Command-line options

Planning only. The positional filename is the one R12 itself had.

| Option | Why |
|---|---|
| `<file>` | Open a drawing at startup. Both binaries. |
| `<file> <file> ...` | One window per drawing — see the multi-document section. |
| `--script <file>` | R12's third argv slot, and the highest-leverage item here. |
| `--font-size <pt>` | GUI. Persistence without a config file. |
| `--saveas <file>` `--quit` | Batch conversion. |
| `--minimal` | Reduced display for slow links — but see below. |
| `--version`, `--licenses` | One line, and the ABOUT text. Packagers expect both. |

**`--script` is worth doing before the others.** It makes any bug reproducible as
a file, it is the natural companion to the history-recording work, and it gives
the test suite a way to drive whole sessions rather than individual commands.

**Batch conversion is nearly free, and it is a scarce utility.** The reader now
takes AC1032 including ELLIPSE, LWPOLYLINE and SPLINE, and the writer emits clean
R12. `ncad in.dxf --saveas out.dxf --quit` is therefore a modern-DXF-to-R12
converter that falls out of what exists rather than needing anything new. Very
little else does this simply.

### `--minimal` should be a preset, not a mode

A second display path is how the two drift. Nearly all of it is already
expressible as system variables, and the missing one has an R12 name waiting for
it:

**DRAGMODE** — R12's own control, 0 off, 1 on-request, 2 auto. That is exactly
"show me the thing moving while I work, or don't", and it is the sysvar
`InFlight` has never had. Add it and `--minimal` becomes `DRAGMODE=0` plus a
reduced `OSMODE` plus one or two genuinely display-only flags such as
antialiasing.

The payoff is that every one of those is then adjustable **at run time**, which
is when a slow link actually reveals that you wanted it — rather than needing a
restart to change your mind. The flag becomes a convenience over machinery that
is useful on its own.

### The serious version of `--strict`

Not refusing non-R12 features — *reporting what a save is about to lose*.

Three entity types degrade lossily on write: ELLIPSE and SPLINE become polylines,
MTEXT becomes a run of TEXT records. The program knows this at write time and
says nothing, and that silence has already cost an afternoon once, when a saved
and reloaded ellipse snapped like the polyline it had become.

    Saved plan.dxf -- 2 ellipses, 1 spline and 1 mtext degraded to R12 form.

One counter per degrading `dxf_write`. It makes the honest-degradation rule
*visible* rather than merely documented, which is the difference between a
principle and a safeguard.

### Two notes for whoever builds it

`QApplication` consumes parts of argv itself — `-style`, `-display`, `-platform`
— so ours has to be parsed deliberately around it rather than treating the vector
as clean. And the parser belongs in `src/app`, shared by both binaries, for the
same reason `PromptSession` and `about_text()` are: two argument parsers would
disagree about `--script` inside a month.

---

## Multiple documents and windows

**Wanted: several drawings open at once, one window each.** No tiling and no MDI
— separate top-level windows, which is also what makes several files on the
command line mean something.

### What stands in the way

`MainWindow` owns the `Database`, the LISP `Context`, the `Interp` and the
`CommandEngine` **by value**. The application is single-document by
construction, and the first move is not a window at all: it is separating a
*document* from the *window that shows it*.

A Document is a `Database` (which already carries its own undo journal, tables
and system variables) plus the drawing's name. A window is a viewport, a command
line, a `CommandEngine` and a `PromptSession` over one Document. That much is
mechanical.

Three things are not mechanical, and they are the reason this is a design item
rather than a refactor:

**1. Which drawing does AutoLISP act on?** `entmake`, `ssget` and `entget` all
mean "the current drawing", and R12 never had to answer because there was only
one. Either the interpreter is shared with a notion of the active document, or
each document gets its own — AutoCAD offers both and defaults to per-document
namespaces. Shared state between drawings is genuinely useful for the workflow
this project exists for (generating geometry from analysis data), so the shared
interpreter with a current-document pointer is probably right. It should be
decided rather than defaulted into.

**2. System variables have no scope, and they need two.** This is the sharp one.
Some sysvars belong to the drawing — `LTSCALE`, `LIMITS`, the UCS, `DWGNAME`.
Others belong to the session and should be the same in every window —
`PICKBOX`, `APERTURE`, `OSMODE`, `CURSORSIZE`. R12 and AutoCAD split them exactly
this way, saving one set in the drawing and the other in the profile.

`sysvar.hpp` makes no such distinction: every variable lives in the `Database`,
so opening a second drawing would give it its own `PICKBOX` and changing one
would not change the other. The table needs a scope column, and the decision has
to be made per variable. Worth knowing that this is discovered *now*, cheaply,
rather than by two windows disagreeing about the aperture.

**3. What does the undo journal do across windows?** Per-document is the obvious
answer and probably the right one, but it interacts with the item below: a paste
is one drawing's change caused by another's.

---

## Copy and paste between drawings

Sadie's note is right that this is not MOVE/COPY with a displacement. The
displacement is the easy half. The hard half is that entities carry *references*
— layer ids, linetype ids, block ids — and those are indices into the table of
the database they came from. Pasting them anywhere else means remapping every
one, and merging what is missing.

**The clipboard should be a DXF fragment.** Copy writes the selection as DXF into
a buffer; paste reads it into the target drawing. Three things fall out at once:

- **Table remapping is already solved.** `dxf_read`'s `apply_common` creates a
  layer that a file names but does not define, which is exactly the merge a paste
  needs, and `WBLOCK` already writes a selection to a file.
- **Interchange comes free.** Put the same fragment on the system clipboard and
  copy/paste works with other CAD applications, which is a real capability rather
  than a side effect.
- **One code path.** WBLOCK, INSERT from another drawing, and paste become the
  same machinery, so they cannot disagree about what a block reference means.

R12's own answer was the same shape: `COPYCLIP` put a block on the clipboard.

Needs a base point, as R12's `COPYBASE` and `PASTECLIP` have — the paste has to
land somewhere, and the default of "where it came from" is wrong the moment the
two drawings are not aligned.

The cost is that a DXF round trip is lossy for the three degrading entity types,
so a copied ellipse would paste as a polyline. That is not acceptable for an
internal clipboard, and the answer is the same one the DXF-version work already
points at: the *internal* fragment can be a later DXF version, or can carry the
exact forms as XDATA, while what goes on the *system* clipboard stays R12.

---

## Queued elsewhere

Items with analysis already recorded in `SF_todo.md`, listed here only so this
file is not misleading by omission:

- **MTEXT as a command.** The entity exists and can only arrive from a file or
  from `entmake`; there is no way to draw one.
- **Tab completion** in the Qt command line. GUI-only by decision.
- **INT on ellipse and spline**, and therefore TRIM, BREAK and EXTEND on them.
- **The remaining previews** — POINT, SOLID, INSERT, BREAK, TRIM.
- **OFFSET, FILLET, CHAMFER, CHANGE/CHPROP.**
- **History recording for scripting**, which pairs with `--script` above.
- **DIM**, deferred deliberately and now on the roadmap rather than out of scope.
- **Writing DXF versions later than R12.** Reading them is built.
