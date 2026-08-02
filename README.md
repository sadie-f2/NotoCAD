# NotoCAD

An open-source, command-line-first CAD tool modeled on **AutoCAD R12** — the
pre-GUI-heavy, pre-solids era. Not a modern AutoCAD clone.

Linux-first. The working platforms at last count are Linux, macOS and 64-bit
Raspberry Pi OS; it builds and passes its tests on all three.

The intended use is not interactive drafting, by command line or by mouse —
though both work, and pointer input answers prompts wherever typing does. It is
driving a CAD kernel as a procedural 3D graphics engine from AutoLISP:
generating geometry from external analysis data, and pulling results back in for
visualisation.

## Status

**0.2.59** — the third component of the version is the implemented command count.
59 commands, and the version says how much of the tool exists rather than how
much of the 1992 manual is covered: every registered command counts, whether or
not R12 had it.

The headless core builds and tests with no GUI and no display; the geometry
kernel (Vec3/Mat4, arbitrary-axis/ECS, 3D transforms including rotation about an
arbitrary axis and mirroring) and the entity database with stable never-reused
handles are underneath everything below.

**Extensions with respect to R12, and to AutoCAD generally** — Ellipse à la R12
was drawn as polyline segments, and spline was not introduced to ACAD until R13.
Ellipse is mathematically accurate in ncad. They are implemented a little
differently from ACAD. AutoCAD will not correctly run a normal, or deferred
tangent osnap to an ellipse, ncad does. ACAD will also not initiate a line from
a spline using deferred tangent osnap, ncad will (it's not well predictable or
stable at first release and may be dropped).

ACAD will also not snap to an out of plane object snap, ncad will if it fits the
geometric criteria.

MEASUREGEOM is lifted from ACAD 2010 as a convenience until DIM can be
implemented.

AutoLISP as implemented is designed to work for most scripts; it is almost
certainly not complete — it is reimplemented from documented behaviour, and there
is no formal specification or conformance suite to test against.

The headless core is consistent with the command line design of AutoCAD, and is
a substantial convenience in the development cycle: most tests of mathematical
functionality can be performed by engineer or assistant without GUI interaction.

**Entities:** LINE, CIRCLE, ARC, POLYLINE (with bulges), TEXT, POINT, SOLID,
3DFACE, INSERT and MINSERT — plus ELLIPSE, SPLINE and MTEXT, which AC1009 cannot
name and which are held exactly in the database and degraded honestly on the way
out. Anything read from DXF with no class here becomes a Proxy that writes its
groups back unchanged, so opening and saving cannot quietly destroy what the
program does not understand.

**Editing:** MOVE, COPY, ROTATE, ROTATE3D, SCALE, MIRROR, ARRAY, STRETCH, TRIM,
EXTEND, BREAK, EXPLODE, PEDIT. Selection sets with Window, Crossing, Last,
Previous, All, Add and Remove. Unlimited UNDO and REDO, back to the start of the
session.

**Drawing structure:** layers, linetypes, blocks and WBLOCK, UCS with a saveable
named set, LIMITS, and a system variable table reachable from AutoLISP through
`getvar` / `setvar`.

**File I/O:** DXF read and write. `SETVAR DXFVERSION` selects R12 (AC1009) or
R2000 (AC1015); R12 is the interchange guarantee, R2000 is the first version that
can name what the database already holds. `APPLOAD` loads a `.lsp` file into a
running session -- the same path as `ncad file.lsp` at startup and `(load ...)`
from within a script, so a function defined by one is reachable by the others.

**Object snaps** are complete at the kernel level: the static ones
(END/MID/CEN/QUA) on the entity vtable, the derived ones (NEA/PER/TAN/INT) as
free functions over the kernel, headless and tested.

**AutoLISP:** arena, reader, evaluator, special forms, the builtin function
table, the entity-access functions (`entmake`, `entget`, `entmod`, `entdel`,
`entlast`, `entnext`), `load`, and the selection-set family (`ssget`, `ssadd`,
`ssdel`, `sslength`, `ssname`, `ssmemb`). Scoping is dynamic, as R12's is.

**The Qt6 shell**, `ncad_gui`: a wireframe viewport with pan, zoom and orbit, a
command line driving the same engine and interpreter as `ncad`, entity picking,
box and crossing selection, and osnap cursor tracking with marker glyphs.
Commands are resumable state machines, driven interchangeably by script text, by
a mouse click, or by AutoLISP `(command ...)` — a command can be started by one
`(command ...)` call and finished by a later one, with arbitrary LISP in between.

**TEXT is drawn with a bundled Hershey stroke font.** R12's `romans` and `romand`
descend from the Hershey set, so this is the same lineage rather than an
approximation of it, and screen text and DXF text come from one source with no Qt
font dependency in the core.

Geometry can be generated procedurally from LISP and written to DXF today.

R12 / AC1009 DXF is probably correct (free software / no warranty). AC1015 (ACAD
2000) DXF is a best effort that can probably be improved. Test cases of all known
entities import; things have almost certainly been missed.

**What has actually been verified in AutoCAD 2026**, stated precisely rather
than generally, because the general version of this sentence hid a real defect
for weeks:

- Entities on arbitrary tilted planes, from the sample drawing, open correctly.
- A full round trip: an AutoCAD-written R12 DXF opened in NotoCAD, saved out,
  and reopened in AutoCAD — 19 block definitions, 1,643 nested INSERTs, 4
  polylines and 1,408 vertices preserved exactly.
- **R2000 output** (`SETVAR DXFVERSION R2000`): `tests/acad/r2000_conformance.lsp`
  — 43 entities, one of every type the program can make — opens in AutoCAD 2026
  and lists clean. MTEXT, both ELLIPSEs and both SPLINEs arrive as themselves;
  the 3D spline reports Non-Planar, the bulge lands on its segment's starting
  vertex with the right centre and radius, the SOLID's bowtie corner order
  survives, and all three TEXT justifications come back as start/center/end
  point. It took six rejections to get there, every one of them structure rather
  than geometry.
- **The arbitrary axis algorithm agrees with AutoCAD's to four decimals.** A
  circle with extrusion (0, 0.4472, 0.8944) puts ECS (98, 26) at world
  (-98, -23.2544, 11.6272); AutoCAD lists -98.0000, -23.2551, 11.6276. That is an
  independent check on the piece `CLAUDE.md` calls foundational.

Testing so far is a combination of engineered tests and "does it work imported
into AutoCAD (2026) / related design tools". Geometric accuracy has been checked
with a variety of ad-hoc examples, and some designed examples.

**Not yet:** interactive grip dragging, fence selection, reference angle on ROTATE
and reference length on SCALE, the interactive AutoLISP input functions (`entsel`,
`getpoint`, `getdist`, `getangle`, `getstring`), PFACE meshes and the surface
entities (RULESURF, TABSURF, REVSURF, EDGESURF), and dimensioning.

## Using it

`ncad` is an R12-style command prompt holding one drawing: a command prompt
that evaluates AutoLISP, not a LISP prompt that calls commands.

```console
$ ncad
ncad 0.2.59 -- type ? for commands, ( for AutoLISP, QUIT to exit.
Command: LINE
Specify first point: 0,0
Specify next point: 100,0
Specify next point or [Undo]: 100,50
Specify next point or [Close/Undo]: C
Command: (setq r 25.0)
25.0
Command: CIRCLE
Specify center point for circle: 50,25
Specify radius of circle: !r
Command: DXFOUT
Enter file name: out
out.dxf written
```

A line starting with `(` is AutoLISP, and an expression also answers a prompt:
`Specify radius of circle: (* 25.4 2)` works, as does `!r` for a variable —
which at the command prompt prints it instead. The one exception is a file name
prompt, which takes the line verbatim, since parens are ordinary in paths; a
name containing spaces has to go on its own line.

A space acts as Enter, so `CIRCLE 50,25 20` is one line. Points may be written
`10,20` absolute, `@5,0` relative to the last point, `@30<45` polar in degrees,
or `@` for the last point itself. Any unambiguous abbreviation of a command
works. `?` lists commands, `CANCEL` aborts one, and Enter at the command prompt
repeats the last.

`ncad --lisp` gives a plain AutoLISP REPL instead, which is the better shape for
piping a generated script in.

It also runs non-interactively, which is the point — geometry gets generated,
not drawn:

```sh
ncad wheel.lsp -e '(dxfout "wheel.dxf")'   # load files, then evaluate
ncad < script.lsp                          # or pipe it
ncad -e '(princ (* 6 7))'                  # one-off expression
```

Forms may span lines; the reader decides when one is complete, so a `)` inside
a string or comment does not confuse it. An error returns you to the prompt with
the interpreter intact, and makes the process exit non-zero in batch use.

There is no line editing yet: GNU readline is GPLv3, and linking it would put
the default build under the obligation the optional DWG module exists to avoid.
libedit (BSD) can go behind a build option later.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
ctest --test-dir build          # unit suite plus CLI smoke tests
./build/src/app/ncad            # the application
```

Options: `NCAD_BUILD_GUI` (off), `NCAD_WITH_DWG` (off), `NCAD_BUILD_TESTS` (on).

`-DNCAD_BUILD_GUI=ON` adds `./build/src/gui/ncad_gui`, the Qt shell: the same
drawing, engine and interpreter as `ncad`, with a viewport. Middle-drag pans,
shift+middle orbits, the wheel zooms about the cursor, Home is extents and
Ctrl+Home is plan. Typing anywhere goes to the command line, and a left click
answers a point prompt. Rendering the same database two ways is a correctness
check worth having — the two disagreeing is a real signal.

**Platforms.** Linux is primary. macOS builds and passes with the same commands.
64-bit Raspberry Pi OS works too — on a Pi 5 (8 GB) a full build takes about four
minutes and the Qt shell runs over X11. On Trixie, which defaults to Wayland,
install `qt6-wayland` alongside `qt6-base-dev` or Qt falls back to xcb with a
missing-plugin warning. A Pi 3B with 1 GB is the practical floor: usable for
drawing, but large files are slow. Constrain the build there (`-j2`) — the
interpreter's translation units are the memory-hungry ones, and an OOM kill looks
like a mysterious compiler crash.

Sanitizer build, worth running after any arena or interpreter work:

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan && ./build-asan/tests/ncad_tests
```

`./build/tools/ncad_gen_sample out.dxf` emits a drawing containing entities on
tilted planes. Opening it in another CAD application is the real correctness
gate for the DXF and ECS code — headless tests only prove self-consistency.

## File formats

**DXF is the native format.** R12 DXF read/write lives in-tree: the format is
small, text, and fully documented.

**DWG is optional and import-only**, via [LibreDWG][libredwg], enabled with
`-DNCAD_WITH_DWG=ON`. It is off by default. DWG export is not planned, and
LibreDWG types do not appear in core headers — conversion happens at the
boundary through LibreDWG's own structs.

[libredwg]: https://www.gnu.org/software/libredwg/

## Licensing

NotoCAD is licensed under the **BSD 3-Clause License** — see [LICENSE](LICENSE).
The license applies to all content in this repository, including documentation.

One exception applies to *binaries*, not to the source:

> LibreDWG is licensed under the GPLv3. A binary built with `-DNCAD_WITH_DWG=ON`
> links it, and so that binary must be conveyed under the GPLv3. This does not
> affect the NotoCAD source, which remains BSD-3-Clause, and it does not affect
> the default build, which links no GPL code.

If you redistribute a DWG-enabled build, you are distributing a GPLv3 work.
The default build carries no such obligation.

This structure is deliberate: keeping DWG behind a compile-time option is what
lets the core stay permissively licensed. See `CLAUDE.md` for the reasoning
behind this and the other foundational decisions.

## Design notes

`CLAUDE.md` is the durable record of the decisions that shaped the codebase and
why — the language dialect and its restraint rules, why the arbitrary axis
algorithm is foundational rather than a feature, why commands must be resumable
state machines, and why the AutoLISP implementation is written from scratch
rather than derived from XLISP.

## Trademarks

AutoCAD, AutoLISP and Autodesk are trademarks of Autodesk, Inc. NotoCAD is an
independent project and is not affiliated with, endorsed by, or sponsored by
Autodesk.

Those names are used throughout this documentation only to identify the software,
file formats and language dialect NotoCAD is compatible with. There is no other
way to say which DXF version it writes or which LISP dialect its interpreter
implements. Nothing here is derived from Autodesk source: the AutoLISP dialect is
reimplemented from its documented semantics, and the R12 DXF reader and writer
work from the published format.
