# Using NotoCAD

NotoCAD is a command-line-first CAD program. You type commands at a prompt —
or click where a prompt asks for a point — and the drawing changes. There are
no toolbars, no ribbons, and very few dialogs; the command line is not a
compatibility layer over a GUI, it *is* the program, and the window is a view
of it. This is a deliberate design, and it is what makes every command equally
reachable from the keyboard, from script files, and from AutoLISP.

**Status: alpha.** NotoCAD is provided as-is, with no warranty. It will not
eat a file it did not write — opening and saving preserves entities it does
not model — but you should treat it accordingly: save often, keep copies of
anything you care about, and expect rough edges exactly where this document
marks them.

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
something to work on. A drawing on its own leaves you at the prompt rather
than exiting, which is what makes the heredoc and pipe forms useful.

On macOS, an unsigned or ad-hoc-signed build will be refused by Gatekeeper on
first launch when it arrived by download: right-click the app, choose *Open*,
and confirm — once. A build you made locally launches without ceremony.

## The command line

Type a command name and press Enter. `?` lists every command. What you should
know beyond that:

- **Abbreviations resolve automatically** — any unambiguous prefix works, and
  the shortest match wins. The classic short forms (`L` for LINE, `C` for
  CIRCLE, `E` for ERASE, `Z` for ZOOM…) are all present; `?` lists them.
- **Enter on an empty line repeats the last command.** This is the fastest way
  to draw five lines or erase in waves.
- **Space acts as Enter** while typing a command or an option — but not inside
  a text or file-name answer, where spaces are content.
- **Escape cancels the running command.** Work the command already committed
  stays: a polyline cancelled after three vertices keeps those segments.
- **Keywords in prompts** are chosen by typing any unambiguous prefix of the
  capitalized part shown in the prompt.
- **`(` starts AutoLISP.** Anything opening with a parenthesis is evaluated by
  the interpreter, and multi-line forms continue until the parens balance.
- **`!name`** prints an AutoLISP variable — or answers the standing prompt
  with its value.

## Points and coordinates

A point prompt takes any of:

| Form | Meaning |
|---|---|
| `10,20` or `10,20,5` | absolute, in the current UCS |
| `@5,0` | relative to the last point |
| `@30<45` | polar: distance 30, angle 45°, from the last point |
| `@` | the last point itself |
| *(click)* | in the GUI, a click answers the prompt |

Angles are in degrees, counterclockwise, zero along +X. Coordinates are read
in the current UCS — if you have set one, `10,20` means the UCS's 10,20, not
the world's. `UCS` manages coordinate systems; `PLAN` looks straight down the
current one.

## The window

The viewport shows the drawing as wireframe — lines, arcs, and stroke-font
text, with no shading and no hidden-line removal. A circle seen edge-on is a
line; that is the honest projection, not a bug.

- **Toolbars** line the top, left and right: File and view along the top, Draw
  down the left, Modify down the right. A button does exactly what typing the
  command does — it feeds the name to the same command line — so anything a
  button can do, you can type, and the tooltip tells you the name and its
  abbreviation so you can stop clicking. Clicking a button while a command is
  running cancels it first, as typing a command name would.
- **Middle-drag** pans. **Shift+middle-drag** orbits. **Wheel** zooms about
  the cursor.
- **Home** zooms to extents. **Ctrl+Home** returns to plan view.
- **Typing goes to the command line from anywhere.** There is no "click the
  command line first" step — start typing and the input follows.
- **Cmd +/−** (Ctrl on Linux) resizes the command-line text.
- **Toolbars can be dragged** to any edge. **Right-click anywhere** — the
  drawing area included — for a checklist of them, which is how you hide the
  ones you do not want and how you get them back afterwards. A toolbar
  switched back on returns to the edge it was left at.
- Where the toolbars are, the window size, and the command-line text size are
  remembered between sessions in `~/.config/NotoCAD/ncad_gui.ini`.
  `ncad_gui --reset-ui` forgets them if the arrangement ever goes wrong, and
  `ncad_gui -h` prints that path.
- **Escape** cancels, from either the viewport or the command line.

### Copy and paste

Cmd-C / Cmd-X / Cmd-V (Ctrl- on Linux) each mean two things in a CAD window —
text or geometry — and NotoCAD decides between them like this:

- **Copy / Cut:** a text selection in the command line wins; with none, at an
  idle prompt, the chord runs COPYCLIP / CUTCLIP, which ask you to select
  entities.
- **Paste goes by focus.** Focus in the command line pastes characters into
  the input. Focus anywhere else runs PASTECLIP — so to paste geometry right
  after typing, click the viewport first. Supplying that context is your job,
  by design.

Geometry crosses between NotoCAD windows — and to and from any program that
handles text, because the clipboard payload is an ordinary DXF document. The
same commands work typed, without the shortcuts.

Known caveat: pasting very large selections through a *remote* X11 display
(or the XQuartz pasteboard bridge) can truncate the transfer. Local sessions,
native macOS, and reasonable selections are fine.

## Editing geometry

The editing commands take a selection, or two picks:

- **MOVE, COPY, ROTATE, SCALE, MIRROR, ARRAY, STRETCH, ERASE** all select first,
  then act.
- **TRIM / EXTEND** cut back or lengthen to a boundary; **BREAK** removes a piece
  between two points.
- **OFFSET** makes a parallel copy at a distance, or through a point. It asks the
  distance once and then repeats object-and-side until you press Enter, because
  offsetting a run of curves is the normal use. Corners are **mitred** — the moved
  segments are extended until they meet — so an offset outline stays an outline
  rather than becoming disconnected pieces. It offsets lines, circles, arcs and
  polylines; ellipses and splines are declined rather than approximated, because
  their true offset is a shape neither entity can hold.
- **FILLET / CHAMFER** round or bevel the corner where two lines meet, and both
  will *extend* lines that fall short of each other rather than refusing.
  **FILLET with radius 0 closes a corner exactly**, which is the quickest way to
  fix two lines that overshoot or miss. Set the size with the `R` (or `D`) option;
  it is remembered for next time.

Caveat: TRIM, EXTEND, FILLET and CHAMFER decide what survives from *where you
picked*, so they are at their best in the window. From the terminal, FILLET and
CHAMFER fall back to each line's midpoint (which is right whenever the corner is
at an end); TRIM and EXTEND currently need the GUI.

## Selecting things

Editing commands ask for a selection first — `Select objects:` — and collect
until you press Enter:

- Click entities one at a time (a pick box hangs on the cursor), or type a
  keyword: **Window** (fully inside a box), **Crossing** (touching a box),
  **Last**, **Previous** (the last selection an editing command used),
  **ALL**, **Remove** / **Add** to subtract and re-add.
- Enter with nothing selected simply ends the command; missing everything is
  how you decide you are done.

## Object snaps

`OSNAP` sets the running snap modes (endpoint, midpoint, center, nearest,
perpendicular, tangent, quadrant, intersection…); the `OSMODE` system
variable holds the same state for scripts. While any point prompt stands, a
typed snap name — `end`, `mid`, `cen`, `nea`, and so on — overrides the
running modes for that one pick. Feature snaps always beat NEAREST when both
are in the aperture, so an endpoint does not get buried by the curve it ends.

## Files — read this section

File handling is deliberately simple and still rudimentary. The gotchas are
all of one species: **you are typing paths at a prompt, and nothing browses
for you.**

- **DXF is the native format.** `OPEN` reads a file *as* the drawing; `SAVE`,
  `SAVEAS` and `QSAVE` write it.
- **In the window, file prompts open a file dialog.** OPEN, SAVE, SAVEAS,
  DXFIN, DXFOUT, WBLOCK and APPLOAD all raise one, so you browse instead of
  typing paths. Cancelling the dialog cancels the command.
- **The DXF version is in the save dialog**, as its file-type choice ("R12" or
  "R2000" — the *File Format* popup on macOS). Choosing there answers it, so
  the command line does not ask again; the same goes for replacing an existing
  file, which the dialog already confirmed. At the terminal both are asked as
  ordinary prompts.
- **`FILEDIA` turns that off** — `SETVAR FILEDIA 0`, or from AutoLISP — and
  then file names are typed even in the window. **Do this in any script or
  LISP routine that opens or saves**, or it will stop on a dialog with nobody
  there to answer it. Typing `~` alone at a file prompt raises the dialog for
  that one prompt regardless.
- **At the terminal, paths are typed** — there are no dialogs in `ncad` — and
  relative paths resolve against the directory NotoCAD was started from, which
  for a double-clicked macOS app is not your project directory. Type absolute
  paths until you have a feel for it.
- **`DXFIN` merges; `OPEN` replaces.** DXFIN adds a file's geometry to the
  current drawing without touching what is there — and where both define a
  layer or block of the same name, *the drawing's own definition wins*.
- **Saving asks which DXF version, once.** R12 (maximally interchangeable) or
  R2000 (richer: splines and ellipses stay themselves). The answer becomes
  the session default (`DXFVERSION`); only the first save asks.
- **What NotoCAD does not model, it does not destroy.** Entities it has no
  class for ride through as proxies and write back out unchanged, so opening
  and saving someone else's file does not quietly strip it. Modern files
  generally open fine; a warning is printed when the version is newer than
  NotoCAD claims to read.
- **There is no autosave, no backup file, and no crash recovery.** Alpha.
  Make QSAVE a habit.
- **No DWG.** DXF is the interchange path. (A read-only DWG import exists as
  a compile-time option; the standard build does not include it.)
- **`WBLOCK`** writes a named block — or the whole drawing — to its own DXF
  file. **`APPLOAD`** (or `(load "file.lsp")`) brings AutoLISP into a running
  session.

## What the display is telling you

- **Wireframe means wireframe.** No hidden lines, no faces, no shading. SOLID
  and 3DFACE entities draw as their edges.
- **Text is drawn with a built-in stroke font**, on screen and into DXF alike,
  so what you see is what a plotter-era file format can actually carry.
  Multi-line formatted text arriving from other programs (MTEXT) is held
  exactly and displayed as wrapped plain text; its formatting codes are
  preserved in the file even though the screen does not render them.
- **Tilted geometry projects honestly.** A circle on a tilted plane renders
  as an ellipse from above and edge-on from its own plane. If a shape looks
  "wrong," check your viewpoint before your geometry: `PLAN`, `VPOINT`, or
  an orbit usually explains it.
- **No dimensions yet.** `MEASUREGEOM`, `DIST`, `ID`, `AREA` and `LIST`
  answer the questions dimensions would; annotation entities are on the
  roadmap.
- **No grid snap yet** — object snaps are the precision mechanism. Typed
  coordinates are always exact.

## Undo

One command is one undo step, however much it did — a paste of five hundred
entities comes back off in one `U`. `REDO` reverses an undo. Escape is not
undo: what a cancelled command already committed stays in the drawing.

## AutoLISP

The interpreter speaks the classic dialect: `defun`, the list primitives,
`command`, `entmake` / `entget` / `entmod`, `ssget`, the `getxxx` input
functions, `getvar` / `setvar`, and file I/O (`open`, `read-line`…).
Variables are dynamically scoped, as the dialect has always been. The
intended scale is real: generating meshes with tens of thousands of faces
through `entmake` is a design target, not an abuse.

```lisp
(defun box (/ p)
  (setq p (getpoint "Corner: "))
  (command "PLINE" p (list (+ (car p) 10) (cadr p))
                    (list (+ (car p) 10) (+ (cadr p) 10))
                    (list (car p) (+ (cadr p) 10)) "C"))
```

Call it as `(box)`. Defining `c:box` to make a bare `BOX` typed at the prompt
run it is the classic convention and is not wired up yet — for now, functions
are invoked with parentheses.

## Known limitations, in one place

- Alpha software; no warranty; no autosave.
- File paths are typed, not browsed; relative paths follow the launch
  directory.
- No dimensions, no hatching, no grid snap, no plotting.
- Toolbars cover the common commands; the rest are typed (`?` lists them all).
- FILLET and CHAMFER work on two lines; arcs and circles are a later pass.
- OFFSET declines ellipses and splines rather than approximating them.
- Selection is command-then-select; select-first (grips) is planned.
- Large clipboard transfers over remote X11 can truncate.
- AutoLISP `c:` functions are not yet callable as bare commands; use `(name)`.
- One drawing per window; run a second instance for a second drawing.
