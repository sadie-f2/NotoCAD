# Handoff — session ending 2026-08-17

Session-scoped and disposable. `CLAUDE.md` holds settled rules, `SF_todo.md` the
roadmap and the reasoning, `features.md` capabilities not yet built,
`SF_strategy.md` the long horizon, `solids.md` the engine design and
`programming.md` the scripting and parametric direction. **This file is only the
bridge.** Delete or rewrite it rather than letting it rot.

## Where things stand

**0.2.74**, 1224 tests, sanitizer clean, `main` level with `origin/main`.

**Sadie's assessment, and it sets the shape of what comes next: this code is
release-ready.** Close enough for jazz, no serious design flaws known, and the
17 August audit closed the memory-safety questions that were open. What remains
in `SF_todo` is a long tail rather than a blocker.

So the next work is **solids, for real**.

## This session

- **PLOT**, to PDF, from the core. Display, Extents, Limits and Window; fits to
  A4 landscape; black line work. `plot.hpp` records why the writer is in the core
  rather than reached through Qt — a command only the window can run is one
  `(command "PLOT" ...)` cannot drive. Verified by rendering the output through
  macOS's own PDF engine and looking at it.
- **DXF read held the file twice** before parsing a byte. `.str()` on an
  `ostringstream` returns a copy, so a 2.1 GB drawing occupied itself twice over
  plus stream slack. One buffer now: 7.8 GB → 5.05 GB on that file, a third off.
- **OPEN warned you about a lock you had just taken yourself.** SAVE and DXFOUT
  both guard with `ctx.locks->holds(path)`; the OPEN warning was written before
  the write side existed and never learned.
- **The audit**, on the Linux box — twenty-two defects, `code-review_8-17.md`,
  and `SF_todo` now carries the parts that outlive it.

## Next: solids, and how Sadie wants it started

**A prototype, expected to be a failing first try.** That is the stated
intention, and it should shape what gets built: the point of the first attempt is
to find out which parts of `solids.md` survive contact, not to produce something
that lasts. Build it to be thrown away and it will teach more.

`solids.md` is the design and is deliberately written as a proposal rather than
in settled voice. The parts most likely to break first, in the order they
probably will:

1. **Evaluation and parent-child.** Named there as the question that gates code.
   The boundary that makes solids easy makes evaluation hard: the process split
   is clean because it is thin, but a parametric relationship CROSSES it, with
   the profile in ncad and the shape in the kernel.
2. **The tessellation-is-never-authoritative rule**, and the feature-description
   caching that keeps it affordable. Untested against a real kernel at a real
   frame rate.
3. **What the solids UI is.** R12 offers no fidelity target, so this is design
   rather than reconstruction, and it is wide open.

Read `programming.md` alongside: the feature tree IS the parametric engine and
the sketcher IS the constraint solver, so those sit underneath solids rather than
after them. `SF_strategy.md` carries the strategic decisions with its superseded
items marked rather than deleted.

## A version thought, NOT a decision

Sadie's, recorded because it will otherwise be re-derived. Roughly: **dub the
release 0.3**, keep a component that is the command count, and add a readable
point-release number so that naming a build does not mean quoting a commit hash.

Two things to pin before implementing it, because the current scheme collides:

- **`CLAUDE.md` says the PATCH number is the command count** ("0.2.72 means 72
  commands"), and `tests/test_registry.cpp` asserts the two agree. The note above
  says "minor", which is probably loose usage rather than a change of meaning —
  but which component carries the count has to be stated before anything moves.
- **A fourth component is available.** CMake's `project(VERSION)` accepts
  `major.minor.patch.tweak`, so `0.3.74.xx` needs no scheme invention — only a
  decision about what increments `xx`, and when.

Nothing was changed. `project(VERSION 0.2.74)` and the registry assertion still
agree.

## Still open, carried forward

The long tail, none of it blocking a release:

- **The SizeAllCursor crash**, three times, all with the identical Qt stack. The
  command-line failure that accompanied the third turned out to be a separate bug
  and is fixed — so the third crash carries no more information than the first
  two, and the memory-corruption candidate gets no support from it. The LTS Qt
  6.8.3 the bundle ships is still the untried discriminator, and it is still
  untested on Linux, where a crash would rule the Cocoa path out entirely.
- **One audit GUI finding untested and unfixed** — the unguarded
  `static_cast<int>` at `viewport_widget.cpp:491`. Two more were fixed but never
  interactively confirmed.
- **A non-monotone spline knot vector is technically UB** and observably
  harmless. Known and accepted.
- **Locks are done and the reciprocity question is closed**, by Autodesk's own
  documentation: `.dwl`/`.dwl2` have been informational since AutoCAD 2000 and
  WHOHAS is DWG-only, so AutoCAD will never refuse a file we hold. Reading theirs
  works, which is what was actually asked for.
- **PLOT's second phase**: scale, paper size, and the colour-to-pen table, which
  is the only route to varied lineweight. The GUI's native print dialog too.
- **`?` listings only print when the command exits**; `c:` functions are not
  dispatched as commands; TRIM and EXTEND cannot be driven from LISP.
- **Memory**: a line-work drawing costs about 1.3 KB of RSS per entity and nobody
  has looked at where that goes. The whole file text is also resident for the
  duration of a parse — and the measurement says the drawing is the smaller half,
  so a streaming reader is the larger win.

## Traps worth knowing

- **"Sanitizer clean" on macOS does not mean it compiles on Linux.** `plot.hpp`
  used `std::uint8_t` without `<cstdint>`; libc++ forgave it transitively,
  libstdc++ did not, and the audit found the ASan build broken on `main` as a
  result. Seven other headers are one reordering from the same thing. **Check
  both platforms before calling a build clean.**
- **`DimKind`'s values are DXF's and are not contiguous** (Angular is 5).
- **`entity_subrs.cpp`'s `entget` converter has a `default:`**, so a new entity
  type compiles and silently returns nothing from LISP.
- **`EntityType` has six registration sites**, only one of which fails loudly.
- **BSD `sed` does not support `\b`.** Use `perl -pi -e` with a lookbehind.
- **`/scripts/` is gitignored** and is scratch only. Anything the build, the
  tests or a release depends on belongs in `packaging/` or `tests/acad/`.
- **`examples/` is gitignored too** — the AutoCAD lock specimens, the crash
  reports and the comparison screenshots live there and do NOT travel on a pull.
- **The AutoCAD comparison is worth more than any test we write**, diffing
  against the SOURCE rather than against AutoCAD's output. Sadie's licence is
  time-limited, and `solids.md` lists the three experiments only it can answer.
