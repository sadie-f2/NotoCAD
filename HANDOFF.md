# Handoff — session ending 2026-08-14

Session-scoped and disposable. `CLAUDE.md` holds settled rules, `SF_todo.md` the
roadmap and the reasoning, `features.md` capabilities not yet built, and
`SF_strategy.md` the long horizon. **This file is only the bridge.** Delete or
rewrite it rather than letting it rot.

## Where things stand

**0.2.72**, 1112 tests, sanitizer clean, `main` level with `origin/main`.

This session added: the geometry clipboard (COPYCLIP/CUTCLIP/PASTECLIP as DXF
text), OFFSET/FILLET/CHAMFER, toolbars with drawn-not-shipped icons, native file
dialogs behind FILEDIA, a macOS `.app` bundle pipeline, drawings openable from
the command line, sticky toolbar placement — and **dimensioning**: linear,
aligned, radius, diameter and angular, with R12's `DIM` mode over the same
command objects.

A `build-asan-gui` exists now; the Qt shell had never been sanitized before.

Three tasks below, in the order Sadie gave them. The first is explicitly **not**
first priority.

---

## 1. A macOS 13 machine refused the .app  (low priority)

A build handed to a friend about a week ago reports the app is incompatible with
their OS. They are on **macOS 13.x**. We believe the toolchain targets 12.

**Most likely answer, and check it before anything else: the build predates the
fix.** `b6d981c` (2026-08-10) is what moved bundling onto the official Qt 6.8.3
LTS and set `CMAKE_OSX_DEPLOYMENT_TARGET=12.0`. Before it, bundles were built
against **Homebrew Qt 6.11**, whose bottles are compiled per-OS-release for the
machine that pours them — so a bundle made on this Sequoia machine carried
`minos 15.x` in every Qt framework and refuses to launch on anything older. That
is exactly the failure reported, and a build "about a week ago" is very plausibly
on the wrong side of that date.

**Diagnose in this order.** Get the actual bundle they were sent, not a fresh one:

```sh
otool -l NotoCAD.app/Contents/MacOS/NotoCAD | grep -A3 LC_BUILD_VERSION   # want minos 12.0
lipo -archs NotoCAD.app/Contents/MacOS/NotoCAD                            # see below
find NotoCAD.app/Contents/Frameworks -type f -perm +111 -exec sh -c \
  'printf "%s " "$1"; otool -l "$1" | awk "/^ *minos/{print \$2; exit}"' _ {} \;
```

`scripts/mac_bundle.sh` now fails the build if any Mach-O in the bundle demands
newer than the target, so a bundle produced by the current script cannot have
this problem. A bundle produced before it easily could.

**The other candidate, and it is a real one: we build arm64 only.** Nothing sets
`CMAKE_OSX_ARCHITECTURES`, so the binary matches whatever machine built it. If
the friend's machine is an **Intel** Mac, macOS 13 is entirely plausible on it
and the app will be refused no matter what the deployment target says. `lipo
-archs` settles this in one line. The fix is one line too — build universal:

```cmake
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
```

The official Qt frameworks in `~/Qt` are already universal, so only our own code
needs the flag. Worth doing regardless, since "runs only on Apple Silicon" is not
a property anyone will guess from a dmg.

**Do not conclude the target is broken until the actual artifact is inspected.**
A fresh bundle from today's script should be verifiably 12.0, and if it is, the
answer is simply that the friend has an old copy.

---

## 2. Read and respect AutoCAD lock files

In preparation for writing our own. Sadie's view: locks are worth having now.

**What AutoCAD does.** With `plan.dwg` open it creates `plan.dwl` and
`plan.dwl2` beside it. `.dwl` is plain text holding the user name; `.dwl2`
carries more — user, machine, and when it was opened. They sit next to the
drawing, not in a system directory, which is what makes them work over a shared
network folder and also what makes them stale when a process dies. They are
**advisory**: nothing in the OS enforces them, and AutoCAD itself will let you
past with a warning.

**Read side first**, which is this task. On `OPEN` and on `ncad file.dxf`, look
for a sibling lock, and if one is there say who holds it and when. The question
Sadie should decide before this is written:

- **Warn and continue** — closest to AutoCAD, and honest about an advisory lock
  being advisory.
- **Warn and open read-only** — safer, but the drawing then needs a read-only
  mode, which does not exist and is a much larger change (every command that
  mutates has to be refused, and SAVE has to know).
- **Warn and refuse** — simplest to build and the most annoying of the three.

Recommend the first for the read side, with the answer recorded, because it
needs no new concept anywhere in the database.

**Write side after.** Creating our own means deciding what a stale lock is and
who may clear one — a process that segfaults leaves its lock behind, and this
program has segfaulted twice this month. A pid plus a hostname in the file lets
a later session recognise its own dead lock; it does not help across machines,
which is exactly where shared folders live.

**Where it goes.** `src/core/paths.hpp` already owns filename handling and is
where both front ends agree about what a name means; the lock path belongs
beside `ensure_extension` and `same_file`. The commands that would consult it are
`DxfInCommand` (OPEN and DXFIN) and `SaveCommand`, all in `commands.cpp`. Note
that `ncad`'s startup path opens through the OPEN command deliberately, so
wiring it into the command covers both front ends and the command line at once.

---

## 3. DIM needs a LEAder

Confirmed absent. `DIM`'s keywords today are Horizontal, Vertical, Aligned,
Rotated, ANgular, Radius, Diameter, Undo, eXit — see `commands.cpp` around the
`DimCommand::ask_option` hub.

**The design question to settle first: a leader is not a dimension.** It
measures nothing. Everything in `Dimension` is built around `measurement()`
being the truth the entity holds and the label being derived from it, and a
leader has no such number — it has a note somebody typed. Three ways to place it:

- **A `DimKind::Leader`** whose measurement is always zero and whose label is
  always the override. Cheapest, reuses the whole pipeline including the
  writer-synthesised block — but it makes `measurement()` lie for one kind, and
  every switch over `DimKind` grows a case that does nothing.
- **A separate `Leader` entity.** Honest, and the vtable work is now well
  understood — see the impact list in SF_todo and `git show 644fa79`. More code,
  and a second thing that generates arrowheads.
- **Plain geometry at creation**, i.e. lines plus text and no entity. R12 is
  closer to this than to either of the above, and it is what `EXPLODE` produces
  anyway. Loses the ability to edit the note afterwards.

Recommend the second if leaders are going to carry annotation the drawing cares
about, the third if they are throwaway callouts. Sadie should pick.

**R12's prompt sequence**, whichever is chosen: `Leader start` (the arrow end),
then `To point` repeatedly until Enter, then `Dimension text <measurement>`.
The last segment is conventionally horizontal — a shoulder — and the text sits
at its far end. DXF R13+ has a real `LEADER` entity; at R12 it degrades to the
block-and-lines form the dimensions already use, which is a path that now exists.

---

## Also open, carried from SF_todo

- **`%%D` and `%%C` render literally.** Correct DXF — AutoCAD draws ° and ⌀ —
  but our ASCII Hershey font has no such glyphs, so every angular dimension
  reads `90.0000%%D` on screen. One fix in the font layer for TEXT generally.
  More visible now that angular exists.
- **Radial leaders run centre-to-rim**, where AutoCAD's come in from outside the
  curve. A different shape, not a wrong number. Found in the AutoCAD comparison.
- **TRIM and EXTEND cannot be driven from LISP** — they need a pick point the
  terminal cannot supply. FILLET and CHAMFER work around it with a midpoint
  stand-in; those two have no fallback.
- **`c:` functions are not dispatched as commands.**
- **`?` listings only print when the command exits**, because `Step::ask`
  cannot carry output.
- **The SizeAllCursor crash.** Two crashes, both on that one cursor shape,
  mitigated by not asking for it. Untested on Linux, where a crash would mean
  the cursor correlation was coincidence and the fault is ours.
- **Baseline and continue dimensions**, cheap now the families exist.

## Traps worth knowing

- **`DimKind`'s values are DXF's and are not contiguous** (Angular is 5). A
  table indexed by the enum is read off its end — that shipped once already.
- **`entity_subrs.cpp`'s `entget` converter has a `default:`**, so a new entity
  type compiles and silently returns nothing from LISP. It is the site missed
  for ELLIPSE and the one to check first when adding a type.
- **`sysvar.cpp`'s static_assert catches a wrong COUNT, not a wrong ORDER.**
  The enum and the table are matched by position.
- **The AutoCAD comparison is worth more than any test we write.** It found the
  duplicated `*Model_Space`, and it found dimension text drawn aligned when R12
  draws it horizontal. Diff against the SOURCE though, not against AutoCAD's
  output — the pwm boards showed AutoCAD *adding* a stray zero-length line and
  an orphaned block that we correctly did not.
