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
- **CHANGE/CHPROP.**
- **History recording for scripting**, which pairs with `--script` above.
- **Baseline and continue dimensions**, cheap now that the families exist.
- **Writing DXF versions later than R12.** Reading them is built.

---

## AutoLISP — what is implemented, and what classic scripts still need

Measured, not estimated: **97 built-in functions and 13 special forms.**

**The language is essentially complete. The library is about a third.** And the
missing third contains the functions real scripts use most, which is why the
headline number flatters it.

### Present

- **Control and binding** — `defun`, `setq`, `set`, `if`, `cond`, `while`,
  `repeat`, `progn`, `lambda`, `foreach`, `and`, `or`, `not`, `quote`. Dynamic
  scoping, as R12 has it.
- **Lists** — `car`/`cdr` with ten `cxr` combinations, `cons`, `list`, `append`,
  `length`, `nth`, `last`, `reverse`, `member`, `assoc`, `subst`, `apply`,
  `mapcar`.
- **Maths** — the full classic set, including `gcd`, `expt`, `rem`, `fix`,
  `float`, the trig functions and `pi`.
- **Strings** — `strcat`, `strlen`, `substr`, `strcase`, `chr`, `ascii`, `itoa`,
  `atoi`, `atof`, `rtos`.
- **Predicates and output** — `atom`, `listp`, `null`, `numberp`, `boundp`,
  `type`, `eq`, `equal`, `zerop`, `minusp`, the comparisons, `princ`, `prin1`,
  `print`, `terpri`.
- **CAD** — `entmake`, `entget`, `entmod`, `entdel`, `entnext`, `entlast`; the
  six selection-set functions; `getvar` and `setvar`; and `command`, which is the
  one that matters and works properly across suspension.

### Absent

| Group | Missing |
|---|---|
| Geometry helpers | ~~`polar`, `distance`, `angle`, `inters`, `osnap`~~ **built** — `trans`, `textbox` remain |
| User input | `getpoint`, `getdist`, `getangle`, `getcorner`, `getint`, `getreal`, `getstring`, `getkword`, `initget` |
| File I/O | ~~`open`, `close`, `read-line`, `write-line`, `read-char`, `write-char`~~ **built**, plus `findfile` |
| Tables | ~~`tblnext`, `tblsearch`~~ **built** for LAYER, LTYPE, BLOCK and UCS |
| Selection | `entsel`, `nentsel`, `handent` |
| Other | `*error*`, `load`, `eval`, `logand`, `logior`, `lsh`, `angtos`, `prompt`, `alert`, `ver` — ~~`read`~~, ~~`findfile`~~ and ~~`wcmatch`~~ built |

### Can a classic script run?

Some. Anything that computes with lists and numbers and then builds geometry
through `entmake` or `command` runs today, unmodified.

Most cannot, and the reason is narrower than the table suggests. **`polar`,
`distance` and `angle` appear in very nearly every drafting script ever
written** — they are how a script says "a point 40 units at 30 degrees from
there". Without them a script fails on its third line no matter what else is
present. The second wall is `getpoint` and its family, which is any script that
asks the user anything. The third is `open` and `read-line`, which matters here
more than elsewhere because reading external analysis data is the workflow this
project exists for, and `CLAUDE.md` names solid file I/O in its own scope.

### Order worth doing them in

1. ~~**The geometry helpers.**~~ **Built.** `polar`, `distance`, `angle`,
   `inters` and `osnap`. They are world-coordinate rather than UCS, because this
   layer has no UCS anywhere in it and doing them in UCS would have meant
   `(command "LINE" (polar p a d))` silently mixing two frames — see
   `geom_subrs.hpp`. `osnap` searches the whole drawing rather than an aperture,
   since a script has neither a screen nor a zoom. `trans` and `textbox` remain.
2. ~~**File I/O.**~~ **Built.** `open`, `close`, `read-line`, `write-line`,
   `read-char`, `write-char` and `findfile`. Descriptors are `Type::File`, whose
   slot in the value union had been waiting since the reader was written. Paths
   get the same `~` expansion the file commands give, so a script and the SAVE
   prompt agree about what `~/drawings/x.dxf` means.

   **`read` closed the text-handling gap.** `(read (strcat "(" line ")"))` turns
   `"30.0 40.0"` into the list `(30.0 40.0)` — the reader does the splitting,
   which is how AutoLISP has always parsed a whitespace-separated record and why
   this was the missing piece rather than a split-string function.
3. **The `get*` family with `initget`.** See "Interactive AutoLISP input is not
   implemented" below, which explains why bare `(ssget)` refuses rather than
   returning an empty set.
4. **`tblsearch`/`tblnext`, `entsel`, `handent`, `wcmatch`, `*error*`** — the
   long tail that real scripts assume is there.

Out of scope and staying there: `vl-*`, `vla-*` and `vlax-*` (Visual LISP), DCL
dialogs, `menucmd`, `getfiled`.

---

# Moved from the roadmap

These were recorded in `SF_todo.md` while it was the only planning document.
They are capabilities that do not exist yet, so they belong here; the analysis
came with them unchanged, because the reason a thing is wanted is usually the
most perishable part of wanting it.

## Viewport feedback — what the display does not tell the designer

Sadie's list, in her words "visual sugar that gives the designer landmarks". Less
sugary than it sounds: three of the four are the viewport reporting engine state
that is currently invisible, and the first one found a real bug.

**Done:** the selection window now draws as a box. `Prompt::rubber_band`
(`RubberBand::None/Line/Box`) carries the shape, because `PromptKind` cannot —
a window's second corner and a LINE's next point are both a Point prompt with a
base, and they want different glyphs. It is advice for whoever is drawing, like
`Prompt::base` beside it, and the engine never reads it.

The interaction is modal click-click, not press-drag-release: a click sets the
first corner, the box follows the cursor, a second click closes it. That needs
no drag state at all, because `SelectionPrompter` already asks for two ordinary
point prompts and `mouseMoveEvent` already repaints while one stands.

**Still to build, cheapest first:**

1. **The UCS/WCS axis icon.** Pure screen-space painting beside
   `draw_rubber_band` and `draw_osnap_marker`. `Sysvar::UcsIcon` already exists
   with R12's 0/1/2 (off / corner / at origin) and round-trips through DXF;
   nothing reads it. AutoCAD's colours: X red, Y green, Z blue. No core changes
   and no decisions outstanding.

2. **Highlighting the selection.** The enabling piece for this and (3): nothing
   can currently draw a *subset* of entities with *overridden* styling.
   `Renderer` is two virtuals — `begin_entity(EntityProps)` and `polyline()` —
   with no colour or style channel, and `EntityProps` has no highlight bit.
   `DashRenderer` is the precedent for the wrapper shape; `Entity::draw()` is
   directly callable for a subset, as `RegionProbe` already does. The viewport
   also has no access to `engine_->selection()` at all — `src/gui/` contains
   zero references to selection. R12 highlighted by redrawing dashed, which is
   machinery that already exists.

3. **The ghost during MOVE/COPY/STRETCH.** Falls out of (2) nearly free: clone
   the selection, `transform(Mat4)` by the pending delta, draw it through the
   same path.

   Sadie asked whether XOR is the simple way, as R12 did it. It was, and it no
   longer is. XOR's payoff was never contrast but *erasure* — draw twice and the
   screen is restored without redrawing the scene — which needs persistent
   access to the front buffer between frames. Qt is double-buffered and every
   `paintEvent` starts from a fresh backing store, so there is nothing to
   un-XOR, and at R12-era wireframe complexity the full repaint is free anyway.
   `RasterOp_SourceXorDestination` also does not survive antialiasing (which
   `paintEvent` enables) or the OpenGL paint engine that phase 14 plans to move
   to. So: repaint, in a highlight colour.

4. **Live selection candidates** while the box is being sized — highlight what
   *would* be taken as the cursor moves. Wants (2) first.

**Open:** modern AutoCAD picks window-vs-crossing implicitly from drag direction
(right = window, left = crossing) with different box styling. Whether R12 did is
unverified, and it is keyword-driven here for now, which is R12's documented
behaviour. Decide before adding implicit dragging.

Items 2, 3 and 4 above are all the same missing construct. See the next section.

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

## UI polish — reported from use, not yet built

*(The focus loss is fixed — see the commit "Typing always reaches the command line".
It needs confirming in a real session, since it could not be reproduced headlessly.)*

**Show the selection box while it is being dragged — HALF BUILT.** The rectangle
itself is drawn: `viewport_widget.cpp:368`, screen-aligned and normalised, anchored at
the opposite corner. What is absent is telling window from crossing by eye — AutoCAD
uses a solid outline for one and a dashed one for the other.

That absence is currently *reasoned*, not overlooked. The code says: *"Which of window
and crossing this is comes from the typed W or C keyword, so the box does not have to
say — the prompt already does."* True today. **It stops being true the moment implied
windowing lands**, because then the drag direction decides which you get and nothing
announces it until you release — which is the whole reason AutoCAD draws them
differently. So solid-vs-dashed is a prerequisite of that item rather than a polish
item beside it, and the comment explaining its absence will need deleting, not
updating.

**Ghost the selection during placement.** MOVE and COPY should show the selected
geometry following the cursor between the base point and the second point.

Both are wanted, with a caveat worth designing around: this is used over SSH with X11
forwarding, where the current no-feedback behaviour is *faster*. Whatever is drawn
should be cheap — outline only, no fill — and probably switchable, since the remote
case is a real working mode rather than an edge case.

**The pick box wants too much precision — and it is felt in exactly one place.**
`PICKBOX` defaults to 3 — a half-height, so a 7-pixel box — which is AutoCAD's default
and has been since R12, when a screen was around 50 DPI. At the ~110 logical DPI of a
modern display the box is **physically less than half the size it was**, and nothing
scales it: `devicePixelRatio` appears only in toolbar icon generation, nowhere in the
picking path. Everything in the viewport is consistently in logical pixels, so this is
not a HiDPI bug — the arithmetic is right and the constant is stale.

This is the `CURSORSIZE` case again, and `CLAUDE.md` already licenses it: `PICKBOX = 3`
is not a considered design choice about interaction, it is a number calibrated against
a display that no longer exists.

**`PICKBOX` and `APERTURE` are used in disjoint situations, so there is no ratio to
tune.** `viewport_widget.cpp:334` picks between them —
`wants_entity() ? pickbox_px() : aperture_px()` — and `wants_entity()` is true only for
`PromptKind::Entity`, which is only ever the *Select objects* prompt:

| doing | prompt kind | box drawn and used | size |
|---|---|---|---|
| Select objects, including typing `W`/`C` to start one | `Entity` | `PICKBOX` | 7 px — **tiny** |
| Window / crossing **corners** | `Point` | `APERTURE` | 21 px |
| Drawing lines, any point entry | `Point` | `APERTURE` | 21 px — right |

So the pick box is only ever live during selection, and that is the one place it is
wrong; the hit test at `viewport_widget.cpp:774` uses it too. Everything else runs on
`APERTURE` at 21 px, which is **correctly sized and must not move** — reported from use,
and consistent with the osnap complaint below being about which snap WINS rather than
how far the aperture reaches.

*What it needs:* raise `PICKBOX` toward `APERTURE`. An earlier version of this note said
to raise one while lowering the other, "converging them deliberately, breaking AutoCAD's
10:3 ratio". That was wrong: the two never both apply, so the ratio was never a thing
either variable experienced.

**Object snap reaches too far, and that one is a DEFECT rather than a preference.**
`src/core/osnap_search.cpp:68`:

```cpp
if (a_discrete != b_discrete) return a_discrete;
```

A discrete snap — endpoint, midpoint, centre, quadrant — beats a continuous one —
nearest, perpendicular, tangent — **unconditionally, at any distance inside the
aperture**. With `APERTURE = 10` that is a 21-pixel box, so an ENDPOINT 20 pixels away
wins over a NEAREST the cursor is sitting exactly on. The radius is not wrong; the tier
is unbounded.

The comment above the line explains why it is written that way and the reasoning is
sound — without a tier, NEAREST buries ENDPOINT, because NEAREST is always ~0 px when
you are on the line. The cure simply overshoots. It also warns, correctly, that the
obvious repair (`if (fabs(da - db) > eps) return da < db`) is **not transitive** and
makes `std::sort` undefined rather than merely differently ordered.

*What it needs:* an effective distance — `distance + (discrete ? 0 : penalty)` — sorted
on as an ordinary number, so transitivity is structural rather than argued. It states
something meaningful too: *a discrete snap is worth up to N pixels of extra travel.*
Today's behaviour is `penalty = infinity`. Something around 5 px keeps ENDPOINT winning
when it is genuinely close and lets NEAREST win when it is not. Probably a sysvar.

Note these two pull in opposite directions and are felt together: raising `PICKBOX`
while **lowering** `APERTURE` converges them deliberately, breaking AutoCAD's 10:3
ratio on purpose.

**Selection has no implied windowing, and that is an unbuilt R12 feature rather than a
modernisation.** Today a window or crossing needs `W` or `C` typed first, then two point
picks (`commands.cpp:4731`). Click-and-drag on empty space — left-to-right for window,
right-to-left for crossing — is AutoCAD behaviour governed by `PICKAUTO`, which R12 had
and this sysvar table does not: 33 variables, no `PICKAUTO` and no `PICKDRAG`. So the
justification is "a gap", not "let us be modern", which is a much easier argument.

*Priority note, and it is the kind that otherwise evaporates:* **this is for newcomers,
not for Sadie** — the R12/R13 muscle memory came back within a session of real use, so
typing `W` costs the author nothing. It stays recorded because the next person has no
such memory, not because it is felt daily.

There are a few more of this shape. Worth collecting rather than taking one at a time,
since they will share the drag-state machinery in the viewport and that is the whole
cost.

**Ortho is built and cannot be reached while you are drawing.** Constraining a point
to N/S/E/W of the previous one is `apply_ortho` in `viewport_widget.cpp:416`, and it is
done properly: relative to the **current UCS** rather than world, skipped when there is
no base point (R12 leaves a first point unconstrained), skipped for `RubberBand::Box`
so a selection window is not forced square, and an object snap beats it — *"you asked
for that exact point."* `cursor_point()` applies it on every mouse move.

What is missing is the toggle, and it is missing twice over:

- **There is no F8.** The Qt viewport handles no function keys at all — `Key_Escape` is
  the only key it interprets. F8 is *the* ortho key and has been since long before R12,
  so the reflex has nowhere to land.
- **`'ORTHO` mid-command does not work either.** `command_is_transparent`
  (`commands.cpp:6362`) lists ZOOM, PAN, PLAN, REDRAW, VPOINT, ID and DIST. ORTHO is
  not among them, so toggling requires finishing or cancelling the command first —
  which is exactly the moment you wanted it.

So the feature exists and the workflow it was built for does not. That is worth
separating from a missing feature, because the fix is a keybinding rather than geometry.

*What it needs:* F8 in the viewport's `keyPressEvent`, flipping `ORTHOMODE` directly.
That is also how AutoCAD does it — F8 is not a transparent command invocation, it is a
direct toggle — and it **sidesteps a real awkwardness**: `command_is_transparent` says
its test is "whether it changes drawing state, not whether it is useful mid-command",
and ORTHOMODE is journalled and saved in the drawing, so by that rule ORTHO has a fair
claim to being exactly what must not be transparent. A key that flips a sysvar never
raises the question.

*What it does not need:* the base points are already there. 74 of the 93 `PromptKind::Point`
prompts in the core set one, MIRROR's second point included — so "any point-to-point work
in the viewport" is already covered the moment the toggle exists.

**Polar tracking is the superset, and a later decision.** Ortho is 4 directions; polar
snaps to an angle increment (45°, 30°, 15°) and reports the angle as you hover. R12 had
ortho only — polar arrived in AutoCAD 2000 — so it is a deliberate divergence rather than
a gap, and it wants the same F8-adjacent plumbing underneath. Worth building ortho's
toggle first and seeing whether polar is still wanted afterwards.

**While the keys are being added,** R12's other F-keys are worth deciding as a set rather
than one at a time: F7 grid and F9 snap are both excluded by scope today (grid snap is
listed as "later, rarely used"), F6 is a coordinate readout that wants a status bar, and
F3 is osnap toggle — which, unlike the rest, has a live sysvar (`OSMODE`) and the same
shape as this entry.

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
