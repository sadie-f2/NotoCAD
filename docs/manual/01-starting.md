# 1. Getting started

NotoCAD is a command-line-first CAD program. You type commands at a prompt —
or click where a prompt asks for a point — and the drawing changes. There are
no toolbars in the terminal build and very few dialogs anywhere; the command
line is not a compatibility layer over a GUI, it *is* the program.

**Status: alpha.** No warranty, no autosave, no backup file, no crash
recovery. It will not destroy what it does not understand — entities it has no
class for ride through a load and save unchanged — but save often and keep
copies of anything you care about.

This chapter gets you to a prompt and teaches the handful of conventions every
other chapter assumes. If you want the two-page version instead, read
[`../using-notocad.md`](../using-notocad.md).

## Starting it

- **`ncad`** — the terminal program. The full drawing engine and interpreter
  with no display: it can create, edit, open and save drawings, and run
  AutoLISP.
- **`ncad_gui` / NotoCAD.app** — the same engine with a viewport above the
  command line. Everything the terminal accepts, the window accepts.

Both take a drawing on the command line:

```sh
ncad plan.dxf                 # open it, then give me the prompt
ncad_gui plan.dxf             # open it in a window
ncad plan.dxf script.lsp      # open it, then run the script against it
ncad plan.dxf < commands.txt  # or drive it from standard input
ncad -e '(...)'               # evaluate an expression and exit
```

A `.dxf` argument is opened as the drawing; anything else is loaded as
AutoLISP, and drawings are always opened before scripts run so a script has
something to work on.

On macOS, an unsigned or ad-hoc-signed build is refused by Gatekeeper on first
launch when it arrived by download: right-click the app, choose *Open*, and
confirm — once. A build you made locally launches without ceremony.

You arrive at a banner and a prompt:

```
ncad 0.2.74 -- type ? for commands, ( for AutoLISP, QUIT to exit.
Command:
```

## Which build am I running?

`ABOUT` answers it, and the answer is worth knowing before you file a bug:

```
Command: ABOUT
NotoCAD 0.2.74  (409d332)
A command-line-first CAD tool modelled on AutoCAD R12.
Copyright (c) 2026, Sadie Forbes.

Commands: 74
```

It goes on to print the licence and the bundled-font acknowledgements. Two
things in that header matter:

- **The git hash is the build, not the source tree.** A binary built from a
  dirty tree says so (`409d332-dirty`). The GUI prints the same stamp on its
  viewport nameplate, which makes "is this the build I think it is?"
  answerable from a screenshot.
- **The patch number IS the command count.** `0.2.74` means the registry holds
  74 commands. That is a deliberate convention, asserted by a test rather than
  merely documented, so the version says something a version usually does not:
  how much of R12's surface is actually here.

## The command line

Type a command name and press Enter. `?` lists every command, every alias, and
a summary of the point syntax — it is the fastest reference in the program and
it cannot go stale, because it is generated from the registry:

```
Command: ?
Commands: ABOUT APPLOAD ARC AREA ARRAY CIRCLE ELLIPSE NEW SAVE SAVEAS QSAVE
MEASUREGEOM ORTHO OSNAP SETVAR SPLINE COLOR COPY DIST DXFIN DXFOUT ERASE ID
OPEN COPYCLIP CUTCLIP PASTECLIP LIMITS LTSCALE 3DFACE BASE BLOCK BREAK EXPLODE
EXTEND TRIM OFFSET FILLET CHAMFER DIM DIM1 DIMLINEAR DIMALIGNED DIMRADIUS
DIMDIAMETER DIMANGULAR LEADER PLOT UCS UCSICON VPOINT INSERT MINSERT WBLOCK
LAYER LINE LIST LTYPE MIRROR MOVE PAN PEDIT PLAN PLINE POINT REDO ROTATE
ROTATE3D SCALE SOLID STRETCH TEXT UNDO ZOOM QUIT
Aliases: A=ARC AP=APPLOAD EL=ELLIPSE SPL=SPLINE MEA=MEASUREGEOM C=CIRCLE
E=ERASE AA=AREA AR=ARRAY DI=DIST P=PAN Z=ZOOM LA=LAYER LT=LTYPE LI=LIST CP=COPY
L=LINE PL=PLINE PO=POINT PE=PEDIT B=BLOCK BR=BREAK TR=TRIM EX=EXTEND I=INSERT
X=EXPLODE W=WBLOCK O=OFFSET DLI=DIMLINEAR DAL=DIMALIGNED DRA=DIMRADIUS
DDI=DIMDIAMETER LEAD=LEADER DAN=DIMANGULAR F=FILLET CHA=CHAMFER SO=SOLID
DT=TEXT 3F=3DFACE M=MOVE MI=MIRROR RO=ROTATE S=STRETCH SC=SCALE U=UNDO
Any abbreviation works; the shortest match wins.
Points: 10,20  @5,0 (relative)  @30<45 (polar, degrees)  @ (last point)
Anything starting with ( is evaluated as AutoLISP.
!name prints a variable, or answers a prompt with it.
CANCEL aborts a command; Enter repeats the last one.
```

`QUIT` appears in that list and is not in the registry's 74: it ends the
session rather than acting on the drawing, so it is handled beside the prompt
itself. That is why `?` lists 75 names where `ABOUT` counts 74.

Five conventions carry the rest of the manual:

- **Abbreviations resolve automatically.** Any unambiguous prefix works and the
  shortest match wins, so `L` is LINE and `C` is CIRCLE.
- **Enter on an empty line repeats the last command.** This is the fastest way
  to draw five lines or erase in waves — and it is a real trap, because the
  repeat happens before you have decided what to type next. If you meant to
  start CIRCLE and pressed Enter first, `C` is read as the *first point* of
  another LINE and refused as a bad coordinate.
- **`CANCEL` aborts the running command**, and so does Escape at a real
  terminal. Work the command already committed **stays**.
- **Keywords in prompts** are chosen by typing any unambiguous prefix of the
  capitalised part, so `D` answers `[Diameter]`.
- **`(` starts AutoLISP.** Anything opening with a parenthesis is evaluated.

Here is cancelling, with the point that matters — two segments were committed
before the abort, and two segments survive it:

```
Command: LINE
Specify first point: 0,0
Specify next point: 10,0
Specify next point or [Undo]: 10,10
Specify next point or [Close/Undo]: CANCEL
*Cancel*
Command: LIST
Select objects or [Window/Crossing/Last/Previous/ALL/Remove/Add]: ALL
Select objects (2 found) or [...]:
LINE  Handle = 1  Layer: 0
  from  X = 0.0000  Y = 0.0000  Z = 0.0000
  to    X = 10.0000  Y = 0.0000  Z = 0.0000
  Length = 10.0000
LINE  Handle = 2  Layer: 0
  from  X = 10.0000  Y = 0.0000  Z = 0.0000
  to    X = 10.0000  Y = 10.0000  Z = 0.0000
  Length = 10.0000
```

Cancel is not undo. For that, see *Undo and redo* below.

## Telling it where

Typed coordinates are always exact, and in an alpha with no grid snap they are
the precision mechanism you will lean on hardest. Four forms:

| form | means |
|------|-------|
| `10,20` | absolute X,Y in the current UCS (`10,20,5` for X,Y,Z) |
| `@5,0` | relative to the last point |
| `@30<45` | polar: 30 units at 45 degrees |
| `@` | the last point itself |

Absolute, relative and polar in one command, checked afterwards with `LIST`:

```
Command: LINE
Specify first point: 0,0
Specify next point: @10,0
Specify next point or [Undo]: @10<90
Specify next point or [Close/Undo]:
Command: LIST
Select objects or [Window/Crossing/Last/Previous/ALL/Remove/Add]: ALL
Select objects (2 found) or [...]:
LINE  Handle = 1  Layer: 0
  from  X = 0.0000  Y = 0.0000  Z = 0.0000
  to    X = 10.0000  Y = 0.0000  Z = 0.0000
  Length = 10.0000
LINE  Handle = 2  Layer: 0
  from  X = 10.0000  Y = 0.0000  Z = 0.0000
  to    X = 10.0000  Y = 10.0000  Z = 0.0000
  Length = 10.0000
```

`@10<90` went from `10,0` to `10,10`: 10 units at 90 degrees. Angles are in
degrees and measured counter-clockwise from the positive X axis.

## Units

**A drawing has one unit, and it starts with none.** `INSUNITS` says which:
`0` unitless, `1` inches, `2` feet, `4` millimetres, `5` centimetres, `6`
metres — AutoCAD's own codes, so a DXF carries the answer to other programs.

```
Command: SETVAR
Variable name or ?: INSUNITS
New value for INSUNITS <0>: 4
```

A drawing that never says stays unitless, which is what every drawing made
before this existed is, and nothing about it changes.

**Whatever the drawing is in, you may type any unit at any prompt.** A number
on its own means the drawing's unit, exactly as it always has; a number with a
unit after it is converted for you:

| you type | on a millimetre drawing | on an inch drawing |
|----------|------------------------|--------------------|
| `6.35` | 6.35 | 6.35 |
| `1/4"` | 6.35 | 0.25 |
| `0.25in` | 6.35 | 0.25 |
| `6.35mm` | 6.35 | 0.25 |
| `1'` | 304.8 | 12 |

`mm`, `cm`, `m`, `in`, `"`, `ft` and `'` are all understood, coordinates take
one per component (`1/4",10mm` is a point), and fractions work — `1/4`, and
`1-1/4` for the mixed form. Angles never take a unit: `30<45` is a distance
and a bearing, and `30<45mm` is an error.

On a drawing with no unit set, a bare number still works and a unit is
**refused rather than guessed**:

```
Command: LINE
Specify first point: 0,0
Specify next point: 1/4",0
; this drawing has no unit, so "1/4"" has nothing to convert into. Set INSUNITS first (0 unitless, 1 inches, 2 feet, 4 millimetres, 5 centimetres, 6 metres)
```

That refusal is deliberate. There is no answer to "a quarter inch of what",
and a program that guessed millimetres would send an inch part out at
one-twenty-fifth of its size without saying anything.

Note that this is about **measure**, not about how numbers are displayed —
`LIST` reports four decimal places whatever the unit.

## Object snaps

There is no grid snap yet. Object snaps are how you land exactly on geometry
you have already drawn, and they are **on by default** — `END,CEN`, endpoints
and centres. `OSNAP` shows the current set as the prompt's default and takes a
new one:

```
Command: OSNAP
Object snap modes <END,CEN>: END,MID
Command: OSNAP
Object snap modes <END,MID>:
```

Pressing Enter keeps what is there. The modes are `END` (endpoint), `MID`,
`CEN` (centre), `NOD` (node/point), `QUA` (quadrant), `INT` (intersection),
`PER` (perpendicular), `TAN` (tangent) and `NEA` (nearest); the long spellings
(`ENDPOINT`, `MIDPOINT`, `CENTER`…) work too. Anything else is refused by name
rather than ignored:

```
Command: OSNAP
Object snap modes <END,CEN>: ?
; unknown object snap mode: ?
```

Snaps apply when you *pick* a point with the mouse in the GUI. A typed
coordinate is already exact and is never adjusted.

## Ortho

`ORTHO` constrains the next point to horizontal or vertical from the last one.

```
Command: ORTHO
ON/OFF <ON> or [ON/OFF]: OFF
```

## System variables

`SETVAR` reads and writes the session's settings; `?` lists them.

```
Command: SETVAR
Variable name or ?: ORTHOMODE
New value for ORTHOMODE <0>: 1
Command: SETVAR
Variable name or ?: ORTHOMODE
New value for ORTHOMODE <1>:
```

The prompt shows the current value in angle brackets and Enter leaves it
alone — that is how you *read* a variable without changing it. `ORTHOMODE` and
the `ORTHO` command are the same setting reached two ways, which is true of
most of this list: `CLAYER` is the current layer, `CECOLOR` the current colour,
`OSMODE` the object-snap set, `DXFVERSION` the format the next save writes.
`SETVAR ?` prints all of them, and the ones marked `(read only)` — `DWGNAME`,
`UCSORG` and friends — report state rather than set it.

From AutoLISP the same values are `(getvar "ORTHOMODE")` and
`(setvar "ORTHOMODE" 1)`.

## Undo and redo

**One command is one undo step**, however much it did — a paste of five
hundred entities comes back off in one `UNDO`. Both commands name what they
moved, so you can see what happened:

```
Command: LINE
Specify first point: 0,0
Specify next point: 10,5
Specify next point or [Undo]:
Command: UNDO
LINE
Command: LIST
Select objects or [Window/Crossing/Last/Previous/ALL/Remove/Add]: ALL
Select objects or [...]:
Nothing selected
Command: REDO
LINE
```

After the `REDO` the line is back. Note the difference from cancelling: Escape
and `CANCEL` stop a command and keep what it had already committed; `UNDO`
removes a command that finished.

## AutoLISP from the prompt

Anything starting with `(` is evaluated, and the result is printed:

```
Command: (+ 1 2)
3
Command: (setq r 5)
5
Command: (command "CIRCLE" (list 0 0) r)
nil
Command: LIST
Select objects or [Window/Crossing/Last/Previous/ALL/Remove/Add]: ALL
Select objects (1 found) or [...]:
CIRCLE  Handle = 1  Layer: 0
  center  X = 0.0000  Y = 0.0000  Z = 0.0000
  radius 5.0000
  circumference 31.4159
  area 78.5398
```

`(command ...)` returns `nil` — it is called for its effect, and the effect is
a real circle in the drawing. `!name` at any prompt prints a variable or
answers the prompt with it. The interpreter is the subject of
[chapter 8](08-scripting.md) and of `programming.md`.

## Where to go next

- [2. Drafting](02-drafting.md) — the commands that make geometry.
- Chapters 3–8 are stubs today; each is written as its own brief comes round.
