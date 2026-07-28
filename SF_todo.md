# NotoCAD — roadmap

The long-range plan. `CLAUDE.md` records decisions already settled; this records what
is *next* and why it sits where it does, so the order is chosen rather than drifted
into.

Near-term work is tickable. Beyond that the phases carry rationale instead of task
lists, because the detail belongs in a per-phase plan written when the phase starts.

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

- [ ] `SelectionSet` type; widen `CommandContext` (its header already anticipates this)
- [ ] Window, Crossing, Last, Previous, All, Fence, plus Remove/Add
- [ ] Box and crossing pick in the viewport widget
- [ ] `ssget`, `entsel`, `ssadd`, `ssdel`, `sslength`, `ssname` in AutoLISP
- [ ] `getpoint`, `getdist`, `getangle`, `getstring`, `getreal`, `getint`
- [ ] MOVE, COPY, ROTATE, SCALE, MIRROR, ARRAY, ROTATE3D — all route through
      `transform(Mat4)`, which already exists
- [ ] STRETCH, crossing-window only, on the 4b mechanism
- [ ] Interactive grip dragging

**STRETCH needs more than a handle list.** It only works when the selection is made by
crossing window or crossing polygon. With a plain Window, or with objects picked
individually, every defining point of every selected entity is inside the selection —
so "move the points that are inside" moves all of them, and STRETCH silently
degenerates into MOVE. No error, no warning. That is exactly the shape of a feature
that seems broken to a user.

The design consequence: `SelectionSet` must carry the crossing region's geometry, not
only the handles. The question "which vertices move" is answered by the crossing
rectangle, and that is not derivable from a set of entity handles.

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

**Still open:** layers and linetypes are not journalled yet — no command changes them,
so there is nothing to lose today, but LAYER in phase 8 must not land without it.
Memory is unmeasured; see below.

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
| 6 | View commands | ZOOM, PAN, PLAN, VIEW, VPOINT, REGEN, REDRAW. **Blocked**: they need a `Viewport`, and `CommandContext` deliberately holds none — the same decision the selection-window axes and the interactive LISP input are waiting on. The inquiry half (DIST, ID, AREA, LIST) is done. |
| 7 | Entity breadth | PLINE, POINT, SOLID, 3DFACE, TEXT, then PEDIT. Forces the TEXT rendering decision (open question 2). Afterwards, selection, hit-testing, osnap, transforms and DXF are all exercised against a realistic entity set instead of Line/Circle/Arc. |
| 8 | Tables and settings | LAYER, LTYPE and dash rendering, COLOR, LTSCALE, UNITS, LIMITS. Linetypes touch three layers at once: the DXF table, dash generation in the render path, and LTSCALE — not just a table entry. |
| 9 | DXF read and OPEN | Closes the write-only gap. `dxf.hpp` has `DxfWriter` and nothing else, so the drawing cannot be reopened — not even our own output — and the "open it in other CAD software" correctness gate runs one way only. Placed after 7–8 so the reader is written once against a fuller entity and table set rather than retrofitted, but see open question 6. |
| 10 | Geometry editing | TRIM, EXTEND, OFFSET, FILLET, CHAMFER, BREAK, CHANGE/CHPROP. Needs intersection machinery beyond what osnap uses. |
| 11 | Blocks | BLOCK, INSERT, WBLOCK, EXPLODE, MINSERT, BASE. Raises open question 7. |
| 12 | UCS | `CLAUDE.md` notes ECS is foundational and already in the kernel, but that UCS "has nowhere to live". This gives it one. |
| 13 | Meshes and surfaces | PFACE, 3DMESH, RULESURF, TABSURF, REVSURF, EDGESURF; AutoLISP file I/O (`open`, `read-line` — `file_subrs.cpp` currently holds only `dxfout`); suppressed-regen batch mode for LISP loops. **The project's stated purpose.** |
| 14 | Performance | A spatial index — none exists, and 4a's mouse-move path is the first thing to feel the linear scan. `QOpenGLWidget` migration behind the same `draw()`. |
| 15 | macOS port | Stated goal in `CLAUDE.md`. |

---

## CIRCLE is missing its construction options

`CIRCLE` takes centre-then-radius only. R12 also has **2P**, **3P** and **TTR**
(tangent-tangent-radius), and they are the reason CIRCLE is worth more than a radius
box.

Worth separating two things that look alike and are not:

- *Snapping the radius pick to a TAN point* — now works, since the radius prompt takes
  a picked point and osnap runs there. It gives a circle passing **through** a tangent
  point of another entity.
- *A circle tangent **to** another entity* — a different construction. With the centre
  fixed, the radius is the perpendicular distance to that entity, and there are
  generally two answers (inside and outside). TTR with two tangent entities has up to
  eight. Solving those is real geometry work, not prompt plumbing.

The second is what "always two solutions" means and what TTR is for. Neither exists.
Phase 5 or 6, alongside the other construction commands.

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

## UI polish — reported from use, not yet built

*(The focus loss is fixed — see the commit "Typing always reaches the command line".
It needs confirming in a real session, since it could not be reproduced headlessly.)*

**Show the selection box while it is being dragged.** The window or crossing rectangle
should be drawn as it is dragged, and distinguishably: AutoCAD uses a solid outline for
window and a dashed one for crossing, which is how you know which you are getting
before you release.

**Ghost the selection during placement.** MOVE and COPY should show the selected
geometry following the cursor between the base point and the second point.

Both are wanted, with a caveat worth designing around: this is used over SSH with X11
forwarding, where the current no-feedback behaviour is *faster*. Whatever is drawn
should be cheap — outline only, no fill — and probably switchable, since the remote
case is a real working mode rather than an edge case.

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

- **An arc caught by its centre alone does nothing.** Only Stretch-kind grips are
  eligible, which for an arc means its endpoints; the centre is a Move grip. Whether
  R12 moves an arc whose centre falls inside the crossing window is unverified.
- **A circle uses its centre**, because it has no Stretch grips at all and falls back
  to its Move grip. That matches R12's behaviour of moving a circle when its centre is
  inside, and it also means the crossing box has to cross the rim *and* contain the
  centre — crossing the rim alone selects the circle but moves nothing.

## One decision now blocks three things

How does a command reach the view?

`CommandContext` is `{Database&, SelectionSet&, const SelectionSet&}` and its header
says it holds no view deliberately. Three separate pieces of work are now waiting on
that:

1. **ZOOM, PAN, PLAN, VPOINT, VIEW, REGEN, REDRAW** — all of phase 6's view half. They
   manipulate a `Viewport`, which lives in the Qt widget.
2. **Selection window axes under orbit** — `SelectionPrompter::set_view_axes()` exists
   and nothing calls it, because the widget cannot reach a prompter private to a
   command.
3. **Interactive AutoLISP input** — `entsel` and the `get*` family, below, which need
   the same route from the interpreter out to whatever can ask a question.

Options, none obviously right:

- **A third `CommandContext` member.** Simplest, and the header's own precedent — the
  selection arrived exactly this way. But it puts a view in the core's command
  interface, and `ncad` has no view at all, so it would have to be optional or a null
  object.
- **A host interface** — an abstract `ViewControl` the GUI implements and `ncad` stubs.
  Keeps `Viewport` out of `CommandContext` while giving commands what they need. This
  also happens to be the shape `UserInput` wants for (3), so one design could serve
  both.
- **Commands return view requests** as part of `Step`, and the host applies them.
  Keeps the core pure but only expresses what can be enumerated in advance.

Worth settling before phase 6 rather than during it.

## Interactive AutoLISP input is not implemented

`ssget` has its non-interactive modes — `"X"`, `"P"`, `"L"`, `"W"`, `"C"` — plus the
whole accessor family, which is the half a procedural workflow uses: generating
geometry from analysis data never involves a cursor. Bare `(ssget)` refuses with a
message rather than returning an empty set, because "found nothing" and "cannot ask"
must not look alike.

Still missing, and blocked on one decision: `entsel`, `getpoint`, `getdist`,
`getangle`, `getstring`, `getreal`, `getint`, `getcorner`, `getkword`.

**The decision.** These need the interpreter to ask a question part-way through
evaluating an expression. In `ncad` that is a blocking read from stdin and easy. In the
Qt shell it cannot block the event loop, so it needs a nested `QEventLoop` — which is
the standard Qt answer and does work, but re-entrancy is the hazard: a second command
started from inside the nested loop, or the window closing while an expression is
suspended, both need thinking about. The alternative is suspending the interpreter
itself, which is what `CommandEngine` does for commands and would mean continuations or
a separate thread for LISP.

The shape that fits: an abstract `UserInput` with `bool ask(const Prompt&, InputValue&)`
in the core, implemented by `ncad` over stdin and by the GUI over a nested loop. The
core stays headless either way.

Also not implemented: `ssget` fence mode `"F"`, and filter lists on `"X"`.

## Known issues — reported, not yet diagnosed

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

3. **Where dash generation lives.** In the core, so QPainter and a future GL backend
   share one path, or in each backend. The former matches the flattening rationale
   already stated in `render.hpp`.

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

7. **Blocks and the vtable.** Does an INSERT's `draw()` recurse into the block
   definition, and what does `transform()` mean for a block reference?

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
