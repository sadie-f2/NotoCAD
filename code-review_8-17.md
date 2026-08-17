# Audit — 17 August 2026

A whole-repository audit for crashes and memory safety, at 0.2.74. Five parallel
agents by subsystem, then every claim checked against the running binary rather
than taken on the agent's word — which mattered: one reported path did not
reproduce, and one "theoretical" finding turned out to be reachable by typing six
commands.

**The method is worth repeating, and the fuzz half is worth keeping.** Reasoning
found the defects; execution decided which were real, and in two cases corrected
the severity in both directions.

## What the audit was looking for

Crashes and memory safety, chosen over correctness because the docs record two
segfaults in the month before it, and because a crash now also strands a drawing
lock — `SF_todo.md` makes that argument itself.

It was not looking for design or duplication problems, and did not report any.

---

## Fixed

Five commits, `96239a4` through `08de5f1`. Every one has a regression test, and
two of those tests were checked to genuinely bite: with the undo fix reverted the
suite *aborts* under ASan, and with the DXF fix reverted the fuzz corpus catches
the use-after-free.

| Defect | Reachable by | Commit |
|---|---|---|
| The sanitizer build did not compile | `cmake --build build-asan` | `96239a4` |
| Undo freed a block definition an Insert still pointed at | six typed commands | `72f0e75` |
| `(append)` segfaulted | one token | `bea712a` |
| `mapcar` over a `defun` — UAF, wrong answer in release | ordinary LISP | `bea712a` |
| Reader stack overflow at 50k nesting | a generated script | `bea712a` |
| Printer overflow, including inside error formatting | a deep list, built iteratively | `bea712a` |
| `wcmatch` recursed per leading tilde | a built-up pattern | `bea712a` |
| Five use-after-frees in the DXF block reader | any malformed file | `1905f4c` |
| `nan`/`inf` entered the drawing **and left in the DXF** | `10\nnan` | `1905f4c` |
| Colour `62/32768` indexed the palette from `-32768` | a DXF colour group | `1905f4c` |
| The correctness gate contained no blocks or polylines | — | `08de5f1` |

Two of those deserve to be remembered rather than just listed.

**`mapcar` did not crash — it lied.** In release it reported "no function
definition" for a function that was perfectly well defined, because it re-read
the function pointer from a stack buffer that `apply` had reallocated. Silent
wrong answers in the one language feature this project exists to be driven by,
and 1188 tests never noticed.

**The sanitizer build was already broken on `main`.** `plot.hpp` spells
`std::uint8_t` without including `<cstdint>`; the release build forgives it
through a transitive include and Debug does not. The suite `CLAUDE.md` calls
"worth running after any arena or interpreter work" could not be run at all.

---

## The GUI findings, after testing

All six were reasoned from code and none observed, because the Linux box has no
display. Sadie tested them over X11 from a MacBook on 17 August. Four were real
and are fixed in `5f5bfa4`; one was **not a bug**; one remains untested and
unfixed.

The negative result is the most useful line in this section. It is recorded at
length so that nobody re-derives the same plausible-looking reasoning and
"fixes" it again.

### 1. The key filter's scope — DID NOT REPRODUCE. Not a bug.

The claim: the filter is installed on `qApp` (`main_window.cpp:178`), so it sees
key presses destined for every object in the process. A modal dialog would
therefore have its keystrokes stolen — `input_->setFocus()` cannot make
`hasFocus()` true while the dialog owns the active window, so the early-out never
engages. The predicted symptom was that Enter at "Save before closing?" would not
activate the default button but would instead run `feed_line("")`, which at an
interactive prompt repeats the last command (`prompt.cpp:451`) — arbitrary
command execution inside `closeEvent`.

**Tested, and it does not happen.** At the save dialog Enter activates the
default button; QSAVE runs because QSAVE is what the button does, and it appears
in the transcript because the transcript reports commands. It does not re-run the
previous command. Checked on an earlier build too, so this is not something a
recent change repaired.

Why the reasoning was wrong is not established, and does not much matter: Qt
delivers key events to the focus widget through a path this filter does not
straddle in the way the argument assumed. What matters is that a change WAS
written to "fix" it and then discarded, because changing key routing to repair a
phantom is worse than leaving it alone.

The one piece kept from that work is unrelated and defensible on its own:
`~MainWindow` now removes the filter explicitly, since it is installed on `qApp`
and would otherwise outlive `command_line_`, which it dereferences.

If this is ever revisited, the bar is a reproduction, not an argument.

### 2. Destruction order is inverted — STILL OPEN

`main_window.hpp:127-138`. `db_` and `engine_` are members, so they are destroyed
*before* `~QMainWindow` deletes the child widgets — and `ViewportWidget` holds
`const Database&` and `CommandEngine*`. The event filter also stays installed on
`qApp` until `~QObject` runs last, and it dereferences `command_line_`, which
`~QWidget` has already freed.

Nothing reaches it today: `ViewportWidget`'s destructor is defaulted and
`leaveEvent` touches only `snap_`. It is a latent hazard, and any handler added
to `ViewportWidget` that reads `db_` or `engine_` makes it immediate.

### 3. Middle-drag un-hid the system cursor — CONFIRMED, fixed in `5f5bfa4`

`viewport_widget.cpp:169` sets `Qt::BlankCursor` because the crosshair is painted
against the UCS; `:850` restores `Qt::CrossCursor` on release. Pan once and you
have two crosshairs, the window system's one screen-aligned and contradicting the
UCS one the feature exists to provide. Cosmetic, one word to change, and given
`babd71d` ("both crashes were the same cursor") it is worth doing carefully
rather than casually.

### 4. The osnap marker survived Escape — CONFIRMED, fixed in `5f5bfa4`

`viewport_widget.cpp:575-579`. `on_cancel_requested` calls `update()` but not
`refresh_osnap()` — the typed cancel path at `main_window.cpp:393` does. Start
LINE, hover an endpoint until the glyph shows, press Escape without moving: the
marker and its label stay painted over a drawing with no command running. Not a
crash; the handle is re-validated through `Database::get`.

### 5. `push_view()` ran even when the view did not change — fixed in `5f5bfa4`, not interactively confirmed

`viewport_widget.cpp:187-191`. Home ten times on an empty drawing and the 10-deep
view stack fills with identical copies, evicting every real previous view.

### 6. Unguarded `static_cast<int>` on a projected coordinate — fixed in `5f5bfa4`, not interactively confirmed

`viewport_widget.cpp:491`. `qpainter_renderer.cpp:36` documents this exact hazard
and clamps; this call site does not. Needs a UCS origin far from the model plus
extreme zoom.

---

## Verified structurally, not reproduced

Real in the code, but the entry points are `draw()` and `accumulate_bbox()`,
which are reached from the viewport. Could not be triggered from the CLI.

### Exponential block traversal

A block containing two `INSERT`s of itself loads without complaint — confirmed;
the cycle is accepted into the database. `kMaxBlockDepth = 32` (`blocks.hpp:44`)
bounds *depth*, and there is no visited set, so the fan-out is 2³² traversals.
`insert.cpp:8` says a DXF "may claim a cycle that cannot occur in a drawing this
program built" — the guard was written for this case and does not bound it.

`flatten_definition` clones an entity per leaf, so EXPLODE is unbounded
*allocation*, not merely CPU.

Trigger file: a ~30-line DXF, in the audit scratch notes below.

### MINSERT counts drive a 10⁹ loop

`dxf_read.cpp:486-491` passes groups 70/71 to `set_array` with no clamp against
the definition's cost. 32767 × 32767 ≈ 1.07e9 full traversals per insert, per
frame. Combined with the cycle above, (10⁹)³².

---

## Found, headless-testable, not started

These came from the commands audit and need no display. A natural next batch.

1. ~~**`begin()` does not clear a running transparent command.**~~ **FIXED in
   `ba518cb`, and it was the bug from `a684f9f`.** That commit left a command
   line "answering *Cancel* to every valid command" with two candidate
   explanations; this was the second of them, "a command stuck at Waiting
   swallowing input". Sadie confirms it is what she experienced before the audit
   began. Reproduced headlessly with
   `LINE / 1,1 / 'ID / (command) / CIRCLE / 5,5 / 3` — ID and DIST are
   transparent and need no view, which is what made it testable without a
   display.

2. ~~**A transparent command is leaked on any non-`Waiting` status.**~~ **FIXED
   in `ba518cb`**, the same mechanism reached the other way.

3. ~~**A stale crossing region misdirects the next STRETCH.**~~ **FIXED.** The
   region is now cleared in `begin()` whether or not the set was empty.

4. **OPEN journals every entity as its own undo group.** `read_dxf_text` clears
   the journal at the start, while the OPEN command's group is open, so `push()`
   runs at `depth_ == 0` and each change allocates a whole `UndoGroup` holding a
   full `Entity::clone()`. Freed at the end, so a transient ~2× memory spike
   proportional to file size rather than a leak — but unbounded, and paid on
   every OPEN.

5. **NEW and OPEN leave stale handles in the selection and in `previous_`.** Not
   memory-unsafe — every consumer re-looks-up and was checked. The visible effect
   is a selection count referring to a drawing that no longer exists.

6. **Eight headers rely on a transitive `<cstdint>`**: `blocks`, `dash`,
   `osnap_search`, `tables`, `commands`, `database`, `pick`, `entities`. Only
   `plot.hpp` actually broke, and it is fixed; the rest are one reordering away
   from the same thing. A tidy, not a bug.

7. **Two quadratic load paths.** `apply_common` calls `find_layer` per entity and
   `resolve_inserts` calls `find_block` per insert, both linear scans. 10k blocks
   × 500k inserts is ~5e9 string compares at load, with no progress and no
   cancel. Not memory-unsafe; load-time DoS from a *valid* file.

8. **`$UCSORG` consumes three group pairs unconditionally** (`dxf_read.cpp:855`).
   A short one eats the following `0`/`SECTION` pair, so the whole ENTITIES
   section is skipped and the file loads "ok" and empty. Silent data loss.

---

## Checked and clean

Recorded so nobody re-audits them.

- **The DXF reader, against 193 mutations** of the sample drawing — truncation at
  every 3%, non-numeric group codes, absurd counts, `nan`/`inf`/`1e400`, dropped
  lines so counts disagree and SEQEND never arrives, random bit flips. Clean
  under ASan both before and after the block-bearing seed was added.
- **No unbounded allocation driven by a file-declared count.** Vertex, knot and
  weight vectors grow from groups actually present; a file claiming four billion
  vertices has no effect.
- **Command state machines against LISP abuse**: wrong value kinds, empty
  selections, negative ARRAY counts, SCALE by zero, ROTATE3D with a degenerate
  axis, OFFSET by zero. All errors, no crashes.
- **Binding unwind on every error path**, including the arity and
  "parameter is not a symbol" paths; `depth_` decrements symmetrically.
- **No command class stores a raw `Entity*`, reference or iterator across
  suspension** — all persistent references are `Handle` or `BlockId`, re-resolved
  each step.
- **Every nullable interface use is guarded**: `ctx.view`, `ctx.scripts`,
  `ctx.clipboard`, `ctx.locks`, at all ten call sites.
- **The spline degree clamp covers every path that indexes by degree**, verified
  by exhaustive enumeration; `find_span` proven non-hanging and in-range over
  ~2.5M malformed knot vectors. The documented GCC false positive at
  `spline.cpp:313` remains a false positive and its reasoning is sound.
- **Qt lifetime**: every `new` takes a parent, `createPopupMenu()`'s result is
  held in a `unique_ptr`, and the `aboutToQuit` lambda passes `this` as the
  connection context.

### Two remaining, known and accepted

- **A non-monotone spline knot vector violates `std::clamp`'s precondition**
  (`spline.cpp:172`). `valid()` checks knot *count*, never ordering, so
  `domain_min() > domain_max()` is reachable via `entmake`. Technically UB;
  libstdc++ returns `hi` and nothing observable happens. Did not trip UBSan.
- **`dash.cpp:85` has no lower bound on the pattern period.** `(setvar "LTSCALE"
  1.0e-6)` on a 1000-unit line is ~1.3e9 iterations. Real sysvars get no range
  validation, and group 49 from a DXF is unvalidated. Redraw path, so GUI-only.

---

## Reproducing any of this

The scratch harness is not in the tree — it is a mutation script plus generated
cases. Rebuilding it is a few minutes:

- **Fuzzing the reader**: emit a seed with `./build/tools/ncad_gen_sample seed.dxf`
  (it now contains blocks, a MINSERT and a bulged polyline), mutate it —
  truncation, junk group codes, junk values, dropped lines, bit flips — and run
  each through `ncad -e '(command "DXFIN" "<file>")'` under `build-asan`, watching
  for a non-zero signal or the string `sanitizer` on stderr.
- **The LISP crashes** are all one-liners against `build-asan/src/app/ncad -e`.
- **The undo one** needs the prompt rather than `-e`, because the sequence spans
  commands: pipe `LINE / BLOCK / INSERT / U / U / REDO / REDO / DXFOUT` in.

Worth considering as a tracked target rather than a scratch script, given it has
now paid for itself twice and the seed drawing is already in the tree.
