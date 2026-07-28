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
- [ ] ROTATE3D's **Last** axis option. It has to outlive the command that set it, and
      `CommandContext` has nowhere for it to live — the same shape of problem LASTPOINT
      solved by living on the engine. Small, but it wants a decision about whether the
      context grows a fifth member or the engine grows another accessor.
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
| 10 | Geometry editing | TRIM, EXTEND, OFFSET, FILLET, CHAMFER, BREAK, CHANGE/CHPROP. **The intersection kernel it needs is built** — `intersect.hpp`. The commands themselves are not. |
| 11 | Blocks | *Done.* BLOCK, INSERT, MINSERT, EXPLODE, WBLOCK and BASE, plus the BLOCKS section both ways. Open question 7 is answered below. **Not built:** ATTDEF/ATTRIB, and EXPLODE of a polyline. |
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

## View commands — what is known before they are written

Recorded from Sadie's daily AutoCAD use, so the design does not have to be guessed at
when phase 6 starts.

**VPOINT is interpreted in the current coordinate system** — UCS when one is active,
WCS otherwise. So VPOINT is not a world-space direction that happens to be typed in;
it is a direction in the current CS, and the same numbers mean different views under
different UCSs. This ties phase 6 to phase 12 more tightly than the table suggests:
VPOINT written against WCS only would need revisiting rather than extending. Same
`construction_normal()` seam the transform commands already isolated.

**ZOOM's options are All, Center, Dynamic, Extents, Left, Previous, Window, and a
scale factor.** Previous is the interesting one.

*Open:* whether ZOOM Previous also restores a view direction changed by VPOINT.
AutoCAD 2026 does; R12 is unverified.

**The design consequence, which removes the risk either way:** the previous-view stack
should hold a complete `Viewport` state — target, view height, azimuth, elevation — and
not a zoom rectangle. Then "does VPOINT push onto the stack" is a one-line policy
decision that can be changed after testing against real drawings, rather than a change
to what the stack is made of. Storing only a zoom extent would bake the answer in.

Worth deciding the stack depth at the same time: R12 remembered ten previous views.

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

## Pick cycling is missing

Reported from use: three identical lines at the same place, and a single click only
ever selects one of them.

That much is correct — AutoCAD picks the topmost and so does `pick_entity`, which walks
the drawing order backwards and returns the first hit. What is missing is the way to
reach the others: **Ctrl+click cycling** through coincident candidates. The comment in
`pick.hpp` anticipated it ("nearest-wins would make pick cycling incoherent once it
exists") and it was never built.

The confusing part is not the single selection but what a second click does: it
re-picks the same entity, `SelectionSet::add` dedupes it, and the count stays at 1 — so
it reads as the click having been ignored.

*Workaround today:* a crossing window catches all three, since `select_by_region` walks
every entity rather than stopping at the first hit.

*What it needs:* `pick_entity` returns one result; cycling wants all candidates within
the pick box, ordered topmost-first, plus somewhere to remember which one was offered
last so the next Ctrl+click moves on. The search itself is a small change —
`pick_entity` already visits them all and simply returns early.

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

## PEDIT's curve options need a curve representation first

`Fit curve`, `Spline curve` and `Decurve` are the three PEDIT options not built,
and they are not command plumbing. R12 spells them as a flag on the polyline plus
`SPLINETYPE` and `SPLINESEGS`, and `Decurve` has to put the original vertices
back — so the polyline has to keep its control vertices alongside the fitted ones
rather than replacing them.

That is a storage decision of the same weight as the width one below, and it is
the same question the splines note further down already raises. Worth settling
both at once: the answer to "how does a polyline remember it is a spline" is the
answer to both.

`Edit vertex` is absent for a different reason — it is a nested prompt loop with
ten options of its own, and its useful half is dragging vertices, which wants the
interactive grip work that is still outstanding.

Also absent, and smaller: PEDIT on a LINE or ARC offers to convert it into a
one-segment polyline first. That is a creation path rather than an edit, and it
wants deciding alongside the curve options.

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
