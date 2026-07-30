# Handoff — session ending 2026-07-29

Session-scoped and disposable. `CLAUDE.md` holds settled decisions and `SF_todo.md`
holds the roadmap; both were updated as work landed and are the durable record.
**This file is only the bridge** — what a fresh instance needs that those two do
not already say. Delete or rewrite it rather than letting it rot.

## Where things stand

- On **`main`**, HEAD `50e6aee`. Twenty-two commits since `281ee1e`.
- **872 tests, 0 failed.** Clean under ASan+UBSan. `ctest` 10/10. `ncad_gui` links.
- **An uncommitted patch sits in `~/src/iperl`** (a different repository, Sadie's).
  It adds `--pipe` to `iperl.pl` and ncad's calculator depends on it at runtime.
- Working tree clean apart from `ncad_gui_ss.png`, which is untracked and has
  been deliberately excluded from every commit
  (`git add -A ':!ncad_gui_ss.png' ':!HANDOFF.md'`).

## The one decision that changes everything downstream

**R12 is the floor, not the ceiling.** Sadie's, recorded in `CLAUDE.md`: where a
modern method is plainly better, take it. This was settled mid-session and it
reframes work that came before it — the R12 target exists to give the tool a
coherent shape, not to reproduce 1992's limits.

Two guardrails go with it, and they are what stop it becoming drift:

1. A divergence must not break DXF R12 interchange. Richer geometry in the
   database is fine; it has to **degrade honestly on the way out**.
2. Where R12's behaviour is a considered choice rather than a limitation, keep
   it. Escape preserving committed work, the counterclockwise arc convention,
   negative-radius-means-major-arc.

`ELLIPSE` is the worked example of both, and the template for `SPLINE`.

## What landed, oldest first

| Commit | What |
|---|---|
| `06adee7` | Conditioning: `intersect_line_circle` rewritten; `test_numeric.cpp` |
| `3d12dfe` | Selection window draws as a box; the view-frame bug |
| `33c3bb3` | The UCS icon |
| `280e1a7` | `HighlightRenderer` and the subset scene walkers |
| `79de099` | **`InFlight`** — the ghost mechanism |
| `b998ab7` | Keyboard routing: every key belongs to the command line |
| `fc295aa` | The XYZ cursor, drawn along the UCS |
| `ef38a8d` | UCS Delete left `UCSNAME` dangling |
| `e2c5a43` | **ARC** |
| `6f63038` | SETVAR, OSNAP, ORTHO |
| `5ca5fa7` | MEASUREGEOM |
| `a4b14e4` | ARC and CIRCLE preview the curve |
| `a11ca80` | The registry self-test, and R12-as-floor in `CLAUDE.md` |
| `1355bc7` | LINE and PLINE preview the segment |
| `03eaf7b` | **ELLIPSE** — the first entity R12's DXF cannot name |
| `a472edf` | **SPLINE** — a real NURBS curve, planned by impact agent first |
| `60726f9` | iperl at the prompt: `=2*$pi*5` wherever a number is wanted |

Read the commit message before changing anything it touches. Several record
reasoning that looks arbitrary from the code alone.

## `InFlight` — the mechanism most of this session rests on

`include/noto/inflight.hpp`. What a command *would* do if you clicked now:
`ghosts` (modified clones, drawn highlighted) plus `suppressed` (committed
entities hidden behind them). `Command::preview()` is the vtable slot;
`CommandEngine::preview()` is the entry.

**The abstraction is not a transform.** A pending `Mat4` fails at STRETCH and at
grips. The command is asked what the result would look like and answers however
it likes.

**Two invariants, both tested.** It never touches the database or the undo
journal — a mouse-move reaching the journal would make every pixel an undo step.
And it is derived, never stored, so it cannot go stale behind an undo.

**The rule that keeps it honest:** preview and commit derive the change with the
*same code*, differing only in whether the result is written back. Every command
with a preview was refactored into build-then-write to achieve that, and every
one came out simpler — ARC lost eight duplicated terminal branches. `test_inflight.cpp`
drives each command to its last prompt, captures the ghost, commits, and compares.
**If you add a preview, add that test.** It is the only thing standing between
this and the usual slow divergence.

Eleven command classes define `preview()`: LINE, PLINE, ARC, CIRCLE, ELLIPSE,
SPLINE, MEASUREGEOM, STRETCH, ROTATE3D, `MoveCommand` (MOVE/COPY) and
`TransformCommand` (ROTATE/SCALE/MIRROR). Still wanted: POINT, SOLID, INSERT,
BREAK, TRIM (all cheap — the machinery exists). PEDIT is awkward, TEXT waits on
fonts, ARRAY is deferred on repaint cost.

## Traps a fresh instance will otherwise hit

Carried forward from the previous handoff, still true:

- **`Mat4::from_basis(origin, ax, ay, az)` builds world-TO-basis** — axes in the
  *rows*. For basis-to-world set the columns by hand; see `compose_placement()`.
- **A positive bulge arcs BELOW a left-to-right chord.** `test_polyline.cpp:116`.
- **`Sysvars::set_owned()` bypasses the read-only flag but never the journal.**
- **A test calling `InputValue::of_point()` bypasses the UCS conversion.** Typed
  coordinates go through `parse_input()`. `test_ucs.cpp` has `typed_point()`.
- **`ncad_gui` cannot be launched from here.** X11 over SSH. There is a memory
  about it. Verify GUI work by reasoning and headless tests, and say in the
  commit message that you did not see it.
- **Commit straight to `main`.** Sadie does not use branches.

New this session:

- **Out-of-range `setvar` is REFUSED, not clamped**, and the old value stands.
  A test asserted clamping and was wrong.
- **A selection region ignores depth.** A "zero-width" box still catches
  anything whose projection lands inside it — which cost two wrong assertions.
- **Two routes into one state need a flag, not a sentinel.** ARC and ELLIPSE
  both hit this: a stored point cannot say which way the command arrived,
  because a legitimate answer can equal the sentinel. Watch for it in any
  command with an alternate entry.
- **`arc_segment_count(radius, sweep, tolerance)`** is the shared flattening
  budget. For an ellipse, pass the *major* axis — the minor under-segments the
  ends, which is where the curve turns hardest.

## How I broke the build, and what to do instead

Worth reading before any large refactor.

A Python splice took `s.index('// --- SETVAR ---')` as a range endpoint,
assuming SETVAR followed ARC. It does not — PLINE and MEASUREGEOM sit between
them, and both were silently deleted. It compiled clean and died at `ld` with
"undefined reference to vtable", which is a confusing way to learn you removed
two commands.

The mistake was **using an unverified string as a range boundary**. A wrong
anchor for an insertion fails safely; a wrong anchor for a range end deletes
everything in between. Use `Edit`, which requires a unique exact match, or
assert `count == 1` before replacing. And run `git diff --stat` before
committing: a net-neutral refactor showing −300 lines is a loud signal.

`tests/test_registry.cpp` exists because of this. It asserts the command count
as a **hard literal** — raise it on purpose when adding a command, treat it
dropping as a bug. A count that adjusted itself would catch nothing.

## Codegraph

`.codegraph/` exists and was re-indexed on 2026-07-29 (150 files, 3,953 nodes).

**Only `codegraph_explore` is exposed by the MCP server.** The lightweight tools
the user's global `CLAUDE.md` describes — `codegraph_search`, `codegraph_callers`,
`codegraph_impact`, `codegraph_node` — do not exist here. Since the same rules
forbid calling `explore` from the main session, the main session has no direct
graph access at all: spawn an Explore agent or use grep.

**The working rule agreed this session:** for a cross-cutting change — a new
entity type, a new vtable slot, a changed shared signature — spawn an Explore
agent for the impact list *first* and tick off every site it names. ELLIPSE was
done without that and the AutoLISP converter was missed, then found by accident
in a smoke test. For a single known location, grep directly.

## What is next

**The osnap gap on ELLIPSE and SPLINE, and it is the top item.** Sadie confirmed
it by testing: on a full ellipse, QUA and CEN work and *nothing else does* —
NEAREST especially, which surprised her and is the one that will be missed.

The split is not obvious from outside, and it is not the same split on the two
entities. **The STORED snaps are whatever the entity's own `osnap_points()`
reports, and those work.** For `Ellipse` (`entities.cpp:391`) that is CEN, four
QUA, and — on an elliptical *arc* only — both ENDs, which is why testing on an
arc rather than a full ellipse makes the report look wrong. QUA is correctly
orientation-independent because it comes from the entity. For `Spline`
(`spline.cpp:329`) the stored set is entirely different: END, NODE on each fit
point, and a parameter-space MID. No CEN and no QUA exist on a spline at all.

What both entities share is the DERIVED half. **NEAREST, PERPENDICULAR and
TANGENT are computed in `osnap_derived.cpp`**, which reaches for `as_line()` or
`as_circular()` and gets neither from an ellipse or a spline. **INT** fails for
a third reason: `intersect.cpp`'s `decompose()` has no case for either type and
returns an empty span list, which also makes both invisible to TRIM, BREAK and
EXTEND.

`SF_todo.md`'s "Spline: what was left undone" has the full analysis and the
likely shape of the fix. Three separable pieces, cheapest first:

1. **NEAREST** on both. A projection onto the curve — closed form for an
   ellipse, a Newton iteration on the distance function for a spline.
   Independent of everything below.
2. **PERPENDICULAR and TANGENT.** Same machinery, more algebra. See the deferred
   tangent below before building TANGENT — it changes what the answer has to be.
3. **INT, and therefore TRIM/BREAK/EXTEND.** This is the structural one:
   `SubCurve` models a line segment or a circular arc, and neither an ellipse
   nor a NURBS span is either. The likely answer is a third `SubCurve` form
   carrying a flattened polyline with its parameter mapping — which costs
   exactness at the intersection, and is a real decision rather than a detail.

### The deferred tangent — a real gap, found by Sadie in AutoCAD 2026

**TANGENT as the FIRST point of a line cannot be resolved when it is picked.**
There is no tangent until the other end exists. AutoCAD defers it: the pick
records *which circle* was hit, the point slides along that circle as the second
point moves, and it lands on the true tangent when the second point is fixed.
ncad does not do this — an initial TAN pick resolves immediately to whatever
point it likes at pick time, and that point then stays put, so the finished line
is not tangent to anything. Same for the tangent-tangent case, where neither end
is known until both are.

This is not a rendering or preview bug, and the signature is the evidence:
`tangent_points(const Entity&, const Vec3& ref, Vec3 out[2])` in
`osnap_derived.hpp:45` *requires* the far end. On a first pick there is no `ref`
to give it. It means an osnap result needs to be able to be a **constraint
carrying its source entity**, resolved at commit, rather than only a `Vec3`. Worth deciding before TANGENT is built for ellipse and
spline, because retrofitting the deferred form afterwards touches the same code
twice. R12 behaved this way too, so this is fidelity, not a divergence.

**Do not validate ellipse TANGENT against AutoCAD 2026 — Sadie tested it and it
finds the wrong point.** Check ours against the geometry directly: the tangency
condition, not a reference implementation. This is a case where the newer tool
is simply wrong and matching it would be a bug.

After that, in Sadie's stated priority order:

1. The remaining cheap previews — POINT, SOLID, INSERT, BREAK, TRIM.
2. **TEXT**, via a bundled Hershey font. Settled in `SF_todo.md`, including why
   it is not a hack. **Check the licensing of the distribution before it goes
   in-tree** — public domain in origin, but circulated packagings carry an
   attribution condition.
3. OFFSET, FILLET, CHAMFER, CHANGE/CHPROP — all unblocked, all with the
   machinery already present.
4. A save cycle: NEW, OPEN, SAVE, SAVEAS, QSAVE. There is no current-filename
   concept at all, and `DXFOUT` asks for a name every time.

**Also on Sadie's own list, not the project's:** driving ncad *from* iperl —
making it a scripting language alongside AutoLISP rather than only a calculator.
That needs a protocol in both directions over the same pipe. The calculator was
built with that in mind but nothing was built for it.

**One documented limit worth not rediscovering:** an `=expr` given as an ANSWER
may not contain spaces, because a line of answers is tokenised on whitespace.
See the comment in `src/app/iperl.hpp`.

## Owed verification, none of it doable from here

- **Everything GUI since `33c3bb3`** — the UCS icon, the XYZ cursor, the ORTHO
  constraint, selection highlighting, and every ghost. None has been seen. The
  cursor especially: `Qt::BlankCursor` may not hide the pointer under XQuartz,
  which Sadie is checking on display :0.
- **`tests/acad/t2_chain.lsp` with `*t2-accumulate*` set to `nil`** — confirms
  whether the N² error growth is the accumulating angle.
- **Whether AutoCAD leaves you in a deleted UCS's frame.** `test_ucs.cpp` pins
  current behaviour and names the assertion to flip.
- **Whether R12 had a view-extent limit** like our `kMaxViewHeight = 1e12`. If it
  did, ours is faithful rather than restrictive.
- **ARRAY's spacing, STRETCH on an arc caught by its centre, and the UCS table's
  DXF group codes** — carried from the previous handoff, still unchecked.
