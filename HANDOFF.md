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

## 2026-09-11 — Tier 1 backports from NotoCAD-solids

Ten commits cherry-picked onto `solids-backports` from
`NotoCAD-solids @ run/2026-09-08-naming-and-shells`, the Tier 1 set of
ARCHITECTURE-REVIEW.md §9 in the Nsolid tree. Each carries a
`Cherry-picked-from:` trailer naming its origin and what it dropped. **1254
cases before, 1270 after; ctest 11/11 throughout; the GUI target links.** No
command was added, so the version stays **0.2.74**.

| new | from | what it dropped |
|---|---|---|
| `41b8e3c` | `9b9f755` | the "Solids — wanted" section (direction markers, N-ary booleans) |
| `282b449` | `a2460e1` | nothing — doc-only |
| `fdc3bb8` | `fd1caee` | nothing — doc-only, applied clean |
| `4a3fb0e` | `50ebbb4` | nothing — base-only in full |
| `ccbbf90` | `da6b5e2` | nothing — base-only in full |
| `de97b2b` | `20e8a55` | `engine_root.hpp`, the engine-reachable harness case, the eight solids suites' SKIP conversions, `rebuild.sh`, the SF_todo and HANDOFF records |
| `4ac4365` | `42372f6` | item 1 entirely (the chord policy) and every solids reader; **item 3 only**, despite the subject, which is kept verbatim as every backport's is |
| `409d332` | `b957618` | `solids.hpp`, `solids_commands.cpp`, `test_thicken.cpp` |
| `94ccef3` | `b039986` | `09-solids.md`, the quickstart's Solids section, the HANDOFF record |
| `25ff32a` | `d410812` | everything but `Database::rename_block` |

**`9b9f755` is a PREREQUISITE, not a Tier 1 commit.** It opened the
features.md section that all four picking commits edit; without it `fd1caee`
and `a2460e1` are features.md-only picks with nothing to apply to, and
`50ebbb4` conflicts against an absent section. Its two solids notes were
dropped. Worth telling the steward, since §9 does not list it.

**Nothing was skipped, but one listed commit lands nothing.** `2ec4407`
("The designer's manual, with every transcript actually run") is entirely
`docs/using-solids.md`, which `b039986` renames to chapter 9, plus a
quickstart section pointing at it. There is no non-solids half, so it is
recorded here rather than committed.

**`rebuild.sh` did not come with brief 32**, contrary to §9: the base has no
such file, and it arrives in the solids tree with `a05de75`, a **Tier 2**
commit. Only the generic harness — `test.hpp`'s SKIP/verdict machinery and
`test_harness.cpp`'s first two cases — was taken. `NCAD_TESTS_ALLOW_NO_ENGINE`
keeps its name so that a case backported later from a tree that has an engine
arrives working; nothing here skips, so the count is always 0.

**The manual's ratchet is 54, not the solids tree's 51**, and the two are not
comparable: that number was measured against a registry of 86 with a ninth
chapter covering the solids commands, and this registry holds 74 with no
chapter 9. Measured here on the day it landed: **20 of 74 documented, 54
owed.** The chapter-count assertion is 8 rather than 9 for the same reason.
Chapter 1's `?` transcript was re-run against this program, its
"Regeneration" section removed (`REGEN` is not in this registry), and every
version and count re-stamped to 0.2.74/74.

**Two things not in any original hunk**, both recorded in their commits:
`NCAD_SOURCE_DIR` in `tests/CMakeLists.txt`, which `test_eps.cpp`'s grep needs
and which reached the solids tree with a STEP commit; and a `test_blocks.cpp`
case for `rename_block`, which arrives with no caller in this program and had
no separable test upstream.

**Brief 49's four open rows are still open.** ARRAY's two spacings, PEDIT's
width, LTSCALE and ZOOM no longer measure a click from the world origin —
the predicate makes them unreachable with a point — but the dead
`distance_from(..., Vec3{0,0,0}, ...)` calls remain at `commands.cpp:4706`,
`:5623`, `:5631`, `:6278` and `:6427`, reachable only from AutoLISP. The
audit table is carried into `409d332`'s message. Half an hour of work,
deliberately not taken.
