# NotoCAD — roadmap

The long-range plan. `CLAUDE.md` records decisions already settled; this records what
is *next* and why it sits where it does, so the order is chosen rather than drifted
into.

Near-term work is tickable. Beyond that the phases carry rationale instead of task
lists, because the detail belongs in a per-phase plan written when the phase starts.

**Capabilities that do not exist yet live in `features.md`**, along with their design
notes. The line between the two files: a mechanism that exists, a decision already
taken, or a defect found in use belongs here; something the program cannot yet do
belongs there. Seven sections moved out when that file was created — viewport
feedback, CIRCLE's construction options, UI polish, the view commands, interactive
AutoLISP input, pick cycling, and PEDIT's curve options — and the analysis went with
them, because the reason a thing is wanted is the most perishable part of wanting it.

## Immediate — oversights, ahead of everything below

Not design questions, just things caught in use that should not wait behind phase order.

- [x] **DXFOUT (and SAVE, etc.) should ask which version, not silently use DXFVERSION.**
      Sticky: the chosen value pushes back into DXFVERSION as the new default for the
      rest of the session, so asking once does not mean asking every time. QSAVE stays
      silent on purpose -- asking there would defeat the one command that must not
      interrupt. The sticky push-back goes through `Sysvars::set_metadata`, not
      `set_string`: it runs inside SAVE/DXFOUT's own still-open undo group, and
      journalling it there is the DWGPREFIX/DWGNAME bug again -- the group would close
      with a change of its own and the drawing would read dirty the instant it was
      saved. SETVAR DXFVERSION, typed directly, still journals; only the automatic
      push-back is exempt.
- [x] **Qt command line: text selected in the transcript could not be copied out.**
      Turned out not to be macOS-specific -- Cmd-V/Ctrl-V pasting in but not copying out
      was the same root cause on every platform. The transcript pane never holds
      keyboard focus (deliberately, so stray typing cannot vanish into it), so the copy
      shortcut always resolved against the one-line input instead, wherever it was
      selected. X11's middle-click paste is a separate, always-on mechanism that
      happened to mask the gap on Linux; it was never actually fixed there either.
      `QKeySequence::Copy` (Ctrl+C / Cmd+C) now checks the transcript's selection first.

      *Superseded in the geometry-clipboard work:* that shortcut was scoped to the
      command line widget, so it only fired while focus sat there -- dead after any
      viewport click, which is why it "did not seem to work" on the Mac. The chords now
      live on MainWindow (application scope) and arbitrate: a text selection wins,
      otherwise an idle prompt runs COPYCLIP/CUTCLIP/PASTECLIP.
- [x] **COPYCLIP / CUTCLIP / PASTECLIP: geometry copy/paste between windows.** All
      three entered AutoCAD in R12 for Windows as ordinary commands that prompt Select
      objects, so Cmd-C under command-then-select is not a semantics problem -- the
      shortcut runs the command, the command asks. The payload is a whole DXF document
      (R2000, so a spline crosses as a spline) on the system clipboard as plain text;
      the base point rides in `$INSBASE` as WBLOCK already does; paste is DXFIN's
      Merge plus one translation, in one undo group, and becomes Previous. Block
      definitions travel by closure, nested INSERTs included. `clipboard.hpp` has the
      design notes; `Clipboard` is an interface on CommandContext like ViewControl,
      Qt hands over the system clipboard, `ncad` a session-local one. Cmd-V goes by
      FOCUS -- in the command line it is a text paste, anywhere else it runs
      PASTECLIP, which reports honestly if the clipboard holds no DXF. Sadie's call,
      replacing an earlier content-sniff: the rule puts context on the user, which
      alpha software may. COPYBASE --
      ask for the base point instead of taking the bbox corner -- is the natural
      later addition. When pickfirst/grips (4b) arrive, COPYCLIP should honour the
      implied selection like every other editing command.
- [x] **POINT did not offer a NEA osnap.** NEA is the one snap that means something for
      a zero-dimensional entity; END is convenient (a point looks like its own endpoint
      from some angle) but not correct. The fix is a `EntityType::Point` branch in
      `nearest_point`: every other branch there solves for a position ALONG something
      and a point has no along, which is exactly why it was missing. Confirmed working.

      Read the wrong way first: the item was taken as the POINT COMMAND's prompt
      offering NEA while placing, and `Prompt::extra_mask` was built for it. Reverted --
      it forced NEA on at POINT's prompt on top of OSMODE, so placing a point near any
      geometry snapped to it every time, NEA being continuous rather than tied to a
      feature. AutoCAD does not do that, and typing `nea` at a prompt already gives you
      NEA for one pick, so the field granted no capability that did not exist and only
      removed the ability to decline it. With POINT's use gone it had no callers at all.
- [x] **Toolbars, and file dialogs.** Icons are DRAWN, not shipped -- `command_icons.cpp`,
      the same argument the Hershey font makes: an icon for a drawing command is a little
      line drawing, so strokes cost less than a PNG and there is no asset directory, no 2x
      copy, no icon-set licence, and the ink follows the palette. A button feeds its
      command name to PromptSession exactly as typing does, so a button can never mean
      something the command line does not. Unnamed commands fall back to two letters, so
      adding a button before its icon reads as itself rather than as a blank.

      File dialogs go through `Prompt::file` (a `FileIntent`), which follows RubberBand's
      precedent exactly: the core states a fact about the ANSWER -- this is a file, it must
      exist -- and never mentions a dialog, which is what keeps QFileDialog out of it.
      `FILEDIA` is the R12 switch and earns its keep for the R12 reason: a script driving
      OPEN must not stop on a modal window. `~` at a file prompt asks for the dialog anyway.

      The version choice lives in the save dialog's file-type list rather than in a
      question after it -- `Prompt::file_formats` on the NAME prompt, since that is when
      the dialog is built, and `file_format` / `file_overwrite` marking the questions it
      has thereby answered. Sadie's point, and it generalised immediately: the native
      dialog also confirms replacing a file, so SAVE's own Overwrite? prompt was the same
      redundancy a second time.

      Two things found by LOOKING at the window rather than by anything failing: a
      stylesheet set on the QMainWindow takes over its children and turned the command
      line's transcript white on white (it is per-toolbar now), and an ellipse icon with a
      centre dot reads as an eye.
- [x] **A command finished by a MOUSE PICK reported nothing.** Found via MEASUREGEOM,
      which worked out the distance and the deltas and never printed them -- but it was
      never about MEASUREGEOM. `report_finished` was only reachable from `feed_line`, and
      the viewport answers prompts by calling `CommandEngine::supply()` directly, which is
      the whole point of the resumable design. So EVERY command finished by clicking lost
      its result, and the worse half is that a command that FAILED on a click said nothing
      whatsoever and looked ignored. `PromptSession::report_if_finished` is the same report
      for the other way in, and it is idempotent because the caller is an event handler
      that cannot know which click ended the command.
- [x] **The window's close button did not ask about unsaved work.** QUIT asked; the close
      button went straight out. Three answers rather than two, for the reason QUIT gives
      for having three -- and Save is real rather than a relabelled Discard, which means
      the close is REFUSED and retried once the save has finished, since a save can put up
      a file dialog and a version question of its own and none of that can happen inside
      the close event. A save that fails or is cancelled leaves the window open.
- [x] **A drawing can be named on the command line**, which was a target from early on
      and had rotted into a wrong answer: `ncad plan.dxf` fed the DXF to the interpreter
      and said "unbound variable: SECTION". A `.dxf` argument now opens as the drawing in
      both front ends, decided by extension rather than by sniffing the contents -- a rule
      you can predict from what you are typing beats one that is usually right. Drawings
      open before scripts run, and a drawing alone does NOT count as work-then-exit, which
      is what makes `ncad plan.dxf < script` and heredocs work. Both go through the OPEN
      command rather than reading the file themselves, so DWGNAME, the message and the
      error path stay single-sourced.
- [x] **Toolbar placement is sticky**, along with window size and command-line text size,
      in `~/.config/NotoCAD/ncad_gui.ini` -- QSettings' IniFormat puts it under ~/.config
      on macOS too, so there is one path to name when it needs deleting. Saved on close
      AND on `aboutToQuit`, because Cmd-Q and the Dock's Quit do not reliably deliver a
      close event and losing an afternoon's arrangement to the wrong exit would be a poor
      reward for having arranged it. `--reset-ui` is the way out of a bad state.
- [ ] **Pick-point commands are mouse-only from the terminal.** TRIM, EXTEND, FILLET and
      CHAMFER all need to know WHERE on an entity the pick was, and the terminal's entity
      prompt parses a bare handle with no location (`input_text.cpp`, PromptKind::Entity).
      FILLET and CHAMFER work around it by standing in the line's midpoint, which is a
      real answer because a corner is at an END -- see `pick_on` in commands.cpp. TRIM and
      EXTEND have no such fallback and simply refuse, so they cannot be driven by
      `(command "TRIM" ...)` at all, which the design says must not be true of any command.
      Either give them the same kind of stand-in, or let the text prompt accept a
      handle-and-point form.
- [ ] **`c:` functions are not dispatched as commands.** `(defun c:box ...)` followed
      by typing `BOX` says unknown command — found while writing docs/using-notocad.md,
      which documents the parenthesised call as the workaround. The lookup belongs
      where the registry miss is currently reported, asking the interpreter whether
      `C:NAME` is bound before declaring the name unknown.
- [x] **DXFIN emptied the drawing it was importing into.** Root cause: OPEN and DXFIN
      were one command class, so DXFIN ran OPEN's code and `read_dxf_text` cleared
      unconditionally. Now `DxfReadMode::{Replace,Merge}` and one `DxfInCommand` with a
      mode, the shape `SaveCommand` already uses. R12 agrees: DXFIN into a drawing that
      holds entities reads the geometry and leaves what is there. Merge needs no handle
      renumbering -- `Database::clear` deliberately never rewinds `next_handle_` -- and
      merges tables by name with the existing entry winning, so an import cannot
      redefine a layer being drawn on. It also keeps the drawing's name (or the next
      QSAVE would silently retarget the file imported FROM), its current UCS, and its
      undo history, and is one undoable step.
- [x] **Vanity: colour "Not" in the NotoCAD wordmark cyan.** Considered red for "CAD"
      too, and decided against it -- no reason to make the naming/trademark question
      any more pointed than it has to be. The wordmark is the viewport nameplate,
      which was the only one that existed; it is drawn as two runs now, with the
      alignment still computed from the whole line. Dimmed from full cyan, since a
      nameplate that pulls the eye while you work has stopped being a nameplate.

## Direction

**Editing and command breadth come before 3D meshes and surfaces.**

`CLAUDE.md` names the project's purpose as driving the tool as a procedural 3D
graphics engine — generating mesh entities from external analysis data. That remains
the goal, and it is deferred deliberately: it is not what makes the tool usable day to
day, and nothing in it is a prerequisite for the command work. It sits at phase 13.

The current entity set is LINE, CIRCLE, ARC. The current command set is LINE, CIRCLE,
ERASE, DXFOUT. Almost everything below follows from widening those two.

---

## Phase 4a — picking and object snap

*Done.*

- [x] Sysvar table: `OSMODE`, `PICKBOX`, `APERTURE`; `Database::sysvars()`
- [x] `OsnapMask` with R12 bit order — an explicit table, **not** `1 << int(type)`
- [x] `getvar` / `setvar` in AutoLISP
- [x] `entity_pick_distance` / `pick_entity`, flattened-polyline narrow phase
- [x] `osnap_search` — candidate ranking, discrete snaps beat continuous ones
- [x] Fix: a click at an Entity prompt supplies `of_entity`, not `of_point`
- [x] Osnap cursor tracking and marker glyphs, painted Qt-locally

R12's OSMODE bit order is not our `OsnapType` declaration order — Quadrant and Node
are swapped, and Intersection is last in our enum but mid-mask in R12. `osnap_bit()`
is a lookup table, pinned by a test asserting the literal values.

- [x] One-shot osnap overrides: type `cen`, `nea`, `tan`, `non` at a point prompt

`OSMODE` defaults to 37 (END, CEN, INT) — a lean running set, with everything else
reached as a one-shot override. There is still no OSNAP command and no status line, so
the active mode is named beside the marker glyph instead. Both become phase 6 and 8
work.

## Phase 4b — the grip/stretch vtable

*Done.*

- [x] The interface: `grips()` reports defining points, `stretch(delta, indices, n)`
      moves a named subset. An index list rather than a bitmask, so a polyline with
      hundreds of vertices fits the same signature
- [x] Grips carry *behaviour*, not just position — `GripKind::Stretch` / `Move` /
      `Radius`. A circle's quadrant and centre grips are both points on the same
      circle and mean different things, which is why this is not `osnap_points()`
- [x] Implemented across Line, Circle, Arc; headless tests
- [x] No interactive dragging yet — that needs phase 5

The property worth keeping as entities are added: naming *every* Stretch grip must
equal translating the entity. That is what makes STRETCH degenerate into MOVE rather
than into nonsense when the selection was not a crossing window, and it is tested.

*To verify before phase 5:* R12's exact rule for stretching an **arc** endpoint is not
established. The current behaviour slides the endpoint along the arc's own circle,
preserving centre and radius — predictable, but chosen for that rather than proven
faithful. Check it against the R12 documentation alongside the crossing-window
question below.

**Why now, before the command flood.** The only mutator on the entity vtable today is
`transform(const Mat4&)`, which moves the *whole* entity. STRETCH moves some defining
points and leaves others fixed; `transform` cannot express that. Grips are the same
mechanism — dragging a line's endpoint grip *is* a one-point stretch — so both fall out
of one vtable addition.

It touches every entity class, which makes it far cheaper at three entities than at
eight. That is the same argument `CLAUDE.md` already makes for ECS and for resumable
commands: foundational, not a feature.

## Phase 5 — selection sets and the editing commands

*Mostly done. The unticked items are genuinely outstanding, not overlooked bookkeeping.*

- [x] `SelectionSet` type; widen `CommandContext` (its header already anticipated this)
- [x] Window, Crossing, Last, Previous, All, plus Remove/Add
- [ ] **Fence selection** — never built
- [x] Box and crossing pick in the viewport widget
- [x] `ssget`, `ssadd`, `ssdel`, `sslength`, `ssname`, `ssmemb` in AutoLISP
- [ ] `entsel` — needs interactive input, see below
- [ ] `getpoint`, `getdist`, `getangle`, `getstring`, `getreal`, `getint` — same
- [x] MOVE, COPY (with Multiple), ROTATE, SCALE, MIRROR, ARRAY, STRETCH
- [x] **ROTATE3D** — done, with Entity/View/Xaxis/Yaxis/Zaxis/2points.
- [x] ROTATE3D's **Last** axis option — built, once `CommandMemory` gave it somewhere
      to live. See the session-state note below.
- [ ] **Reference angle** on ROTATE, and reference length on SCALE — R12 has both and
      neither was built, so ROTATE3D would inherit the gap. Worth doing together.
- [ ] Interactive grip dragging

ROTATE3D is also the best available stress test of the 3D kernel: it is the first
command that *deliberately* puts geometry into arbitrary planes rather than reading it
that way. Rotating a bulged polyline out of plane, writing DXF and reading it back
exercises ECS, the bulge sign handling and the arbitrary-axis algorithm at once — and
any of the three being subtly wrong shows up as a shape that changes when saved.

**STRETCH needs more than a handle list.** It only works when the selection is made by
crossing window or crossing polygon. With a plain Window, or with objects picked
individually, every defining point of every selected entity is inside the selection —
so "move the points that are inside" moves all of them, and STRETCH silently
degenerates into MOVE. No error, no warning. That is exactly the shape of a feature
that seems broken to a user.

The design consequence: `SelectionSet` must carry the crossing region's geometry, not
only the handles. The question "which vertices move" is answered by the crossing
rectangle, and that is not derivable from a set of entity handles. *(Built —
`SelectionRegion` in `selection.hpp`, one region, last one wins.)*

*To verify before building:* AutoCAD is believed to honour only the **last** crossing
window when deciding which points move, even after several selections. If that holds,
the set carries at most one stretch region rather than a list. Check against the R12
documentation rather than memory.

---

## Phase 4c — undo and redo

*Done.* Unlimited, back to the start of the session. `undo.hpp`, `UNDO` / `REDO` /
the `U` alias.

- [x] Before/after journal on `Database`, leaning on stable handles and `clone()`
- [x] Command-level grouping — one UNDO reverses one command, not one segment
- [x] Nested groups collapse, so a LISP function calling `(command ...)` repeatedly
      is one step; this is the hook a batch mode will use for `entmake` meshes
- [x] System variables journalled, not only entities
- [x] Erased entities restored to their old position in the drawing order
- [x] Undo is not itself undoable; new work discards the redo stack

Landed before phase 5 on purpose. Every editing command has to be undoable, so
building MOVE, COPY, ROTATE, SCALE, MIRROR, ARRAY and STRETCH first would have meant
revisiting all seven. The same argument as 4b and as ECS: cheaper at four commands
than at eleven.

Layers and linetypes are journalled too, as of phase 8's prerequisite. `Database` no
longer hands out a mutable `Layer&`: writes go through `set_layer_*`, for the same
reason `Sysvars` owns its own journalling — a write routed past the journal is how undo
grows holes.

**Still open:** memory is unmeasured; see below.

Not yet built, and wanted:

- R12's `UNDO Mark` / `Back`, and `UNDO <n>` for several steps at once. The grouping
  mechanism is already there; these are argument parsing on top of it.
- Layer and linetype journalling, before LAYER exists to change them.
- A group around a whole top-level LISP evaluation, so `entmake` in a loop over ten
  thousand faces is one step rather than ten thousand. `begin_group` nests already;
  nothing calls it from the interpreter yet.

Open question: memory. Journalling clones back to session start is unbounded by
definition, and the mesh workload is tens of thousands of faces. Whether that is
actually a problem, and whether a compact diff beats storing clones, needs measuring
rather than guessing — the interface hides which one it is.

---

## Long-range phases

| # | Phase | Why it sits here |
|---|---|---|
| 6 | View commands | *Mostly done.* ZOOM, PAN, PLAN and the inquiry commands are built, with transparent (`'ZOOM`) support. Still to write: VIEW (named views), REGEN, REDRAW, and ZOOM's Center, Left and Dynamic options. **VPOINT is deferred to phase 12**, since it is interpreted in the current CS and writing it against WCS only would mean writing it twice. |
| 7 | Entity breadth | *Done, apart from what is listed below.* The entities existed already; PLINE, POINT, SOLID, 3DFACE, TEXT and PEDIT are now the commands that make and edit them. **Not built:** PEDIT's `Edit vertex`, and its `Fit`/`Spline`/`Decurve` — see the curve-representation note below. PDMODE and PDSIZE do not exist, so a POINT draws a fixed cross rather than R12's choice of marker; TEXTSIZE does not exist either, so TEXT's height defaults to 1.0 rather than to the last one used. TEXT's `Style` option is absent because there is no STYLE table to choose from. |
| 7 | *(detail)* | Done. Selection, hit-testing, osnap, transforms and DXF are now exercised against a realistic entity set rather than Line/Circle/Arc. The TEXT rendering decision (open question 2) is still open — the entity draws as a placeholder box, now correctly placed by its justification. |
| 8 | Tables and settings | *Done, apart from UNITS.* LAYER, LTYPE with built-in patterns, dash rendering, COLOR, LTSCALE, LIMITS, and current entity properties. **UNITS is not built**: R12's is a page of report-format questions, and the formatting they control is hardcoded to four decimal places in DIST, ID, AREA and LIST. LUNITS/LUPREC/AUNITS/AUPREC do not exist yet. Also missing: LTYPE loading from a real `acad.lin`, and wildcards in layer and linetype names. |
| 8 | *(detail)* | LAYER, LTYPE and dash rendering, COLOR, LTSCALE, UNITS, LIMITS. Linetypes touch three layers at once: the DXF table, dash generation in the render path, and LTSCALE — not just a table entry. |
| 9 | DXF read and OPEN | *Done.* `dxf_read.hpp`, the `Proxy` entity, and DXFIN with OPEN as its alias. LINE, CIRCLE, ARC, POLYLINE and INSERT become real entities; everything else becomes a proxy that writes back unchanged. **The BLOCKS gap is closed** — definitions are read, and an INSERT naming a block defined later still resolves, because inserts are fixed up in a second pass. **Still not read:** the HEADER section's system variables (only `$ACADVER` is looked at), so a file's OSMODE, LIMITS and INSBASE are ignored on the way in even though they are now written on the way out. |
| 10 | Geometry editing | **Done, except CHANGE/CHPROP.** The kernel (`intersect.hpp`) and the cutting primitives (`curve_edit.hpp`) are built, and **BREAK, TRIM, EXTEND, OFFSET, FILLET and CHAMFER are done** — the last three at 0.2.65, when a real use for OFFSET turned up (fattening Hershey strokes into outlines with mitred corners). `offset.hpp` and `corner.hpp` carry the geometry. **CHANGE/CHPROP is deferred on purpose until after UCS**: CHANGE's change point is interpreted in the current UCS, so writing it against WCS now would need revisiting rather than extending — the same trap VPOINT is held back from. |
| 11 | Blocks | *Done.* BLOCK, INSERT, MINSERT, EXPLODE, WBLOCK and BASE, plus the BLOCKS section both ways. Open question 7 is answered below. **Not built:** ATTDEF/ATTRIB, and EXPLODE of a polyline. |
| 12 | UCS | *Done.* `Ucs` in `tables.hpp`, the named table on `Database`, the current one in system variables, and the UCS/UCSICON commands. PLAN's three answers finally differ. **Not built:** VPOINT (still deferred, see below), and the UCS icon itself, which is viewport drawing rather than kernel work. |
| 13 | Meshes and surfaces | PFACE, 3DMESH, RULESURF, TABSURF, REVSURF, EDGESURF; AutoLISP file I/O (`open`, `read-line` — `file_subrs.cpp` currently holds only `dxfout`); suppressed-regen batch mode for LISP loops. **The project's stated purpose.** |
| 14 | Performance | A spatial index — none exists, and 4a's mouse-move path is the first thing to feel the linear scan. `QOpenGLWidget` migration behind the same `draw()`. |
| 15 | macOS port | Stated goal in `CLAUDE.md`. |

---

## `InFlight` — geometry a command has changed but not committed

**Built.** `inflight.hpp`, `Command::preview()`, `CommandEngine::preview()`, and
MOVE, COPY, ROTATE, SCALE, MIRROR, ROTATE3D and STRETCH. ARRAY is still out, and
still for the reason below. What follows is the reasoning as it was recorded
before building; two estimates in it turned out wrong in the same direction, and
are marked where they appear.

Sadie's, and the name is hers: not "preview", which sounds like a display effect,
but **state that exists between a command starting and committing**. The display
is only its most visible consumer.

### Why one construct and not a preview per command

Every editing command has the same shape — collect input, derive a modification,
apply it to the selection — and every one of them currently makes the modified
geometry visible only by committing it. MOVE, COPY, ROTATE, SCALE, MIRROR,
ROTATE3D, STRETCH and ARRAY are eight instances of one missing idea, and
interactive grips will be the ninth.

**The abstraction is not a transform.** The tempting version is "a pending `Mat4`
against the selection", and it fails at the first hurdle: STRETCH cannot be
written as one, and neither can a dragged grip — `CLAUDE.md` says as much where
it introduces the vtable. The abstraction is one level up: *if this command
committed right now, what would the affected entities look like?* MOVE answers
with a matrix, STRETCH with `stretch(delta, indices)`, ARRAY with N matrices, and
nothing outside the command needs to know which.

Consequently the complicated commands are not the hard ones. A ROTATE3D about an
arbitrary axis is exactly as easy to show as a translation — `clone()` then
`transform(m)` either way — because the difficulty lives in *deriving* the
matrix, and that code already exists and is reused rather than rewritten.

### The constraint that decides whether this works

**In-flight and commit must be the same code, differing only in whether the
result is written back.** The standard way this feature rots is a second path
that recomputes the modification, drifts from the real one, and shows a ghost
that does not match what you get — silently, because nothing compares them.

So each command factors into *derive the change* and *apply the change*, with
commit calling both and in-flight calling the first onto clones. A test worth
writing once the mechanism exists: drive a command to its last prompt, capture
the in-flight geometry, commit, and assert the committed entities equal what was
shown. That is the check that stops the two drifting, and it is cheap.

### Shape

```cpp
struct InFlight {
    std::vector<EntityPtr> ghosts;      // modified clones, drawn highlighted
    std::vector<Handle>    suppressed;  // committed entities hidden meanwhile
};
```

`suppressed` is the whole of what "mark the selection" was reaching for, and it
is why the alternative — carrying the database with a flagged subset — buys
nothing: the viewport already walks the database, and the only thing it cannot
work out for itself is which committed entities the ghosts stand in for. MOVE
suppresses its originals, COPY suppresses nothing, and the viewport stays
ignorant of the difference.

### Two invariants

**It never touches the database or the undo journal.** That is what makes it
safe, and it is not a nicety — a mouse-move that reached the journal would make
every pixel of cursor travel an undo step. The construct is as much defined by
what it must not touch as by what it holds.

**It is derived, not stored.** Rebuilt whenever the tentative value changes,
never cached and invalidated. Cloning and transforming a selection is cheap;
tracking when a stored copy went stale is exactly the kind of bookkeeping that
produces a ghost of the wrong thing after an undo. ARRAY is the one case where
the rebuild cost is real, which is the argument for deferring it — not that it
would not help, since row and column spacing is pure trial and error and is
arguably where it helps most.

### What is actually missing

Two genuinely new pieces, and one that is only volume:

1. **A restyling channel on `Renderer`.** It is two virtuals —
   `begin_entity(EntityProps)` and `polyline()` — with no colour, style or
   width, and `EntityProps` has no highlight bit. Nothing can currently draw a
   subset of entities differently. `DashRenderer` is the precedent for the
   wrapper shape. This one is shared with selection highlighting, which is why
   both land together.

2. **A tentative value in the engine.** `CommandEngine::supply()` is built on
   "a value arrived and was consumed"; in-flight needs "here is a value that has
   *not* arrived" — the cursor is somewhere, the command has not been told. Small,
   but it is a new concept in the engine's model rather than an extension of one.

3. **Extraction across the editing commands.** Mechanical, and smaller than it
   looks — **checked, and the answer is good.** The worry was that the commit
   paths interleave deriving the change with writing it, which would have made
   the extraction a rewrite. They do not. Four of the five already split at
   exactly the right seam, with `next()` deriving the value and a private
   `apply()` taking it:

   | Command | Signature | `commands.hpp` |
   |---|---|---|
   | MOVE / COPY | `apply(ctx, const Vec3& delta)` | 359 |
   | ROTATE / SCALE / MIRROR | `apply(ctx, const Mat4& m, bool erase_originals)` | 394 |
   | ROTATE3D | `apply(ctx, double radians)` | 432 |
   | STRETCH | `apply(ctx, const Vec3& delta)` | 514 |

   And the bodies are already the right shape: `MoveCommand::apply` is a loop of
   `clone()`, `transform(m)`, then write. Splitting off "produce the modified
   clones" from "write them" is a handful of lines each, not a restructure.

   `TransformCommand`'s `erase_originals` is a nice confirmation that the shape
   is right — it is exactly `suppressed`, already parameterised, and it is the
   same distinction as MOVE-versus-COPY wearing MIRROR's clothes.

   **ARRAY is the one exception**: no `apply()`, just `ask_rows`/`ask_columns`/
   `ask_spacing`. It is the command already being deferred on repaint cost, so
   the outlier and the deferral are the same command.

Optional, and worth considering alongside rather than after: **cache the static
scene in a `QPixmap` and blit it**, drawing only the ghosts on top. This is the
modern form of what R12 got from XOR — rasterise the unchanging geometry once —
without the raster-op dependency, and it survives phase 14's move to
`QOpenGLWidget`. It matters most over X11 forwarding, where the repaint cost is
already felt.

**But it helps in-flight only, and does nothing for orbit or zoom.** Sadie's
observation, and worth keeping straight because the two feel identical from the
outside. A shift+middle orbit and a wheel zoom already repaint the whole database
per input event, which is why they drag over SSH — but they change the *view*
while the geometry stands still, so the cached raster is invalid on every frame
and there is nothing to reuse. In-flight is the mirror image: the camera holds
still and a handful of entities move, so one blit plus a few ghosts is the whole
frame. Same symptom, opposite cause, and only one of them has a cheap fix. Orbit
wants level-of-detail during the drag, or phase 14.

The other half of that observation: orbit is **not** a partial implementation of
in-flight, and there is no pending state in it to build on. What it does give is
the control-flow precedent — event, mutate one piece of widget state, `update()`,
let `paintEvent` re-derive everything — which in-flight follows exactly, with the
tentative value standing where the camera does. So it needs no new event
plumbing, only new state and a branch in the paint path. `paintEvent` also keeps
its unconditional full walk over the database; in-flight adds skipping the
suppressed handles on the way past, then a second short walk over the ghosts.

### Do it before phase 10's remainder and before grips

OFFSET, FILLET, CHAMFER and interactive grips are all still to be written. Each
one landing before this mechanism is another command to retrofit, and grips are
the same machinery wearing a different hat. The argument is not the sunk cost of
the eight that already exist — it is that the window to write the next four only
once is still open.

---

## Spline: what was left undone

The NURBS curve landed with its evaluator, interpolation, entity vtable, DXF
degrade and AutoLISP conversion. Four things were deliberately not done.

**The derived object snaps are now BUILT** — NEAREST, PERPENDICULAR and TANGENT
on SPLINE, ELLIPSE *and* POLYLINE, which turned out to have the same hole. Only
INT remains, and it is the structural one described immediately below. TANGENT to
a spline is approximate; its limits are recorded in their own section above.

**The deferred tangent is built too.** TANGENT as the FIRST point of a line was
not merely unresolved, it was *wrong*: `osnap_search` fell back to LASTPOINT when
there was no rubber-band base, so the tangent was computed against wherever the
user last clicked and then froze there. `OsnapQuery::from_point_is_base` now
separates a genuine base from that fallback; without one the hit is marked
`deferred` and carries its entity, and `LineCommand` solves the constraint when
the far end arrives. Preview solves it too, so the start slides along the curve
as the cursor moves. Where the user pointed is kept, because it is the only
information distinguishing the two tangents a curve offers.

Not done: **tangent-to-tangent**, where both ends of the line are deferred. That
is a different solve — a common tangent of two curves — rather than more of the
same. **Deferred PERPENDICULAR** is the same shape and is not built either; PER
without a base is still simply not offered.

The three shared one cause: `osnap_derived.cpp` dispatched through `as_line()`
and `as_circular()`, which answer for none of them, so all three snaps returned
false and the cursor silently offered nothing on the entity being pointed at.
POLYLINE is now solved segment by segment against the existing exact Line and Arc
solvers, so it is as accurate as the same geometry drawn as separate entities.
ELLIPSE and SPLINE go through one bisection root-finder over the curve parameter,
because all three snaps reduce to "find the roots of a scalar function of the
parameter" and writing that three times is how three tolerances end up
disagreeing.

## MTEXT — built, and why it is a database entity rather than a conversion

There were three honest answers and they trade differently. Leave it Proxy:
perfect round trip, invisible on screen. Convert to TEXT lines on import:
visible, but the paragraph is destroyed and can never be written back as one. Or
hold it exactly and degrade on write, which is what ELLIPSE and SPLINE already
do. Sadie took the third, and the deciding argument was that she had just been
bitten by exactly the second failure on ellipses.

**The raw string is what is held, inline codes and all.** Stripping them at read
time is the cheap option and it destroys the entity: a file opened and saved
would lose its formatting for good. Formatting is discarded at LAYOUT time
instead, where it costs nothing and is recomputed on demand.

**It could not have been built before the Hershey font.** Word wrap needs exact
advance widths, and `StrokeFont::width()` is what decides where a line breaks.
Before TEXT had a font there was no way to lay a paragraph out at all.

What the layout does with the codes: `\P` becomes a line, `\S1^2;` becomes the
flat `1/2` because there is no way to stack strokes at one height and deleting it
would silently lose a dimension, escapes become their literal characters, and
everything else is *consumed with its argument*. That last part is the one that
matters — a half-parsed `\H2.5x;` leaks "2.5x" into the visible text, which is the
failure that looks broken rather than merely plain.

Also settled here: `draw_text_line()` in `font.hpp` is shared by TEXT and MTEXT.
Two copies of glyph placement is how the two would come to disagree about what a
width factor means, and MTEXT's whole degrade rests on its layout matching what a
run of TEXT entities would draw.

**Known limits, none of them urgent.** One font at one height, so a font switch
or a height change inside a paragraph is discarded rather than honoured. Line
spacing is the 3-on-5 default scaled by group 44; the exact-distance mode (group
73 = 2) is not distinguished. Columns are ignored entirely.

## The save cycle — built, and the trap in the obvious design

**Dirty is answered by a SERIAL, not a depth.** Sadie proposed tracking the undo
position at the last save, or resetting a counter to zero on write and treating
negatives as dirty. Those are the same idea with different origins, and both
identify a state by how many groups deep it is — which is not unique:

> Save at depth five. Undo twice. Draw two new things. The redo stack is
> discarded by the new work, the depth is five again, and a depth comparison
> reports the drawing as saved while it holds two entirely different entities.

So each committed group carries a `serial`, never reused, and `mark_saved()`
records the serial on top of the undo stack rather than its size. A rebuilt
branch of the same depth carries different serials and stays dirty — correctly,
and until it is saved again. `redo()` *moves* a group rather than rebuilding it,
so serials survive any number of undo/redo round trips for free.
`tests/test_save.cpp` has the branch case; a depth-based answer passes every
other test in that file and fails only that one, which is how it would have
shipped.

**`Sysvars::set_metadata` exists because of a bug this caused.** DWGNAME and
DWGPREFIX are journalled writes if set the ordinary way, so SAVE recorded a
change of its own — the command's group closed *after* `mark_saved()`, pushed a
new serial, and the drawing was dirty the instant it was saved. Those two
describe where the drawing lives rather than what it contains, so they are
outside undo rather than escaping it. Anything that is drawing content still
goes through `set_owned`.

**`same_file()` exists because of another.** `~/x` and the absolute path are one
file spelled two ways, and a string comparison made QSAVE warn about overwriting
the drawing it had just written. Comparison and storage both normalise now.

Decided along the way: **DXFOUT stays an export.** It takes a name every time and
clears neither the drawing name nor the modified flag. R12's SAVE wrote DWG and
DXFOUT exported DXF; here DXF *is* the native format, so SAVE/SAVEAS/QSAVE are
the save. That leaves DXFOUT the jobs SAVE will not want anyway — a subset, or a
different DXF version once that lands. Modern AutoCAD folded both into
OPEN/SAVEAS, which is worth knowing but is not the same situation.

**Tab completion is GUI-only**, per `CLAUDE.md`'s standing rule that the terminal
gets no raw-mode work. Not yet built; it is a Qt widget concern.

### Known limits

- **NEW and OPEN do not reset the tables.** Layers, linetypes, blocks and UCS
  definitions survive both. `reset_drawing()` matches what `read_dxf_text` has
  always done, so this is consistent rather than new — but a genuinely new
  drawing should probably start with only layer 0.
- **DBMOD is not implemented.** R12's bit-coded modified flag would need every
  mutation to update it; the boolean is read from the journal instead.
- **The quit confirmation is Yes/No.** A third answer meaning "cancel the quit"
  is what No does here, since there is nothing else for it to mean at a prompt
  that only ends the session.

## Undo granularity, interruption, and the journal's missing ceiling

**A whole LISP evaluation is ONE undo step.** Sadie's call. A script is one
action from the user's side, so Escape then a single `U` puts the drawing back
however many commands and `entmake`s ran. `Interp::eval_string` opens a group
around the evaluation; groups already nested by depth, so a `(command ...)`
inside opens no second step.

`CLAUDE.md` claimed this already worked — "a LISP function calling
`(command "LINE" ...)` three times collapses into one step rather than three."
It did not. `begin_group` had exactly one caller, `CommandEngine::begin`. The
nesting mechanism was built and nothing used it.

**The wrap is guarded on `in_group()`, and that guard is load-bearing.** A
command can be SUSPENDED across two `(command ...)` calls — the case the whole
resumable-command design exists for. Wrapping unconditionally leaves the journal
at a depth nobody closes, and the work never commits at all. When a group is
already open the evaluation joins it, which is the right reading: it happened
while a command was running. `tests/test_wildcard.cpp` pins it.

### What was considered and NOT done: per-change undo

Sadie's worry was that a script doing millions of operations held as one undo
step could fail badly, with per-change undo as the escape hatch. Worth recording
why that is not the fix.

**Grouping costs nothing.** The journal stores before/after clones per CHANGE; a
group is only a marker saying where a step begins. A million `entmake`s cost the
same memory whether they are one group or a million — per-change undo would just
make recovery need a million `U`s.

**The real exposure is that the journal has no ceiling**, and that is true today
with or without grouping: `undo.hpp` says "unlimited, back to the start of the
session. Not a depth-limited ring."

And the shape that actually falls over is not bulk creation but MODIFICATION.
`AddEntity` keeps one clone of what was added; `ModifyEntity` keeps both before
and after. A loop nudging one entity a million times stores two million clones
of it while the drawing never grows.

**The fix, when it is wanted, is a budget rather than a granularity switch.** A
ceiling on changes or bytes in one group; when it is passed, stop recording and
say so plainly rather than growing until the process dies:

    Script exceeded the undo budget after 250,000 changes.
    This operation cannot be undone. (UNDOLIMIT to change.)

That turns an out-of-memory into a stated limitation the user was told about
before they reached for `U` — the same honest-degradation discipline the DXF
write follows. `UNDOLIMIT` as the sysvar, zero meaning unlimited.

Pairs with the **suppressed-journal bulk mode**, which `CLAUDE.md` already
anticipates from the performance side as "a suppressed-regen batch mode during
LISP loops". Suppressing the journal in the same mode is the honest answer for
pulling twenty thousand faces in from analysis data: that work does not belong
in the undo stack at all, and reloading the file is the recovery.

### Interruption

There was NO way to stop a running script. Nothing in the evaluator checked
anything, so `(while T ...)` ran until the process was killed — a hang rather
than a defect, and indistinguishable from one to whoever is waiting.

`Interp::interrupt()` sets a flag checked at the top of `eval()`. One site
rather than one per loop form, because a loop that is missed is a loop that
cannot be stopped, and the cost is a bool test per form.

**Not yet wired to anything.** The flag exists and is tested; no Escape handler
sets it. The GUI's Escape currently reaches `CommandEngine`, and routing it to
the interpreter as well is the remaining piece.

## AutoCAD called our R12 DXF corrupt — handles (FIXED, and confirmed)

**Confirmed by round trip.** With handles removed, an AutoCAD-written R12 DXF
opened in NotoCAD, saved out, and reopened in AutoCAD. Counts identical on both
sides: 19 BLOCK definitions, 1,643 nested INSERTs, 4 POLYLINE/SEQEND pairs, 1,408
VERTEX records, one top-level INSERT.

And AutoCAD's own file settled what it expects: `$HANDLING = 1`, `$HANDSEED =
11FA` above the maximum, and **3,081 distinct handles out of 3,082** — a unique
one on every record including each VERTEX. That is precisely what ours failed to
do. Writing none is the other legal R12 option and is what we now do.


Reported from use, and NOT a regression despite looking like one. The cause was
latent from the day polylines could be written, and the sample drawing
`gen_sample` emits contains no polylines at all — which is why "verified to open
in AutoCAD 2026" never touched the path.

**A POLYLINE's VERTEX and SEQEND records are not database entities.** They have
no handles of their own, so they were emitted carrying the PARENT'S handle: a
degraded ellipse wrote eighteen records all claiming to be handle 6. Measured on
a drawing with one of every entity type: 106 handle records, 11 distinct.

Handles must be unique, and the header declared `$HANDLING = 1`, which tells the
reader to check. `$HANDSEED` was wrong too — it is the *next* handle to allocate
and must exceed every one present, and ours equalled the maximum.

**The fix is to write no handles at all.** R12 made them optional, `$HANDLING`
defaults to 0, and most R12 files carry none; they became mandatory only in R13.
Writing them correctly would mean allocating handles for subordinate records at
write time and knowing the total before the header is emitted — a two-pass write,
for something **nothing reads**: `dxf_read` ignores group 5 entirely and assigns
fresh handles on load.

Two lines saved on every record as well, which on a drawing of a million polyline
vertices is not a rounding error. The one-of-everything file went from 9,047 to
8,397 bytes; on vertex-heavy output the saving is proportionally much larger.

**Consequence for later DXF versions:** R13 and up REQUIRE handles, so a
version-aware writer has to solve the allocation properly — subordinate records
need their own, and `$HANDSEED` has to be known before the header is written.
That is a real constraint on the writer's shape, and it is why it is recorded
here rather than in the commit alone.

Pinned by two tests: no group 5 is emitted and the header does not claim any, and
every VERTEX sits inside a POLYLINE that a SEQEND closes with no other entity
interrupting the sequence. The second is the other way a file gets called corrupt
and the degrading entities are the ones that emit those records.

## Rendering cost — measured, and the three things gdb found

Sadie made a polar array of a spline, 100 copies, then a 10,000-element
rectangular array of that. A million splines, and the viewport wedged. Three
gdb samples of the live process all landed in the same place:

    #8  basis_functions (...) at src/core/spline.cpp:73
    #9  Spline::point_at (...)
    #10 Spline::draw (...)
    #11 draw_database (...)
    #12 ViewportWidget::paintEvent (...)

Not thrashing and not stuck -- spinning in the repaint. Three separable causes,
all now fixed, measured on a headless benchmark that flattens and projects
without Qt:

| | before | after |
|---|---|---|
| 20,000 splines | 48.1 ms/frame | 27.7 ms |
| 200,000 splines | 498.2 ms/frame | 198.7 ms |

**1. `basis_functions` allocated three vectors per call**, and `point_at` a
fourth for the result. That is four heap operations for every evaluated point of
every spline in every frame -- a million a frame at twenty thousand splines, and
the reason every gdb sample was inside the allocator. Now stack arrays, bounded
by `kMaxSplineDegree`, which is what the bound exists for.

**2. `Viewport::project` recomputed `basis()` per point** -- two sines, two
cosines and a cross product, 340,000 times a frame. Now cached against the two
angles it derives from. The cache VALIDATES ITSELF rather than being invalidated
by the mutators: a dirty flag needs every setter to remember, and the one that
forgot would render everything through a stale basis, which looks like corrupt
geometry rather than a missing invalidation.

**3. `segment_count` had a floor and no ceiling.** The floor of
`control_points * 4` exists so a wiggly curve is not drawn as a straight line,
and it was binding at every zoom -- sixteen segments for a curve three pixels
long. Now bounded by the on-screen size as well.

That bound had to be sized by the BOUNDING BOX, not the control polygon. The
polygon is a deliberate over-estimate of curve length -- an interpolating spline
overshoots, so its control polygon can be three times the curve -- and the first
version of this used it and never bit. Worth remembering: the two are not
interchangeable, however close they look.

### View culling — now built

Sadie asked the right question: was it painting things outside the viewport? It
was. `draw_database` walked every entity and called `draw()` whatever the view
showed, and QPainter discarded the pixels *after* the curve had been flattened
and every point projected.

`DrawContext` now carries an optional clip, and `draw_database` tests each
entity's `bbox()` against it. Zoomed into a working view of 200,000 splines:

| | before | after |
|---|---|---|
| polylines drawn | 200,000 | 252 |
| points | 2,200,000 | 4,284 |
| frame | ~200 ms | 35.7 ms |

**The clip is VIEW-SPACE, and that is not fussiness.** An orthographic view
turned off axis has no useful world-space bounding box: the visible volume is an
infinite slab, so a world AABB test either culls nothing or culls things plainly
on screen. Two axes and two intervals describe the region exactly. The test
itself uses the standard AABB-along-a-direction extent — the centre's projection
give or take the half-extents weighted by the direction's components — so it is
two dot products rather than eight corner projections.

**Nothing but `draw_database` consults it.** `Entity::draw` ignores the clip
entirely, so picking, the hit-test probes and the DXF write are untouched, and a
default-constructed `DrawContext` has no clip at all. There is a test for that
last part, because it is what those paths silently depend on.

### Found while doing it: the skip list was quadratic

`draw_database`'s skip overload did a `std::find` over the skip vector for every
entity in the drawing. `skip` is `flight.suppressed` — the entities hidden behind
ghosts — so it is non-empty *exactly while a selection is being dragged*. A
thousand entities moving in a drawing of a million was a billion comparisons a
frame. Sorted once and binary-searched now. The case where responsiveness matters
most was the one that scaled worst.

### What remains

The 35.7 ms of the zoomed-in frame is almost entirely the linear scan calling
`bbox()` on all 200,000 entities to decide what to skip. That is what a spatial
index fixes, and it is open question 5, which now has a number against it rather
than a feeling.

At extents a million entities is still roughly 2 million points and about a
second. Culling cannot help when everything genuinely is on screen; that case
wants level-of-detail — below some size, stop drawing entities individually and
draw the fact that something is there.

## TANGENT to a spline is approximate — known, accepted, not yet worth fixing

Sadie's call: it makes mistakes, and at the frequency she will use it that is
fine for now. She will say when it is time. Recorded so the fix starts from what
is actually wrong rather than from a rediscovery.

The solver samples the parameter domain and bisects the sign changes of
`dot(cross(C(u) - ref, C'(u)), n)`. Four ways that is not exact:

1. **A root of even multiplicity is invisible to a sign change.** Where the
   curve grazes tangency without crossing it — an inflection lining up with the
   reference point — the function touches zero and comes back the same side, and
   the scan steps straight over it. This is the most likely cause of a *missing*
   answer.
2. **Two roots inside one sample interval cancel.** The sample count is
   `control_points * 8`, clamped to [64, 2048], which is a heuristic and not a
   bound. A tight wiggle relative to the control polygon can hide a pair.
3. **Only two are returned.** `kMaxTangents` is 2, which is exactly right for a
   conic and arbitrary for a spline. The two nearest the reference win, so a
   third that the user wanted is silently unavailable.
4. **The non-planar case is not really defined.** The test uses the entity's
   normal, which only means something for a planar spline. For a genuinely 3D
   curve a tangent line through an arbitrary point usually does not exist at
   all, and this will report the places where the chord happens to be parallel
   to the tangent *as projected*, which is not the same thing. If splines start
   being built out of plane, this is the part that is wrong rather than
   imprecise.

None of it affects ELLIPSE, whose tangent is closed form, or POLYLINE, whose
arcs go through the exact circular solver.

The honest fix for 1 and 2 is to bracket on the derivative as well as the value,
or to subdivide against the control polygon's convex hull so the sample count is
a bound rather than a guess. For 4 it is to decide what a 3D tangent snap should
even mean before implementing one.

**The ellipse tangent is closed form, not iterated.** The chord of contact — the
polar line of the reference point — meets the ellipse exactly where the tangents
touch, which reduces to one `acos`. Worth knowing because AutoCAD 2026 gets this
case visibly wrong and is inconsistent about tangents to a spline: there is no
reference implementation worth matching here, so the tests check the defining
condition instead, with curve directions from finite differences so they do not
borrow the algebra they are checking.

Two rules fell out that are not obvious:

- **PERPENDICULAR on a polyline needs its own search.** The Line solver is
  unclamped by design, so a segment reports feet far past its own ends — and
  being unclamped those are often the *nearer* answer, so nearest-wins picks
  geometry the polyline does not draw. A foot on its own segment therefore beats
  one off the end, and only when no segment has a real foot do the extended ones
  compete.
- **NEAREST keeps the endpoints, PERPENDICULAR drops them.** NEA stays on the
  entity, so a reference past the end of an elliptical arc answers with the end,
  exactly as `circular_nearest` already did. An endpoint is not a perpendicular
  foot.

**No intersection support, so TRIM, BREAK, EXTEND and the INT osnap ignore it.**
`intersect.cpp`'s `SubCurve` models a line segment or a circular arc and nothing
else, so a NURBS span fits neither — this is a structural change to that file
rather than a new case label, which is why it was not smuggled in. The
behaviour meanwhile is safe: `decompose()` returns an empty vector, so
`intersect()` reports no hits and the cutting functions return null. Nothing
misbehaves; splines are simply invisible to those commands.

**Ellipse is in exactly the same state**, which was not noticed when it landed
and is worth saying plainly: it appears nowhere in `intersect.cpp`,
`curve_edit.cpp` or `osnap_derived.cpp` either. So neither new entity can be
trimmed, broken, or snapped to with NEAREST, PERPENDICULAR or TANGENT.

The likely shape of the fix, when it happens: give `SubCurve` a third form that
carries a flattened polyline with its parameter mapping, and let any curve that
cannot describe itself exactly decompose into that. It costs exactness at the
intersection — the answer becomes as good as the flattening — which is a real
decision and the reason it is recorded rather than assumed.

**NEAREST is the osnap users will miss first** on both entities. It needs a
projection onto the curve, which for a spline is a Newton iteration on the
distance function. Bounded work, and independent of the intersection question.

**The interpolation solver is dense.** The system is banded with bandwidth
degree+1 and a banded solve would make it O(n) rather than O(n^3). For a spline
picked by hand — tens of points — the difference is unmeasurable. It would start
to matter for a spline generated from analysis data, which is phase 13's
territory and does not exist yet.

**The tangent is by finite difference**, not by the analytic derivative. For a
rational curve that derivative is the quotient rule over two B-spline
derivatives, and the result is normalised away immediately. If anything ever
needs curvature rather than direction, this is where it goes.

## DXF versions later than R12

**READING them is BUILT. Writing them is still wanted, not now.** Sadie asked for
import first and called it an asymmetric ask; it is, and the asymmetry is the
right way round -- read what everyone emits, guarantee what everyone can read.

What made it cheap: the reader was already structurally version-tolerant, and
nothing had been written for that. HEADER, CLASSES and OBJECTS are stepped over
because the section loop only recognises TABLES, ENTITIES and BLOCKS; group 100
subclass markers and group 5 handles are ignored because entities are built by
asking for the codes they want rather than by walking every group; and anything
with no class here becomes a Proxy. A 2018 file therefore already imported, with
its modern entities landing as proxies. The work was a list of entity mappings.

Read natively now: **ELLIPSE**, **LWPOLYLINE**, **SPLINE**. The first and last
close a real hole rather than adding a feature -- the database has held both
exactly since they were built and could not read either back, so a round trip
through DXF turned an ellipse into a polyline *permanently*. That is what Sadie
hit when a saved-and-reloaded ellipse snapped like a polyline, because it was
one. LWPOLYLINE matters for a duller reason: modern AutoCAD writes it where R12
wrote POLYLINE, so it is most polylines in most files.

Three details worth not rediscovering:

- **An ELLIPSE's centre and major axis are WORLD, not ECS**, and the major axis
  is a vector from the centre rather than a point. CIRCLE and ARC in the same
  file are ECS. Getting it wrong leaves a flat ellipse perfect and puts a tilted
  one somewhere else entirely.
- **An LWPOLYLINE's height is group 38, one elevation for the whole entity**, not
  a z per vertex. Ignoring it flattens the drawing silently.
- **Repeated group codes need the groups walked in order.** `EntityGroups::real`
  returns the first match, which is right for every R12 entity and wrong for
  every vertex list and knot vector.

**MTEXT is now a real entity too**, on the same terms. See its own section below.

Still Proxy on purpose: HATCH, DIMENSION, LEADER, TABLE, IMAGE and the ACIS
entities. Each needs a decision about what it degrades *to*, and guessing is
worse than an honest passthrough.

Not looked at yet: **binary DXF**, which modern AutoCAD can write and this reader
would garble rather than refuse -- it should detect the sentinel and say so. And
**encoding**: AC1021 and later are UTF-8 where R12 was ANSI, which matters the
moment text is non-ASCII, since the bundled Hershey font covers ASCII only.

### Writing later versions — BUILT (AC1015)

`DXFVERSION` chooses: `"R12"` or `"R2000"`. SAVE, SAVEAS, QSAVE and DXFOUT all
follow it, and DXFOUT names the revision it wrote in its reply, since that is the
one thing a user cannot see from the filename.

**One writer with a version, not two.** Every entity already knew how to write
itself at R12; the version picks between that and a native form. Entities branch
on `dxf_has_modern_entities(w.version())` rather than on the enum, so adding a
version later does not mean revisiting every entity.

**The payoff, measured** on a drawing holding one of everything: 8,397 bytes at
R12, **4,525 at R2000** — and the R2000 file is lossless where the R12 one is
not. ELLIPSE, SPLINE and MTEXT go out as themselves instead of as a
tessellation, a tessellation and a run of TEXT records.

**But R2000 is not always smaller.** A drawing with none of those three gets
*larger*: handles, owner pointers and subclass markers on every record are pure
overhead when there is nothing they enable. `Drawing8.dxf` went 387K at R12 to
449K at R2000. The rule is that R2000 pays when the drawing holds entities R12
cannot name, and costs when it does not.

#### What R2000 needed beyond R12

Mostly structure rather than entities, and it is worth listing because none of it
is optional:

- **Handles on every record**, unique, including VERTEX and SEQEND. `$HANDSEED`
  must clear them all — and it lives in the HEADER, which is written first, so
  the document is written **twice**: once to a null sink to count, then for real.
  Two passes rather than buffering, because a drawing that fills a gigabyte
  should not need a second gigabyte to be saved.
- **A BLOCK_RECORD table**, with `*Model_Space` and `*Paper_Space` entries. The
  model space record's handle is captured as it is written, because every entity
  names it as owner and an entity owned by nothing is rejected.
- **Owner pointers** (group 330) on every entity and table record.
- **Subclass markers** (group 100), `AcDbEntity` plus the per-entity class.
- **CLASSES and OBJECTS sections**, and VPORT, VIEW and DIMSTYLE tables. CLASSES
  is legitimately empty here: it describes application-defined classes and this
  program defines none.

#### DXFVERSION is NOT saved in the drawing

It says how to *write* a file, not anything about the drawing's contents. Marking
it `save_in_drawing` means OPEN resets it, so choosing R2000 and then opening
something silently reverts to R12 — which it did, once, before the flag was
corrected.

#### CONFIRMED IN AUTOCAD, and the six faults it took to get there

`tests/acad/r2000_conformance.lsp` — 43 entities, one of every type we can
make, opened in AutoCAD 2026 and listed clean. MTEXT, both ELLIPSEs and both
SPLINEs arrive as themselves; the 3D spline reports **Non-Planar**, the bulge
lands on its segment's starting vertex with the right centre and radius, the
SOLID's bowtie corner order survives, and all three TEXT justifications come
back as start/center/end point.

**The arbitrary axis algorithm agrees with AutoCAD's to four decimals.** A
circle with extrusion (0, 0.4472, 0.8944) gives Ax = (-1,0,0) and
Ay = (0,-0.8944,0.4472), so ECS (98,26) is world (-98, -23.2544, 11.6272).
AutoCAD lists -98.0000, -23.2551, 11.6276. That is the second independent
check `CLAUDE.md` asks for, on the piece it calls foundational.

Every one of the six rejections was **structure, not geometry** — the same
pattern as the first five. In order, and each pinned by a test:

| AutoCAD said | It was |
|---|---|
| `Missing group code 49 in complex linetype` | Every LTYPE dash length needs a group 74 element type after it. The message names the group that IS there. |
| `Class separator for class AcDbLine expected` | Only ELLIPSE, SPLINE and MTEXT declared a subclass. Every R12-era entity stopped at AcDbEntity. |
| `Class separator for class AcDbText expected` | The markers were present and mis-ordered: layer, linetype and colour are AcDbEntity's and must precede the concrete marker. AutoCAD tolerated this for a LINE and refused it for a TEXT. |
| `Unexpected DXF group code: 210` | An ARC's extrusion is AcDbCircle's; only the angles are AcDbArc's. |
| `Class separator for class AcDbMInsertBlock expected` ×3 | Unsolved. See below. |

Two rules fell out that are worth more than the individual fixes:

- **The shape of a record must not depend on its content.** An optional group
  omitted immediately before a class separator leaves the reader inside the
  parent class. TEXT's group 73 and an INSERT's scale and rotation are written
  unconditionally at R2000 for this reason.
- **Presence is not the property; position is.** A reader being lenient about
  one entity says nothing about the next.

And the blind spot that let all of it through: `add_one_of_each()` in
`tests/test_dxf.cpp` **had no polyline**. POLYLINE is the only entity that
writes subordinate records, so every structural check walked straight past the
VERTEX and SEQEND records — which at R2000 had no handles at all. Exactly the
blind spot that hid the R12 handle bug, where `gen_sample` had no polylines
either. It has one now.

#### Still not done: MINSERT

AutoCAD refuses it with `Class separator for class AcDbMInsertBlock expected`,
and has refused **all three placements** of that separator: before the array
fields; before them with every parent field written out first; and after them at
the end of the record. Each attempt was reasoning rather than evidence and none
produced any.

It is held out of the conformance drawing by `*cf-minsert*` — not abandoned, but
refusing to let one unsolved record hide the state of the other forty-two, which
is precisely what it was doing. **What it needs is a MINSERT written by AutoCAD
itself as R2000, to match byte for byte.** Guessing has now cost three rounds.

**Wanted eventually, not now:** Sadie's: modern AutoCAD offers a range of write
formats and still lists R12 in 2026, so being able to emit AC1015 or later is
warranted — but nothing needs it yet, and AC1009 remains the interchange
guarantee.

## DXF size for spline arrays — MEASURED, and we are smaller than AutoCAD

The worry was that our output was bloated. It is not. Measured on 320,000
splines, written by us and then re-saved by AutoCAD 2026 after reading ours, so
the geometry is held constant.

**Two things this comparison is NOT**, both worth stating because the obvious
reading of the table below overclaims:

- **It is not AC1015-era behaviour.** This is AutoCAD **2026** asked to write
  AC1015, which is not the same thing as what AutoCAD 2000 wrote, and we have no
  R2000-era reference to check against. So "AutoCAD writes the tolerances and
  the extrusion unconditionally" is a fact about a modern writer emitting a
  legacy format, not a fact about the format. It is still the right thing to
  match — a current AutoCAD is the reader that actually has to accept our files,
  and it is the strictest one we have access to — but it does not license
  conclusions about what AC1015 *requires*. A 2026-native file is AC1032 and
  would look different again.
- **It supersedes the informal size differences seen earlier**, which compared
  similar DESIGNS rather than the same data. Those were never like-for-like and
  should not be reasoned from. This is the first controlled measurement: one
  drawing, two writers, identical geometry.

| | ours | AutoCAD |
|---|---|---|
| file | **518.9 MB** | 658.9 MB |
| per spline | **1,437 B** | 1,847 B |
| control points | 11 | 13 |
| knots | 15 | 17 |
| fit points | 11 | 11 |
| extrusion 210/220/230 | omitted at default | always |
| tolerances 42/43/44 | omitted | always |
| knots | normalised 0→1 | chord length, 0→17.9… |

**So there is nothing to fix in the writer**, and the R12-tessellation and
MINSERT theories that were recorded here before the measurement were both wrong
about where the bytes went. Both files are almost entirely SPLINE records
(460 MB of ours, 591 MB of AutoCAD's); the entity count is identical.

### The real finding is fidelity, not size

**AutoCAD re-solves the curve from our fit points and discards our control
polygon.** It produced 13 control points where we wrote 11, and 7 where we wrote
5 in the conformance drawing — we give n control points for n fit points, it
gives n+2. Different end conditions. The curve still passes through every fit
point; between them it is AutoCAD's interpolation rather than ours.

Our own round trip IS exact, and only because the re-solve is deterministic and
uses our solver: a five-point spline written, read and rewritten has identical
control points. That is worth knowing before trusting it — the exactness comes
from both ends being us, not from the file.

### One change would fix both, and it is a decision

**Omitting group 74 makes the curve exact everywhere and the file ~36% smaller.**
With no fit points the control polygon and knots are the only definition, so no
reader can re-solve and every reader draws our curve. Fit points are 33 of the
92 groups in a record.

What it costs: the drawing forgets which points the user picked. That is real —
it is what PEDIT's refitting would want, and re-deriving it from the control
polygon is not the same information.

Writing both is what AutoCAD does and is not wrong. But it is worth being
explicit that the cost of writing both is paid in cross-application fidelity,
which is exactly the thing AC1015 was adopted to buy. Do it when SPLINE editing
is on the table rather than now.

## A point from LISP is WORLD; a point typed at the prompt is UCS

Found by the conformance drawing rather than by the DXF: station 9 sets a
rotated UCS and draws at (0,0), and the geometry came back at the **world**
origin. The UCS command worked; the points bypassed it.

`src/app/prompt.cpp:154` fetches `db_->current_ucs().to_world()` and hands it to
the point parser, so anything typed is interpreted in the current UCS.
`src/lisp/command_input.cpp:82` calls `InputValue::of_point(p)` with no
conversion at all. So `(command "LINE" '(0 0 0))` and typing `0,0` at the same
prompt mean different points whenever a UCS is active.

**AutoCAD interprets `(command ...)` points in the current UCS**, so this is a
divergence rather than a choice — but it collides with one that WAS chosen.
`features.md` records `polar`, `distance` and `angle` as deliberately
world-coordinate, on the reasoning that the geometry layer has no UCS in it and
doing them in UCS would make `(command "LINE" (polar p a d))` silently mix two
frames. That argument assumed command points were world too. They are not, in
AutoCAD, and the mixing it warns about is what we have: `polar` returns world,
the prompt reads UCS.

**DEFERRED, leaning toward option 1.** Sadie's call: the designer convenience
at the UI does not extend to the API, that split is AutoLISP's own and is
coherent — commands are the user-facing layer where a UCS is a convenience, the
database is storage where world is canonical. So align with AutoLISP when it is
picked up, and punt to the community if one forms around the project. Nothing
is blocked on it; station 9 of the conformance drawing is the standing test.

One correction to the reasoning below, since it changes what the work is:
**`polar`, `distance` and `angle` are frame-AGNOSTIC.** They are trigonometry on
three numbers and return whatever frame they were given. `features.md` records
them as "world-coordinate rather than UCS" to avoid
`(command "LINE" (polar p a d))` mixing frames — but that mixing comes from
where `p` came from, not from `polar`. So converting at the LISP boundary needs
no change to any of them, and option 1 is smaller than it looks.

Three ways out, and it needs deciding rather than drifting:

1. **Convert at the LISP boundary** — `command_input.cpp` applies
   `current_ucs().to_world()` like the terminal does. Matches AutoCAD, and makes
   `(polar)` the odd one out, which is what `trans` exists to resolve.
2. **Leave it world and say so.** Defensible for a tool whose stated purpose is
   generating geometry from analysis data, where there is no UCS and world is
   what the data is in. But it silently breaks any classic script that sets a
   UCS.
3. **`trans`, and make the caller choose.** AutoLISP's own answer, and the
   honest one, but it does not decide what the DEFAULT is — which is the actual
   question here.

Note that this is the same trap `CLAUDE.md` already records for tests — "a test
calling `InputValue::of_point()` bypasses UCS conversion". It is not only tests.
Station 9 of the conformance drawing is a standing regression test for whichever
answer is chosen.

What makes it worth recording rather than assuming: the entity set has begun to
outgrow AC1009. `Ellipse` is held exactly in the database and degrades to a
polyline on write, and a spline will do the same. That degradation is honest but
**lossy** — an ellipse written and read back is a polyline, and nothing can
recover what it was. A later DXF version is the only thing that fixes it, since
AC1009 has no group codes to say it in.

The shape it should take, when it happens: a target *version* on the writer
rather than a second writer. Each entity already knows how to write itself at
R12; the version picks between that and a native form. `write_common_as()`
exists for exactly this reason — it is how ELLIPSE writes itself under
POLYLINE's name today, and the seam a version switch would use.

Two things to settle at that point and not before: whether the version is a
system variable, a DXFOUT option, or both; and whether reading a later version
is in scope at all, since a reader is a much larger job than a writer and the
Proxy entity already keeps unknown entities safe on the way through.

## REDRAW and REGEN are not wanted, and the reason is structural

Raised by Sadie: do we even need them? No — and it is worth writing down why,
because "R12 had it" is otherwise a standing argument for building it.

R12 kept a **display list** of vectors separate from the database. Circles were
tessellated once at `VIEWRES` and stored, so zooming in did not retessellate and
they went visibly blocky until you regenerated. REGEN rebuilt that list from the
database; REDRAW merely repainted from it, clearing blipmarks and the holes an
erase left behind. Both commands exist to manage a cache.

There is no cache here. `paintEvent` walks the database every frame, and
`DrawContext::chord_tolerance` comes from `world_per_pixel()`, so tessellation is
recomputed at the current zoom on every repaint. Circles cannot go blocky.
REGEN would have nothing to do and REDRAW nothing to clear, and a command that
silently does nothing is worse than an absent one — it teaches the user it did
something.

Modern AutoCAD still has both, and REGEN is still occasionally needed there, for
exactly the same reason: it still keeps a display list.

**The corollary, which is the part worth remembering:** if the `QPixmap` scene
cache ever lands, we will have reinvented the display list, and REDRAW becomes
meaningful again as its invalidation. So this is "not until there is a cache",
not "never". `REGENAUTO` and `VIEWRES` follow the same reasoning and the same
condition.

---

## TEXT: bundle a Hershey font, and it is not a hack — BUILT

Open question 2 has been "SHX, bundle a vector font, or approximate on screen"
since phase 7. Sadie's memory that R12's text was "simple pure stroke" is right,
and it settles the question rather than dodging it.

R12's SHX files are compiled shape definitions, and the good ones — `romans`,
`romand`, `italicc` — are single-stroke vector fonts descended from the
**Hershey** set (A.V. Hershey, US National Bureau of Standards, 1967). A glyph is
literally a list of polylines, which is exactly what `Entity::draw()` emits. So
bundling Hershey is not an approximation of what R12 did; it is a
reimplementation of the same thing from the same ancestry.

It also satisfies the constraint that ruled out the third option: the core stays
headless. No Qt fonts, so `CLAUDE.md`'s "rendering the same database two ways is
a correctness check" survives, and DXF-written TEXT and screen TEXT come from one
source.

And it does not preclude doing it "properly" later. An SHX parser — which would
let a drawing use the user's own AutoCAD fonts — sits behind the same
glyph-to-polylines interface. Same layering as DXF-first with DWG optional: the
in-tree implementation is complete on its own, and the compatibility path is an
addition rather than a rewrite.

**The licence was read, not assumed, and it is clear.** The notice permits use by
anyone for any purpose, commercial or otherwise, on two conditions: the
acknowledgements to Dr. A. V. Hershey and to James Hurt of Cognition, Inc. must be
distributed *with the font data*, and the data may be converted to any format except
the one distributed by the U.S. NTIS. Both are satisfied. The first attaches to the
source tree rather than to the binary, which is why the verbatim notice lives in
`third_party/hershey/HERSHEY-NOTICE.txt` rather than only in a comment. Nothing here
is copyleft, so unlike DWG and Qt it needs no compile-time module boundary.

**What was built.** `include/ncad/font.hpp` and `src/core/font_hershey.cpp`: Roman
Simplex embedded as `.jhf` text and parsed on first use, so the core keeps no runtime
data path and the tests need no fixtures. Glyphs arrive with the baseline at y = 0 and
the cap height at y = 1, so `Text::height()` means cap height exactly as R12 says.

Three things fell out of having real metrics, all of which had been fudged:
`Text::text_width()` is now exact rather than a 0.6-aspect guess (and was renamed from
`approximate_width()` because the old name would have become a lie); `bbox()` reserves
the descender; and **Middle and MC stopped being the same thing** — AutoCAD's Middle
centres on the text *including* descenders while MC centres on the uppercase height,
a distinction that is inexpressible without a font and that coincides for any string
without a descender, which is how it survives casual testing.

`romand` and `timesi` are vendored but unused: R12 reaches fonts through the STYLE
table, which does not exist yet, and building one font end to end first kept that from
becoming a second project inside this one.

**Consequence for dimensioning:** TEXT is a prerequisite, and it is now unblocked
in principle. But see MEASUREGEOM below: most of what dimensions were wanted for
turned out not to need them.

---

## The viewport cannot zoom out past 1e12

**Recorded, not fixed. Deliberately parked** -- Sadie's call, and right: the
geometry is correct at that size, only the view is limited, and nothing in the
stated workflow needs it yet.

`kMaxViewHeight = 1e12` (`src/core/viewport.cpp:18`), clamped in
`set_view_height()`. A drawing taller than that cannot be zoomed out far enough
to see, so ZOOM Extents saturates and the rest falls off the edges. It reads as
clipping and is not: `Viewport::project()` is a plain parallel projection with
no near or far plane anywhere in it. The kernel is fine at that size --
`test_numeric.cpp` has `curve_point_at` within 1-2 ULP at 1e12 -- so this is a
display limit sitting on top of arithmetic that does not have one.

`kMinViewHeight` next to it carries a stated reason ("a long enough zoom-in
reaches denormals"). The maximum does not, and is probably just conservative.
What to check before raising it, rather than guessing: `world_per_pixel()`
inverts to a scale factor, and `project()` multiplies a world-sized offset by
it, so the question is where that product stops being exact -- not whether it
overflows, which at 1e16 it plainly does not. Tessellation is not a concern:
`draw_context()` derives the chord tolerance from `world_per_pixel()`, so a
coarser view asks for coarser curves by construction.

**The bar, measured in AutoCAD 2026 by Sadie.** No such limit there. A circle of
radius 1e16 placed that far from the origin reports its quadrant at the origin,
while drawing about 1.2274 units off it:

| coordinate | offset | ULP there | in ULP | relative |
|---|---|---|---|---|
| 1e12 | 0.0001 | 1.221e-4 | 0.82 | 1.00e-16 |
| 1e16 | 1.2274 | 2.0 | 0.61 | 1.23e-16 |

Both are BELOW one ULP -- at 1e16 the representable spacing is 2.0 units, so the
offset is smaller than the smallest distinguishable one -- and the relative error
holds at about half of 2^-52 across four orders. Correctly rounded, not merely
close. Together with the closed-chain measurement (`tests/acad/t2_chain.lsp`),
which showed `err/L` constant from L = 1 to 1e9, that is scale invariance
demonstrated over sixteen orders of magnitude, and it is the standard the
tolerance work is aimed at.

**Unverified:** whether R12 itself had a comparable view limit. Plausible, and it
would make the current behaviour faithful rather than merely restrictive -- but
it is a guess, and the difference decides whether raising the cap is a fix or a
divergence.

## Window and crossing boxes assume plan view

`SelectionPrompter` builds its region on world XY unless told otherwise. That is
correct in plan view — which is the overwhelming majority of real work — and wrong
under orbit, where a screen-aligned box is not axis-aligned in world space.

`set_view_axes()` exists and nothing calls it: the viewport widget cannot reach the
prompter, which is private to each command. Fixing it properly means deciding how view
information reaches a command, and `CommandContext`'s header is explicit that it holds
no view. Options are a third context member, or `Prompt` carrying the frame out and the
answer carrying it back.

**This does not affect ROTATE, MIRROR or ARRAY**, contrary to an earlier note here.
Those act in the current construction plane (WCS/UCS), not in the view: in AutoCAD you
can only draw in the current plane, so a rotation collapses to a base point plus an
angle and a mirror to a point pair, with the axis and the plane implied. World XY is
therefore *correct* for them until UCS exists, not a stand-in. Only selection windows
are genuinely screen-space, because a window is a thing you drag on the screen.

## ARRAY details taken from reasoning, not from the R12 manual

Two behaviours in polar ARRAY were implemented as the sensible reading and want
checking against the documentation:

- **Item spacing.** A full 360° fill divides the angle by the item count, so the last
  item does not land on the first. A partial fill divides by one less, so the first and
  last sit on the ends of the arc. Both are defensible; whether R12 does the second is
  unverified.
- **The reference point when items are not rotated.** Each item keeps its orientation
  while its position travels the arc, and the point that follows the arc is the
  selection's bounding-box centre. R12 uses an object base point, which for LINE,
  CIRCLE and ARC amounts to much the same thing but will diverge for TEXT and INSERT,
  where the base point is a defined property rather than a derived one.

## STRETCH details taken from reasoning, not from the R12 manual

*Confirmed correct and not to be "fixed": STRETCH will happily pull an object out of
the construction plane, because the displacement is a full 3D vector and the grip
mechanism applies it as one. Modern AutoCAD does the same. Whether R12 did is untested
and does not matter — this is the behaviour we want.*

- **An arc caught by its centre alone does nothing.** Only Stretch-kind grips are
  eligible, which for an arc means its endpoints; the centre is a Move grip. Whether
  R12 moves an arc whose centre falls inside the crossing window is unverified.
- **A circle uses its centre**, because it has no Stretch grips at all and falls back
  to its Move grip. That matches R12's behaviour of moving a circle when its centre is
  inside, and it also means the crossing box has to cross the rim *and* contain the
  centre — crossing the rim alone selects the circle but moves nothing.

## How a command reaches the view — settled

`ViewControl`, an abstract interface in the core, reached through a nullable
`CommandContext::view`. The Qt shell implements it over its real `Viewport`; `ncad`
leaves it null and commands that need a view say so rather than pretending. Decided
and built when PLAN needed it.

This unblocks the rest of phase 6's view half. Still to write, all straightforward on
top of the interface: **ZOOM** (All, Center, Dynamic, Extents, Left, Previous, Window,
scale), **PAN**, **VIEW**, **REGEN**, **REDRAW**. The ViewControl methods for zoom and
pan already exist and are implemented in the widget; only the commands are missing.

`SelectionPrompter::set_view_axes()` can now be fed from `ctx.view->view_basis()`,
which fixes selection windows under orbit. Not done yet — the commands each own a
prompter privately, so something has to push the basis in when selection starts.

ZOOM's All and Extents are currently the same thing. They diverge once LIMITS exists:
All shows the limits or the extents, whichever is larger. Offering two words for one
behaviour is honest only while that is temporary — phase 8.

**Interactive AutoLISP input** — `entsel` and the `get*` family — should follow the
same shape: an abstract `UserInput` with `bool ask(const Prompt&, InputValue&)`,
implemented by `ncad` over stdin and by the GUI over a nested `QEventLoop`. The
re-entrancy of a nested loop is the part that wants care, not the interface.

## CONTINUOUS currently doubles as BYLAYER

`EntityProps::linetype` defaults to `kLinetypeContinuous`, and `DashRenderer` treats
that as "take the layer's linetype". Nothing can yet express an entity that is
explicitly continuous while sitting on a dashed layer, because there is no BYLAYER
marker distinct from the CONTINUOUS table entry.

It matters as soon as DXF read lands: an R12 file records the string `BYLAYER` in group
6, and an entity that says `CONTINUOUS` means it. The fix is a reserved id — a
`kLinetypeByLayer` sentinel that resolves at render time — and it is small, but it
changes what a default-constructed `EntityProps` means, so it wants doing deliberately
rather than in passing.

## Blocks — built, and what open question 7 turned out to be

`blocks.hpp`, `insert.cpp`. **Open question 7 is answered: yes, an INSERT's
`draw()` recurses into the definition, and `transform()` composes into a matrix
the entity holds.**

**Placement is a full `Mat4`, reduced to R12's fields only at DXF write time.**
That is the convention every other entity already follows — world coordinates in
the kernel, entity coordinates synthesised at serialisation — and it is what lets
ROTATE3D act on a block reference with no special case. A shear, which R12's
point/scale/angle/extrusion cannot express at all, is approximated on the way
out; the alternative was refusing the transform, which would make ROTATE3D fail
on one entity type out of nine. `compose_placement`/`decompose_placement` are
inverses for everything R12 *can* express, and a test pins the round trip —
which is what caught the first version building the matrix transposed, because
`Mat4::from_basis()` builds the world-TO-basis direction.

**A definition is held by pointer, not by name or by value.** Nothing in the
vtable is handed a database — `osnap_points()` takes only an output vector — so
an insert that had to look its block up could not draw or snap at all. Hence
`std::vector<std::unique_ptr<BlockDef>>`: adding a block must not move what an
existing insert points at. R12's redefinition behaviour falls out for free, and
is tested: rewriting a block updates every insertion, because they share the
definition rather than holding copies.

**Picking and region selection through blocks came for free.** `entity_pick_distance`
and the region tests all drive `Entity::draw()` with their own probe, and
`Insert::draw()` recurses — so the flatten-in-the-kernel decision from render.hpp
paid for itself again. Only the *derived* snaps needed work, because NEA/PER/TAN
dispatch on entity type; they descend via `flatten_insert()`, which hands back
transformed copies so no solver needs to know blocks exist. The intersection
kernel uses the same seam.

Decisions worth not re-litigating:

- **EXPLODE goes one level.** A nested reference comes out as a reference. That
  is R12, and it is how an assembly is taken apart deliberately.
- **EXPLODE approximates a non-uniform scale** rather than refusing, since R12
  has no ELLIPSE to explode a squashed circle into and refusing would leave no
  way to take the block apart at all. Same approximation `transform_frame()`
  already documents.
- **BLOCK removes the originals**, as R12 does. One UNDO brings them back.
- **BYBLOCK colour resolves against the reference** as it is exploded, or the
  geometry would silently become BYLAYER.
- **A depth guard of 32 on every recursion.** A drawing cannot contain a cycle,
  because a block does not exist while it is being defined — but a DXF is data
  from elsewhere, and a cycle in one must cost a truncated drawing rather than a
  stack overflow. Tested.

Not built, and deliberately: **ATTDEF and ATTRIB**. Block attributes are a
feature in their own right and TEXT is still a placeholder, so there is nothing
for them to draw. **EXPLODE of a polyline** is also absent — it is the inverse of
PEDIT Join and belongs beside it.

Also fixed in passing, because the same gap in a different place: `$LIMMIN` and
`$LIMMAX` were **hardcoded to the defaults** in the DXF header, so LIMITS could
be set and would not survive a save. They come from the sysvars now, as
`$INSBASE` does since BASE exists to set it.

## The intersection kernel — built, and what it decided

`intersect.hpp` / `intersect.cpp`. Phase 10's foundation, built before any of its
commands because TRIM, EXTEND, FILLET, CHAMFER and BREAK are one question wearing
five hats, and five implementations of it would be five sets of tolerances
disagreeing about whether a corner is closed.

**It supersedes `intersect_entities` in `osnap_derived.hpp`, which now delegates
to it.** That function came first and answered the narrower question osnap asks;
keeping both would have been the duplicated-judgement problem the project already
avoids elsewhere. What stays osnap's own is the *shape* of the answer — points
only, bounded only, at most two, duplicates collapsed — because a cursor cannot
usefully be offered the twelve places two polylines cross.

Three things the kernel adds over what osnap needed:

- **Parameters.** An intersection carries where it falls on each curve, not only
  its coordinates. BREAK cannot split a curve without that, and TRIM's "the piece
  nearer the pick" is a question about parameters. Normalised so [0,1] spans the
  entity as drawn, whatever it is; `curve_point_at()` evaluates the inverse, and a
  test pins the round trip because that is what BREAK will depend on.
- **Extension.** `IntersectMode::Extended` intersects the unbounded carriers — an
  infinite line, a whole circle — and flags which results actually landed on the
  entities. That is what EXTEND is *for*. The invariant that makes it safe, and
  which is tested: extending changes which answers are reported, never where they
  are.
- **Non-coplanar circles.** Two circles in space meet where each crosses the
  other's plane at the other's radius. osnap declined this case as a documented
  limitation; the kernel solves it, so a circle in XY and one in YZ now correctly
  meet at two points. **The osnap test that pinned the old limitation was updated
  rather than worked around** — it was pinning a gap, not a decision.

Decisions worth not re-litigating:

- **Polyline segments are never extended, in either mode.** A polyline's carrier
  is not a well-defined curve; an interior segment's extension means nothing.
- **Collinear overlap is not reported.** An overlap is a range, not a point, and
  every caller here wants points.
- **A tangent is one intersection, not two.** Emitting it twice would make TRIM
  believe there are two pieces where there is one.
- **Everything is decomposed into sub-curves** — a line is one, a circle or arc is
  one, a polyline is one per segment — and every pair goes through the same three
  primitives. That is why polylines needed no fourth case, and why a bulged
  polyline segment meets a circle by exactly the arithmetic a standalone ARC uses.

Not yet done: no spatial index, so `intersect` over a selection set is O(n^2) in
the set's size. That is open question 5's problem and slots in behind this
without changing the signature.

## UCS — where it lives, and the three bugs it exposed

**DXF answered the storage question.** R12 splits UCS exactly the way it splits
layers: named systems are a `UCS` table in the TABLES section, and the *current*
system is HEADER state — `$UCSORG`, `$UCSXDIR`, `$UCSYDIR`, `$UCSNAME`,
`$WORLDUCS`. So named systems went on `Database` beside layers, linetypes and
blocks, and the current one went into system variables, where it inherits
journalling, `getvar`, and header round-tripping from machinery that already
existed.

**The blast radius was smaller than expected, and the format is why.** Entities
do not reference a UCS — they carry an extrusion (group 210) and nothing else.
The ECS is per-entity and permanent; the UCS is global and momentary. They meet
at exactly one instant, when a command creates geometry. So UCS changed no entity
record, no transform, and no DXF entity. What it changed is the *input* path and
the creation commands.

`construction_normal()` was the seam `CLAUDE.md` named, and it was exactly that:
it returned world Z, it now asks the drawing, and not one caller changed.

**Coordinates are converted at the parser, not in the engine.** A typed
coordinate is in the UCS; a clicked one is already world, because the viewport
unprojected it. Those two are indistinguishable by the time they reach
`CommandEngine::supply`, so the conversion belongs at the text boundary where
they are still telling apart. A relative coordinate has its offset transformed as
a VECTOR — applying the origin to a displacement from a base that is already
world would move the point twice.

### Three bugs it exposed, two of them older than it

1. **Undo could not restore a read-only system variable.** The replay path went
   through `Sysvars::set()`, which honours the read-only flag — so UCSORG could
   be set by the UCS command and never put back. It replays through `set_owned`
   now. This was latent from the moment read-only variables existed; UCS is
   simply the first one that anything writes.

2. **`UcsCommand::adopt` took its frame by const reference**, and the first thing
   it does is overwrite the slot that Prev passes it from. Prev restored the
   frame it was leaving rather than the one before that. Taken by value now, with
   the reason written down.

3. **No creation command consulted the construction plane.** CIRCLE, PLINE, TEXT
   and SOLID all built geometry with the default world-Z extrusion. A circle drawn
   in a tilted UCS looked right on screen and serialised with the wrong plane —
   the failure that appears only when the file is opened somewhere else, which is
   precisely the failure `CLAUDE.md` names as the reason ECS is foundational.
   3DFACE is deliberately excluded: it stores world coordinates by definition and
   has no plane to inherit.

### Decisions worth not re-litigating

- **The frame is stored in world**, never relative to the previous one, which is
  what DXF does and means there is no chain to walk and nothing to drift.
- **Axes are orthonormalised on the way out of the database**, not trusted on the
  way in. They arrive through system variables, and a sheared frame would put
  geometry somewhere no transform could undo.
- **UCS ZAxis derives its X axis with `arbitrary_axis()`** — the same rule
  entities use for their extrusion. A UCS built from a normal and an entity with
  that extrusion therefore agree about which way X points.
- **UCS Entity adopts the entity'"'"'s own plane**, which is the one place UCS and
  ECS are literally the same thing.

### Still open

**`UCS Prev`** was a file-scope static when first written. Fixed — see the
session-state note below.

**VPOINT is built.** It was worth the wait: the answer names a direction by its
coordinates in the current UCS, so the same three numbers mean different views
under different systems, and a version written against WCS would have been
written twice.

The arithmetic cancels to `point - origin`, and is deliberately left as the two
steps it is made of, because that is what makes it right for both kinds of
answer. A typed coordinate reaches the command already mapped into world by the
parser; a picked one arrives in world from the viewport. Reading either one's UCS
coordinates as a direction is what R12 does, and it is the same arithmetic for
both — which two of the first tests got wrong by supplying raw numbers and
asserting semantics the pipeline never produces.

`Rotate` is present; the compass and axis tripod are not, so Enter reports the
current direction rather than inventing a default that would silently move the
camera.

**Also fixed, and it was a real bug:** `ViewportWidget::set_plan_view()` ignored
its normal argument. That was correct while every construction plane was world
XY and wrong the moment UCS landed — PLAN in a tilted UCS would have shown a
world plan view. It honours the argument now.

*Still open, and unchanged:* whether VPOINT should push onto the previous-view
stack. It does, because `push_view()` is called by every view mutator and that is
the consistent choice; AutoCAD 2026 agrees. R12 remains unverified, and because
the stack holds whole `Viewport` states rather than zoom rectangles, changing the
answer stays a one-line policy edit.

**The rest of the HEADER is still not read.** The UCS variables are, because a
drawing that reopens in world XY when it was saved tilted changes what typing a
coordinate means. OSMODE, LIMITS and INSBASE are still written but not read.

## TRIM and EXTEND — and the carrier rule they refined

One class for both, because they are the same command with the sign reversed:
select edges, then pick objects until Enter, and each pick is answered by
intersecting against every edge and acting on the parameters. TRIM removes the
stretch the pick falls in; EXTEND grows the end the pick is nearest to.

**The pick point is the argument, not a convenience.** "Trim this line" has no
answer until you say which piece — a line crossing three edges has four. A typed
handle is *refused* rather than guessed at, which is why `InputValue::has_point`
earned its place in the BREAK commit.

**Asymmetric intersection modes.** EXTEND asks about the target's carrier (that
is the point — reaching a boundary the object stops short of) while requiring the
boundary itself to be genuinely crossed. `IntersectMode::Extended` reports both
and flags which, so the command filters on `within1` and ignores `within0`. No
new mode was needed.

**The rule that changed.** `intersect.hpp` originally said polyline segments are
never extended, on the grounds that a polyline has no carrier. That is right for
*interior* segments and wrong for terminal ones: the two end segments of an open
polyline have perfectly good carriers, and growing one is exactly what EXTEND
does to a polyline. So `extendable` is now `!closed && (first || last)`. A hit
found by extending a terminal segment *inwards* maps back to a parent parameter
inside [0, 1], so it reads as an ordinary interior hit and EXTEND ignores it —
the direction needs no separate guard.

Decisions worth not re-litigating:

- **EXTEND reaches the first boundary, not the furthest.** R12's behaviour, and
  the one that makes repeated EXTEND presses walk outwards predictably.
- **Picking an object that meets no edge is not an error.** The command says
  nothing and carries on asking, because picking a few misses mid-command is
  ordinary. Only a pick with no location fails.
- **Trimming past the outermost intersection removes the overshoot**, because an
  open curve's ends count as cuts. A closed curve has no ends, hence its separate
  rule and its two-cut minimum.
- **Still true 3-space.** Two curves that merely cross on screen do not trim each
  other. R12 has no PROJMODE — projecting the trim onto the view arrived with
  R13 — so this is faithful rather than a limitation, and it is tested.

## BREAK, and the bug it found in the kernel

`curve_edit.hpp` is the other half of what phase 10 rests on: `intersect.hpp`
says where curves meet and where on each, this says what is left when a span is
cut out. `extract_curve_span` and `break_curve` are both expressed purely in the
normalised parameters, which is what the uniform parameterisation was *for* — one
implementation cuts a LINE, an ARC, a CIRCLE and a bulged POLYLINE with no
per-command switch. TRIM is the next consumer and needs no new primitive.

`curve_parameter_at` is the inverse of `curve_point_at`, and it projects rather
than requiring incidence, because a pick is never exactly on the curve. It clamps
past the ends, which is what makes BREAK's "second point beyond the endpoint"
shorten the line instead of failing.

**`InputValue` gained `has_point`, and entity answers can now carry where they
were picked.** R12's BREAK takes the pick point as the first break point, because
pointing at an object means pointing somewhere on it — and a bare handle cannot
say that. A typed handle or a LISP ename carries no location, so BREAK asks for
the first point instead of breaking wherever the origin happens to project to.

**The bug worth remembering.** `decompose()` gave a negative-bulge polyline
segment `start_angle = a1` and flipped its sweep positive. Same geometry, but the
segment's parameter then ran *backwards* against the polyline's direction of
travel, so every intersection reported on such a segment had a mirrored
parameter. It had been latent in the intersection kernel since it was written and
was invisible there — nothing consumed a parameter until BREAK did. The sweep is
signed now, `fraction_along_sweep()` is the one place that understands it, and
both the kernel and BREAK have regression tests.

The lesson for TRIM: a parameter that is never evaluated is never checked. The
test that caught this samples the extracted piece and asserts every sample lies
on the original curve, which is the strongest check available without a second
implementation. Worth repeating for TRIM and OFFSET.

## Polyline width and text justification now have somewhere to live

Both were added because a phase 7 command needed them and would otherwise have
been offering an option with nowhere to put the answer.

**`PolyVertex` carries `start_width` and `end_width`** — DXF groups 40 and 41,
per vertex, belonging to the segment leaving that vertex. The reader applies the
POLYLINE header's own 40/41 as defaults to any vertex without its own. Widths
scale under `transform()`, on the same uniform-scale assumption the circular
entities already make.

*Not done:* the renderer ignores width entirely and draws a centreline. R12 with
FILL off draws wide polylines as outlines, so this is a visible divergence rather
than a hidden one, and it is display work rather than storage work.

**`Text` carries `h_align`, `v_align` and `align_point`** — DXF groups 72, 73 and
11. The enumerator values *are* the group values, so the writer needs no mapping
table. Everything that has to agree about where a piece of text is — drawing, the
bounding box, the INSERT snap, the grip — goes through `position_for_drawing()`
and `box_origin()`, so they cannot drift apart.

*Approximations, deliberate:* `Baseline` and `Bottom` are treated alike, because
telling them apart needs a font's descender depth. `Align` and `Fit` solve for
height and width factor respectively against `approximate_width()`, so both are
as approximate as the placeholder metric is — and both become exact for free when
the font question is settled.

## Known issues — reported, not yet diagnosed

**TAN snaps to a circle that is not in the drawing plane.** Reported from the viewport,
2026-07-28. It only fails when the circle is *perfectly orthogonal* to the drawing
plane. Not investigated and not fixed.

Two readings, and which one it is has not been decided: it may be defensible behaviour
for a 3D kernel, since a tangent to an edge-on circle is a real construction in space.
But it is definitively not what AutoCAD does, and matching R12 is the project's
standard. Settle the intent before touching the code — this could as easily end as a
documented divergence as a bug fix.

Start here: `tangent_points()` in `osnap_derived.hpp` projects the reference point into
the entity's plane before solving, which is exactly the step that degenerates when the
plane is edge-on. That is a lead, not a diagnosis.

## Settled: how POLYLINE and PFACE store vertices

**Owned internally, one database entity per polyline or mesh.** VERTEX and SEQEND
records are synthesised when writing DXF and consumed when reading, rather than being
entities in their own right.

The numbers that decided it, for a 20,000-face PFACE mesh stored the R12 way:

- ~20,000 VERTEX entities in the map and the order vector — tens of MB before geometry
- Undo journals a `clone()` of each one, so 20,000 retained allocations per mesh build
- `Database::next()` is a linear scan, so an AutoLISP `entnext` walk becomes O(n²) —
  400 million handle comparisons for one traversal

The last is disqualifying on its own for the workload this project exists for.

Cost: `entget` on a vertex handle is something real R12 LISP does, and it will not work.
Synthesised vertex views — derived handles that `entnext`/`entget` can address, with
`entmod` writing back into the parent — are the way to recover it if that turns out to
matter, and can be added later without changing the storage.

This also happens to be the direction the format went: R14's LWPOLYLINE stores vertices
inline, exactly as this does.

## R13, eventually — where today's design would bite

Not being built, and it does not change the R12 target. Recorded so the choices made
now are made with it in view.

Carries over unchanged: the entity vtable, stable never-reused handles (R13 made handles
mandatory anyway), `transform(Mat4)`, the flattening render path, grips, undo.

Where it would need work:

- **ELLIPSE.** `transform_frame()` in `entities.cpp` assumes uniform scale, with the
  comment "R12 has no ELLIPSE entity, so a non-uniform scale cannot be represented and
  is approximated by the X-axis factor". R13 has one, so that approximation becomes a
  wrong answer rather than the only available one. The comment already marks the spot.
- **SPLINE.** A new entity type; the `draw()`-to-polylines interface handles it with no
  structural change, since everything already flattens.
- **DXF versioning.** The writer is R12-only and unversioned. R13's format differs
  enough that the writer needs a version concept rather than a flag.
- **Solids.** A real kernel (OCCT or similar) behind an `Entity` whose `draw()` emits
  edges. The vtable can host it. Accepted as possibly warranting a fresh start rather
  than a retrofit, which is a reasonable trade for not distorting the R12 design now.

## Splines — R12 has no SPLINE entity

R12's splines are **polyline properties**, not a separate entity: `PEDIT` → `Spline`
gives a quadratic or cubic B-spline approximation of the control polygon, governed by
`SPLINETYPE` and `SPLINESEGS`, and `PEDIT` → `Fit` gives the curve-fit variant. Both are
a flag on the polyline plus a tessellation rule, so they belong with PLINE in phase 7
and cost nothing structurally.

The NURBS `SPLINE` entity is R13, and so is excluded today — see the R13 note above.

## Session state — settled: `CommandMemory` on the engine

The question met three times and now answered once. R12 has several options whose
whole point is "the one from last time", and none of them can live on the command
because the command is gone by the time the next one asks:

| | Remembers | Where it lives |
|---|---|---|
| LASTPOINT | last point entered | engine, stamped onto `Prompt` |
| Previous selection | last selection used | engine |
| ROTATE3D `Last` | last 3D rotation axis | `CommandMemory` |
| UCS `Prev` | previous coordinate system | `CommandMemory` |

`CommandContext` grew its fifth member: a `CommandMemory&` the engine owns,
beside the selection and LASTPOINT. One struct rather than a member each, so the
fourth of these costs a field instead of another context member.

**Why not on `Database`.** It is SESSION state, not drawing state, and the
distinction decides everything: a drawing saved and reopened has no previous UCS
and no last axis, because those describe what you were *doing* rather than what
you drew. So they are not journalled and not written to DXF, and undoing a UCS
change does not restore what `Prev` would have given — which is R12's behaviour
and is pinned by a test.

**Why not a `CommandEngine&` in the context.** It would let a command call
`begin()`, `cancel()` or `supply()` from inside its own `next()`, which is
exactly what the narrow context was protecting against.

The static it replaced was a real defect rather than an inelegance: one per
*process*, so two `Database` instances shared it, undo could not see it, and it
leaked between test cases. A test now pins that two engines do not share history.

## Numerical accuracy — untested, and the tolerances do not scale

Raised by Sadie, and correct: **nothing in the suite tests conditioning.** Every
test runs at comfortable magnitudes — the largest coordinate anywhere in
`tests/` is about 1000 — and no test mentions precision, accumulation or
ill-conditioning. What is pinned is *behaviour*, at magnitudes where double
precision is never in doubt.

**The systemic issue is that every tolerance is ABSOLUTE.** `kIntersectTol`
(1e-9), `kSpanEps` (1e-9), `kJoinTol` (1e-9), `kBulgeEps` (1e-12), `kUcsEps`
(1e-12), `vec3.hpp`'s `kEps` (1e-10), and the harness's own `approx()` and
`near_equal()` — all of them compare a difference against a fixed number of
drawing units. That is fine near the origin and wrong away from it:

- A double has about 15–16 significant digits. At a coordinate of 1e6 the
  representable spacing is around 1e-10, so a 1e-9 absolute tolerance is barely
  above the noise floor. At 1e9 it is *below* it, and a comparison like
  `length(pa - pb) > kIntersectTol` — the skew-line rejection in
  `intersect_line_line` — stops discriminating and starts returning whichever
  answer rounding happens to give.
- This is not hypothetical for an engineering tool. Survey and site coordinates
  routinely run to 1e5–1e7, and the stated workflow imports external analysis
  data whose units nobody here chose.

Specific hazards worth a test each, all in code that already exists:

1. **The bulge singularity.** `bulge = tan(included / 4)`, so an arc approaching
   a full turn has a bulge approaching infinity, and `polyline.cpp`'s
   `apothem = (chord/2) / tan(half)` blows up as a segment approaches straight.
   Both have `kBulgeEps` guards; what is untested is the *transition region*
   just outside them, where the answer is finite, wrong, and unflagged.
2. **`arbitrary_axis()`'s branch discontinuity.** R12's algorithm switches
   derivation when `|Nx| < 1/64 && |Ny| < 1/64`. Straddling that boundary flips
   the derived X axis, so an entity whose normal wanders across it changes
   orientation discontinuously. Deliberate and R12-faithful, but nothing pins
   the behaviour either side.
3. **Catastrophic cancellation in circle/circle.** `x = (d² + r0² - r1²) / 2d`
   followed by `h = sqrt(r0² - x²)` loses most of its significant digits when
   two circles are nearly tangent, or when the radii are large and their
   difference small.
4. **Accumulated transform error.** `transform(Mat4)` composes, so ARRAY with
   many items, repeated ROTATE, and nested block placements all accumulate.
   Nothing measures the drift.
5. **`decompose_placement` at extreme scale.** Recovering scale from column
   lengths and rotation from `atan2` degrades as the factors spread apart.

What a test suite for this should look like, and it is a design job as much as a
test-writing one:

- **Round-trip identities at several magnitudes.** The same construction at
  1e-3, 1, 1e3, 1e6 and 1e9, asserting a *relative* error bound rather than an
  absolute one. That alone would document where the current code stops working.
- **Adversarial pairs.** Nearly-tangent circles, nearly-parallel lines, nearly
  straight bulges, normals on the 1/64 boundary.
- **Chains.** N transforms applied and inverted, asserting drift stays below a
  bound that scales with N — which also catches an inverse that is subtly not.
- **A decision the tests will force:** whether the tolerances become relative
  (scaled by the magnitude of the operands or by a drawing-extents-derived
  unit), or whether the kernel documents a supported coordinate range and says
  so. R12 itself had limits here; matching it is a defensible answer, and so is
  beating it, but drifting into one by accident is not.

Worth doing before the mesh work in phase 13, since that is the phase that
imports coordinates this project did not choose.

## Open architectural questions

Recorded so they get decided rather than drifted into.

1. **Grip/stretch vtable shape.** Decided in 4b: defining points plus a move-a-subset
   mutator, with behaviour attached to each grip rather than bare coordinates.

2. **TEXT rendering.** R12 used SHX vector fonts. `draw()` emits world-space polylines
   and the core is headless, so Qt fonts are not available to it. Parse SHX, bundle a
   vector font, or emit TEXT to DXF and only approximate it on screen? The last option
   splits the headless core from what the viewport shows, which cuts against
   `CLAUDE.md`'s "rendering the same database two ways is a correctness check worth
   having". Gates phase 7 and anything dimension-adjacent.

3. **Where dash generation lives — settled: a wrapper `Renderer` in the core.**

   `Entity::draw()` keeps emitting solid polylines. A `DashRenderer` sits between
   `draw_database()` and the real backend, cutting runs into dashes and passing them
   through, so QPainter and a future GL backend share one implementation.

   The reason it is a wrapper rather than something `draw()` does: hit-testing and
   region selection both run through `Entity::draw()` —`entity_pick_distance` and
   `entity_crosses_region` each drive it with their own probe. If `draw()` emitted
   dashes, the probes would see the gaps, and a dashed line could not be picked
   between its dashes or caught by a crossing window through one. AutoCAD picks a
   dashed line anywhere along it. A wrapper gets this right by construction, because
   the probes simply do not wrap; a flag on `DrawContext` would get it right only as
   long as every probe remembered to set it.

4. **`CommandContext` widening.** It stays narrow through 4a and 4b. The selection set
   is the first legitimate new member, in phase 5 — which is what its own header
   anticipates: "Those arrive as explicit members when they exist, so the coupling
   stays visible."

5. **Spatial index.** None exists. It slots in behind `pick_entity` and
   `osnap_candidates` without changing either signature, so it can wait until the
   linear scan stops holding.

6. **DXF read timing.** Phase 9 above, but the argument for pulling it earlier is real:
   it would allow loading genuine test drawings and make the correctness gate
   bidirectional. The counter-argument is that a reader written before the entity set
   settles gets retrofitted repeatedly.

7. **Blocks and the vtable.** *Settled in phase 11.* `draw()` recurses through
   the definition under an accumulated transform; `transform()` composes into a
   full matrix the entity carries, which is reduced to R12's fields only when
   the drawing is written. See the blocks section above.

8. **GL migration trigger.** `CLAUDE.md` says "when 3D orbit performance demands it".
   Worth naming an actual threshold rather than deciding by feel.

9. **APPINT.** `osnap_derived.hpp` attributes apparent intersection to R12; if it is in
   fact R13, it is out of scope by `CLAUDE.md`'s post-R12 exclusion and the comment
   should say so. No code impact either way.

---

## Settled — do not revisit without reason

- **Object snap INTERSECTION is true 3-space, never view-projected.** Two skew lines
  that merely cross in the pixel view yield no snap. This is deliberate and tested;
  see `osnap_derived.hpp`.

- **Plan view is the common case.** Looking down `0,0,1` covers the overwhelming
  majority of real work. The 3D paths exist and are tested, but the common case stays
  the simple one and should not be complicated to serve the rare one.

- **STRETCH is crossing-selection only.** See phase 5 — this is R12 behaviour, not a
  limitation to design away.
