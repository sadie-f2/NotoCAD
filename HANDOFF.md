# Handoff — session ending 2026-08-15

Session-scoped and disposable. `CLAUDE.md` holds settled rules, `SF_todo.md` the
roadmap and the reasoning, `features.md` capabilities not yet built, and
`SF_strategy.md` the long horizon. **This file is only the bridge.** Delete or
rewrite it rather than letting it rot.

## Where things stand

**0.2.73**, 1162 tests, sanitizer clean, GUI builds, `main` ahead of `origin/main`
by three commits.

Sadie picked three items and their order, then left it running unattended. All
three are done and committed separately:

1. **`%%` control codes** — `%%D` drew three characters instead of a degree sign.
2. **LEADER**, on R13's design, which was her call between the three shapes.
3. **Advisory `.dwl` locks**, read side and a warning before writing.

## Decisions taken while she was asleep

Three of these are judgement calls she may want to look at. They are flagged in
`SF_todo.md` where each item lives; this is the short list.

- **QSAVE now asks when the target is locked.** That is a deliberate exception to
  "QSAVE is the one that must not interrupt". The reasoning is that the rule is
  about *routine* questions and a lock is somebody else's unsaved work about to
  go — and that it cannot fire twice for the same reason it fired once, since the
  lock has to appear *between* two saves. **One line to reverse.**
- **LEADER's annotation is owned, not referenced by handle.** R13 binds it with a
  hard pointer (group 340) to a separate database entity. Nothing in this program
  holds a handle to another entity, and adding that brings dangling references,
  clipboard remapping and erase ordering with it. Ownership buys the point of
  R13's split — the note is a real `Text` or `MText` — without the machinery. The
  cost is that the note cannot be selected on its own.
- **LEADER degrades to line work at BOTH DXF versions**, not just R12. R2000 does
  have a LEADER record, but writing one means a hard pointer to an annotation
  record that has to exist and be read back as one — and our reader does not know
  LEADER. Writing it would open a round trip we cannot close, and a file we can
  only half read is worse than one that degrades honestly.

## What is worth doing next, in the order I would do it

**1. The real R2000 LEADER record, with a reader for it.** One piece of work and
not two — that is the whole reason it was deferred rather than half-built. It
closes the round trip the degrade currently leaves open, and it is the only thing
standing between the R13 entity design and its payoff in interchange.

**2. Baseline and continue dimensions.** Still cheap, still queued, and now the
last easy thing in the dimension family.

**3. Something that can edit a note.** There is no DDEDIT or CHANGE for a `Text`
either, so this is not leader-specific — but the entire argument for R13's shape
was that the annotation is a real entity rather than baked line work, and nothing
yet takes advantage of that. The capability the design bought is not spent.

**4. `?` listings still only print when the command exits.** Unchanged, and still
an API gap rather than a GUI bug: `Step::ask` cannot carry output. Two options are
written up in `SF_todo.md`.

## Still open, carried forward

- **The macOS 13 refusal is PARKED, at Sadie's direction**, pending the friend's
  machine architecture. Her read is that it is probably an Intel Mac, which `lipo
  -archs` on the bundle they were actually sent settles in one line. Do not touch
  the deployment target until that data arrives. The universal-build line is worth
  doing regardless once it does.
- **TRIM and EXTEND cannot be driven from LISP** — they need a pick point the
  terminal cannot supply. FILLET and CHAMFER work around it with a midpoint stand-in;
  those two have no fallback. LEADER, for what it is worth, *is* LISP-drivable and
  was checked.
- **`c:` functions are not dispatched as commands.**
- **The SizeAllCursor crash.** Untested on Linux, where a crash would mean the
  cursor correlation was coincidence and the fault is ours.
- **DXFOUT leaves the drawing dirty**, so quitting still asks.
- **Writing our own lock files.** Deliberately not started: it needs Sadie to say
  what a stale lock is and who may clear one, and this program has segfaulted
  twice this month. We never clear anybody else's today — the message names the
  file so the user can delete it once they know the session is gone.

## Traps worth knowing

Unchanged from last time except where noted.

- **`DimKind`'s values are DXF's and are not contiguous** (Angular is 5). A table
  indexed by the enum is read off its end. `measurement()`'s switch now names
  Angular explicitly so a new kind produces a warning rather than a silent zero.
- **`entity_subrs.cpp`'s `entget` converter has a `default:`**, so a new entity
  type compiles and silently returns nothing from LISP. It is the site missed for
  ELLIPSE. LEADER was added there and there is a test pinning it — copy that test
  when the next type lands.
- **`sysvar.cpp`'s static_assert catches a wrong COUNT, not a wrong ORDER.**
- **`EntityType` has six registration sites**, and only one of them fails loudly.
  For LEADER they were: `entity.hpp`, `entities.cpp`'s name table, the `entget`
  converter, LIST, EXPLODE, and the entity's own file in `src/core/CMakeLists.txt`.
- **BSD `sed` does not support `\b`.** A rename across a file with `sed -i ''
  's/\bfoo(/bar(/g'` silently does nothing and the build then fails somewhere
  unrelated-looking. Use `perl -pi -e` with a lookbehind.
- **The AutoCAD comparison is worth more than any test we write.** Diff against the
  SOURCE though, not against AutoCAD's output.

## Verified by hand this session, not only by tests

- The three new glyphs were rendered as ASCII art through the real font path
  before they were trusted — I drew them blind and would not have caught a
  degree sign at the wrong height any other way.
- Leader geometry likewise: rightward, leftward and already-horizontal cases
  rendered as a scene, which is what confirmed the hook picks its side from the
  path direction and is suppressed when the last segment is already flat.
- `LEADER` driven from the terminal, from `DIM`'s `LEader`, and from
  `(command "LEADER" ...)` including the measurement default.
- The lock warning on OPEN, on SAVE with both answers, on QSAVE, and on DXFOUT,
  against real `.dwl` / `.dwl2` files in a scratch directory.
