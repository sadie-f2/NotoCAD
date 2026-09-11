# The NotoCAD manual

NotoCAD is a command-line-first CAD program modelled on AutoCAD R12. This is
its reference manual, written one chapter at a time.

For the two-page version — enough to draw something today — read
[Using NotoCAD](../using-notocad.md) instead. It stays the front door; this is
where the detail lives.

**Status: alpha, and so is this manual.** Chapters land one per brief. A stub
below is an honest stub: its scope is named so the shape of the whole is
visible, and nothing in it is claimed to be true yet.

| # | Chapter | State |
|---|---------|-------|
| 1 | [Getting started](01-starting.md) — starting it, the command line, coordinates, snaps, undo, `SETVAR` | **written** |
| 2 | [Drafting](02-drafting.md) — `LINE`, `PLINE`, `CIRCLE`, `ARC`, `ELLIPSE`, `SPLINE`, `POINT`, `SOLID`, `3DFACE` | **written** |
| 3 | [Editing and construction](03-editing.md) — selecting and changing; trim, extend, fillet, the clipboard | stub |
| 4 | [Viewing and display](04-viewing.md) — zoom, pan, viewpoints, the UCS; the inquiry commands | stub |
| 5 | [Layers, blocks and properties](05-organising.md) — organisation and reusable geometry | stub |
| 6 | [Dimensions and text](06-annotation.md) — annotation and the dimension family | stub |
| 7 | [Files, plotting and interchange](07-files.md) — DXF in and out, plotting, what survives | stub |
| 8 | [Scripting and customisation](08-scripting.md) — AutoLISP; points at `programming.md` | stub |

## How this manual is kept honest

Two rules, and both are enforced by tests rather than by good intentions.

**Every transcript is executed.** Nothing in a written chapter is recalled or
reconstructed from what a command ought to print. Where a program's real output
contradicts what a design document claims, the chapter documents the behaviour
that *is* and the contradiction is filed as a note.

One typographic liberty, and only one: the selection prompt is long
(`Select objects (2 found) or [Window/Crossing/Last/Previous/ALL/Remove/Add]:`)
and is shown as `[...]` after its first appearance in a chapter, so transcripts
fit a page. Nothing else is shortened, reflowed or tidied.

**Coverage is asserted, not hoped.** `tests/test_manual.cpp` walks the command
registry and requires every registered command name to appear in a written
chapter. It is a ratchet: the number of undocumented commands is pinned at a
hard figure that can only be lowered, so a chapter cannot quietly stop covering
a command it used to cover, and the gap cannot drift upward while nobody is
looking. Stub chapters are excluded by a marker comment — otherwise the scope
notes above would read as coverage and the manual would measure as finished on
the day it was empty.

The same shape as the version-is-the-command-count convention: `0.2.74` means
the registry holds 74 commands, and a test says so.
