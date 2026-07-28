# NotoCAD

An open-source, command-line-first CAD tool modeled on **AutoCAD R12** — the
pre-GUI-heavy, pre-solids era. Not a modern AutoCAD clone.

Linux-first, with a macOS port as a stated goal.

The intended use is not drafting by mouse. It is driving a CAD kernel as a
procedural 3D graphics engine from AutoLISP: generating geometry from external
analysis data, and pulling results back in for visualisation.

## Status

Early. The headless core builds and tests with no GUI and no display.

**Working:** geometry kernel (Vec3/Mat4, arbitrary-axis/ECS, 3D transforms
including rotation about an arbitrary axis and mirroring); LINE, CIRCLE and ARC
with object snaps; an entity database with stable never-reused handles; a DXF
R12 (AC1009) writer; and an AutoLISP interpreter — arena, reader, evaluator,
special forms, the builtin function table, and the entity-access functions
(`entmake`, `entget`, `entmod`, `entdel`, `entlast`, `entnext`).

Commands are resumable state machines — LINE, CIRCLE, ERASE and DXFOUT so far —
driven interchangeably by script text, by a mouse click, or by AutoLISP
`(command ...)`. A command can be started by one `(command ...)` call and
finished by a later one, with arbitrary LISP in between.

Object snaps are complete at the kernel level: the static ones (END/MID/CEN/QUA)
on the entity vtable, the derived ones (NEA/PER/TAN/INT) as free functions over
the kernel, headless and tested.

There is a Qt6 shell, `ncad_gui` — a wireframe viewport with pan, zoom and orbit,
and a command line driving the same engine and interpreter as `ncad`. It is a
viewer you can type at; picking geometry with the cursor is the next phase.

Geometry can be generated procedurally from LISP and written to DXF today. The
emitted DXF has been verified to open correctly in AutoCAD 2026, including
entities on arbitrary tilted planes.

**Not yet:** the remaining R12 entities, a DXF reader, UCS, and in the shell,
entity hit-testing, osnap cursor tracking and grips.

## Using it

`ncad` is an R12-style command prompt holding one drawing: a command prompt
that evaluates AutoLISP, not a LISP prompt that calls commands.

```console
$ ncad
ncad 0.0.1 -- type ? for commands, ( for AutoLISP, QUIT to exit.
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

Options: `NOTO_BUILD_GUI` (off), `NOTO_WITH_DWG` (off), `NOTO_BUILD_TESTS` (on).

`-DNOTO_BUILD_GUI=ON` adds `./build/src/gui/ncad_gui`, the Qt shell: the same
drawing, engine and interpreter as `ncad`, with a viewport. Middle-drag pans,
shift+middle orbits, the wheel zooms about the cursor, Home is extents and
Ctrl+Home is plan. Typing anywhere goes to the command line, and a left click
answers a point prompt. Rendering the same database two ways is a correctness
check worth having — the two disagreeing is a real signal.

Sanitizer build, worth running after any arena or interpreter work:

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan && ./build-asan/tests/noto_tests
```

`./build/tools/noto_gen_sample out.dxf` emits a drawing containing entities on
tilted planes. Opening it in another CAD application is the real correctness
gate for the DXF and ECS code — headless tests only prove self-consistency.

## File formats

**DXF is the native format.** R12 DXF read/write lives in-tree: the format is
small, text, and fully documented.

**DWG is optional and import-only**, via [LibreDWG][libredwg], enabled with
`-DNOTO_WITH_DWG=ON`. It is off by default. DWG export is not planned, and
LibreDWG types do not appear in core headers — conversion happens at the
boundary through LibreDWG's own structs.

[libredwg]: https://www.gnu.org/software/libredwg/

## Licensing

NotoCAD is licensed under the **BSD 3-Clause License** — see [LICENSE](LICENSE).
The license applies to all content in this repository, including documentation.

One exception applies to *binaries*, not to the source:

> LibreDWG is licensed under the GPLv3. A binary built with `-DNOTO_WITH_DWG=ON`
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
