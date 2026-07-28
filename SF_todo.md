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

Left undone deliberately: there is no OSNAP command and no status line, so `OSMODE`
is set with `(setvar "OSMODE" 47)` and the active mode is named beside the marker
glyph instead. Both become phase 6 and 8 work.

## Phase 4b — the grip/stretch vtable

- [ ] Decide the interface: expose defining points, and move a named subset of them
- [ ] Grips carry *behaviour*, not just position — dragging a circle's quadrant grip
      changes its radius, dragging its centre moves it. Same coordinates as
      `osnap_points()` in several cases, different meaning, so it cannot reuse them
- [ ] Implement across Line, Circle, Arc; headless tests
- [ ] No interactive dragging yet — that needs phase 5

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

## Long-range phases

| # | Phase | Why it sits here |
|---|---|---|
| 6 | View and inquiry commands | ZOOM, PAN, PLAN, VIEW, VPOINT, REGEN, REDRAW; DIST, ID, AREA, LIST. Cheap — `Viewport` already exists, and the GUI's Home / Ctrl-Home shortcuts become these commands' aliases, as `viewport_widget.cpp` already promises. High value per line, and a good breather after phase 5. |
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
