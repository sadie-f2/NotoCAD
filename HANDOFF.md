# Handoff — session ending 2026-07-31 (Linux → macOS)

Session-scoped and disposable. `CLAUDE.md` holds settled rules, `SF_todo.md` the
roadmap and the reasoning, `features.md` capabilities not yet built, and
`SF_strategy.md` the long horizon. **This file is only the bridge.** Delete or
rewrite it rather than letting it rot.

**Written for a macOS instance to pick up**, because this one can design but
cannot compile for that platform. The port is phase 15 and has never been
attempted.

## Where things stand

- On **`main`**. **1019 tests, 0 failed.** Version **0.1.58** — the patch number
  is the command count, and no commands were added this session.
- **R2000 (AC1015) output is CONFIRMED IN AUTOCAD 2026** for 43 of the 44
  entities in the conformance drawing. That was the session's main work.
- **MINSERT is the one unsolved record.** Held out of the drawing by
  `*cf-minsert*`. See below.
- The `noto` namespace, headers and library targets are now **`ncad`**
  throughout. `include/ncad/`, `ncad_core`, `ncad_lisp`, `ncad_app`,
  `ncad_gui_lib` — the GUI library carries the `_lib` suffix because
  `ncad_gui` is the executable.

## FIRST: push, or the Mac sees none of this

At the time of writing there are **15 unpushed commits**, which is normal for
this project but blocking here — a fresh clone would predate the rename, the
R2000 fixes and the conformance drawing. Push before starting the Mac instance.

## What happened this session

**The `noto` → `ncad` rename.** Namespace, header directory, every CMake target,
the `NCAD_*` options, `NCAD_IPERL`. Verified with a from-scratch configure and
build, headless and with `NCAD_BUILD_GUI=ON`.

**iperl is no longer machine-specific.** The `--pipe` patch is committed in
`~/src/iperl` (it was uncommitted for weeks), and `script_path()` now searches
`NCAD_IPERL`, then `~/src/iperl/iperl.pl` and `~/iperl.pl`, then **PATH**. A
checkout still beats an installed copy so that working on iperl tests the copy
being worked on.

**The R2000 conformance drawing.** `tests/acad/r2000_conformance.lsp` builds ten
labelled stations holding one of every entity type reachable;
`tests/acad/gen_conformance.sh` writes it to the NAS share under a
serial-numbered name and prints the md5. Six faults came out of it, every one
structure rather than geometry, and each is pinned by a test in
`tests/test_dxf.cpp`. `SF_todo.md` has the table.

**Two rules worth carrying**, both learned the expensive way:

- **The shape of a record must not depend on its content.** An optional group
  omitted immediately before a class separator leaves the reader inside the
  parent class. TEXT's group 73 and an INSERT's scale and rotation are written
  unconditionally at R2000 because of this.
- **Presence is not the property; position is.** AutoCAD accepted a misplaced
  subclass marker on a LINE and refused the identical mistake on a TEXT.

**Point input fixes.** `1, 1, 1` is now one point rather than three answers — a
comma joins across spaces, which changes the meaning of no valid line because a
token ending in a comma cannot be a complete answer. And `=` is now
**per-coordinate**, so `=$pi,=$pi,1` works while `=join(",",3,4)` stays one
expression.

## macOS: what to actually do

Nothing has ever been built on macOS. The core is POSIX and standard C++20, so
the expectation is that it mostly works and the interesting output is the list of
what does not.

```sh
cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
./build/tests/ncad_tests
```

Then the GUI, which is the part with a real dependency:

```sh
brew install qt ninja
cmake -S . -B build-gui -G Ninja -DNCAD_BUILD_GUI=ON \
      -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build-gui
```

**Qt must be dynamically linked.** `src/gui/CMakeLists.txt` aborts the configure
if it finds a static Qt, on purpose: the BSD-3 core must not acquire LGPLv3
relinking obligations. Homebrew's Qt is shared, so this should pass — if it
fires, that is the guard working, not a bug to route around.

### Known risk points, in the order I would check them

1. **`std::numbers`** (`<numbers>`) is used in the geometry and DXF code. Needs a
   recent libc++; Xcode 14 or newer. The most likely hard failure.
2. **`tests/test_save.cpp:205`** writes to `/proc/definitely/not/writable.dxf` to
   prove an unwritable path leaves the drawing dirty. macOS has no `/proc`, so it
   should still fail to open and the test should still pass — but for a different
   reason than intended. If it passes, it passes by luck; consider a path that is
   unwritable on both.
3. **`-Wconversion` is on** and warnings are meant to stay quiet. AppleClang
   warns in places GCC does not, so expect noise that is real rather than
   spurious.
4. **iperl** shells out via `execlp("perl", ...)`. macOS ships perl, and the PATH
   lookup added this session should find an installed `iperl`/`iperl.pl`. If it
   is absent the calculator degrades politely and three tests skip — that is by
   design, not a failure.
5. **`tests/acad/gen_conformance.sh`** already falls back from `md5sum` to
   `md5 -q`, so it should run as-is. It falls back to `/tmp` when the NAS share
   is not mounted.
6. `sys/stat.h`, `sys/wait.h`, `unistd.h`, `fork`/`pipe`/`waitpid`,
   `mkdtemp`, `SIGPIPE` — all POSIX and all expected to be fine.

There is **no** Linux-specific API in the core: no inotify, no epoll, no
`/proc` outside that one test path.

### codegraph across platforms

The database is **gitignored** — `.codegraph/.gitignore` ignores everything but
itself — so the Mac will not receive it through git and will want
`codegraph init` regardless.

If the cross-platform read is worth testing deliberately, the `.db` has to be
copied by hand through the NAS share. One encouraging data point measured here:
**no absolute `/home/sadie` paths are interned in the database**, so a copy is
not obviously doomed. Check `codegraph status` reports the expected file count
before trusting it, and re-index if anything looks thin.

## Open, and needing Sadie rather than code

**MINSERT at R2000.** AutoCAD refuses it with `Class separator for class
AcDbMInsertBlock expected`, and has refused **all three** placements of that
separator: before the array fields; before them with every parent field written
out first; and after them at the end of the record. Each attempt was reasoning
rather than evidence and produced none. **What it needs is a MINSERT saved as
R2000 by AutoCAD itself**, to match byte for byte. Then flip `*cf-minsert*` to
`T`. Guessing has cost three rounds; stop guessing.

**A point from LISP is world; the same point typed is UCS.** Deferred, leaning
toward matching AutoLISP — the UI convenience does not extend to the API, and
that split is AutoLISP's own and coherent. Station 9 of the conformance drawing
is the standing test. Note the correction recorded in `SF_todo.md`: `polar`,
`distance` and `angle` are frame-agnostic, so this is a smaller job than the
section originally implied.

**SPLINE group 74.** Omitting the fit points would make the curve exact in every
reader *and* cut about 36% of each record, at the cost of forgetting which points
the user picked. A decision for when SPLINE editing is on the table.

## Traps

Carried forward and still true:

- **`Mat4::from_basis(origin, ax, ay, az)` builds world-TO-basis** — axes in the
  rows.
- **A positive bulge arcs BELOW a left-to-right chord.** `test_polyline.cpp:116`.
- **A test calling `InputValue::of_point()` bypasses UCS conversion** — and so
  does the LISP path, which is the open question above.
- **`ncad_gui` cannot be launched from the Linux box** (X11 over SSH). It may
  well be launchable on the Mac, which would be the first time anyone has seen
  the viewport outside a forwarded session.
- **Commit straight to `main`.** No branches.

New this session:

- **Everything we know about AC1015 is what AutoCAD 2026 demands**, not what the
  format requires. There is no R2000-era reference here. Matching a current
  AutoCAD is right — it is the reader files must satisfy — but it licenses no
  claim of spec conformance, and another reader could be strict in a direction
  we have not seen.
- **`add_one_of_each()` in `tests/test_dxf.cpp` had no polyline**, so every
  structural R2000 test walked past the VERTEX and SEQEND records, which had no
  handles at all. The same blind spot that hid the R12 handle bug, where
  `gen_sample` had no polylines either. It has one now — keep it that way.
- **`load` does not exist** in this AutoLISP. A positional argument to `ncad` is
  a LISP file: `ncad script.lsp -e "(conform)" -i`.
- **`entmake` cannot make MTEXT** despite what `features.md` implies. The
  conformance script writes a DXF fragment and reads it back, first, because
  DXFIN clears the entities while leaving the tables.
- **`LAYER Color <n>` takes its own name list.** `"Color" "1" "Ltype" "DASHED"`
  reads `Ltype` as a layer name and fails one argument later with
  `LAYER: unknown option`, pointing at the wrong place entirely.
