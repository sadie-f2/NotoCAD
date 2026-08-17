# NotoCAD — programming the drawing

Three ideas Sadie put together in one breath, and the reason they belong in one
file is that they look like one idea and are not. Ordered as she gave them:

1. **Any language as a scripting engine.** Perl, Python, whatever -- the same
   interface to the backend that AutoLISP has.
2. **Expressions as prompt answers.** `LINE` then `(* 5 2),(* pi 4)` as the first
   point; `=5*2, =$pi * 4` the same way.
3. **A parametric engine.** The drawing database holds variables, each a constant
   or an expression.

**What this file is not.** `SF_todo.md` is the roadmap and the defects.
`features.md` is what the program could do from the outside. `SF_strategy.md` is
the long horizon. This one is about the *seam between a program and a drawing* --
who computes what, and when -- because all three ideas above are really about
that one question, and answering it differently for each is how they end up
three incompatible mechanisms.

Nothing here is scheduled. **And it may not be built here at all** -- Sadie's
note when this file was written is that the direction is solid but might be
developed in a separate repository. Worth recording, because it changes what the
answers below have to be: a scripting protocol and a parametric layer that live
outside this tree must be built against a stable interface rather than reaching
into the database, and that is a constraint on the design and not a packaging
detail.

---

## Where this already stands, measured

More of (2) exists than anybody remembered. Checked at the terminal, not assumed:

| form | result |
|---|---|
| `CIRCLE 0,0 (* 5 2)` | works -- radius 10.0000 |
| `LINE (list 10 20)` | works -- a whole point from an expression |
| `CIRCLE 0,0 =2*$pi*5` | works -- `iperl.hpp`, the `=` prefix |
| `LINE (* 5 2),(* 4 3)` | **fails** -- "a point is required" |

So the gap is not "expressions at prompts". It is **expressions below the
comma**: an expression may be a whole answer but not a component of a
coordinate. `=` has the same shape of limit for a different reason, recorded in
`iperl.hpp` as a known one -- an answer line is tokenised on whitespace, since
`CIRCLE 0,0 5` is three answers, so `=$pi * 4` is torn apart before it is seen.

**And the failure DESYNCS the prompt.** After `(* 5 2),(* 4 3)` is refused, the
next line is eaten as a command:

    LINE
    (* 5 2),(* 4 3)      ; a point is required
    50,50                ; Unknown command "50,50"

That is a defect rather than a missing feature, and by this project's own rule
it belongs in `SF_todo.md` rather than here.

**The R12 ancestor is CAL**, AutoCAD's transparent geometry calculator, and
`iperl.hpp` already names it. So (2) is not a divergence at all -- it is an
R12-era capability we have most of.

---

## The line between (2) and (3)

They look continuous. They are not, and this is the distinction the whole file
exists to record:

- **(2) evaluates once and throws the expression away.** `(* 5 2)` becomes
  `10.0` and nothing in the database remembers why it was ten.
- **(3) keeps the expression and re-evaluates it.** The expression IS the
  drawing content; the number is a cached consequence.

That single difference lands on the entity model, on undo, on DXF, and on what
`draw()` may depend on. Note which way it cuts against the grain here: `draw()`
is handed no database, which is exactly why a `Dimension` bakes its style in at
creation. A parametric entity needs the opposite -- it cannot answer what it
looks like without consulting something outside itself.

**All of (2) can be built for real value without touching (3). (3) cannot be
reached by extending (2)** -- it is the other machine, started from scratch.

---

## What AutoCAD actually does, and it is not the sketch above

Sadie's instinct that "this isn't how ACAD does parametric" is correct, and the
difference is structural rather than cosmetic.

AutoCAD's parametrics (2010 and later) is **a constraint solver over geometry**,
not a spreadsheet of expressions. Three layers:

- **Geometric constraints** -- coincident, collinear, concentric, parallel,
  perpendicular, tangent, symmetric, equal, fix. Relationships between entities,
  carrying no numbers at all.
- **Dimensional constraints** -- linear, aligned, radial, angular, each with a
  NAME and an expression: `d1 = 100`, `d2 = d1/2`.
- **The Parameters Manager** (`PARAMETERS`) -- named user variables whose
  expressions may reference each other.

A solver resolves the network whenever anything moves, including deciding what
is under- or over-constrained. AutoCAD licenses D-Cubed DCM from Siemens for it;
that is not a component anyone writes as a side project.

**Idea (3) as sketched is the Parameters Manager without the solver** -- the
minor half. The major half is that dragging a line updates the NUMBER, or
changing the number moves the GEOMETRY, and that needs iterative numerical
solving with degree-of-freedom analysis, not evaluation.

Dynamic blocks are the block-scoped version of the same machinery, and
`CLAUDE.md` already excludes them by name.

---

## The fork, stated so it gets chosen rather than drifted into

**Constraint-based** -- AutoCAD, SolidWorks, Fusion. Draw approximately, apply
constraints, let a solver find the configuration. This is what the long-horizon
ambition in `SF_strategy.md` eventually requires: sketch constraints are how
those tools work, and a solids kernel without them is a modeller nobody can
drive.

**Program- or expression-based** -- OpenSCAD, FreeCAD's spreadsheet, and idea
(3) as sketched. Geometry is the output of evaluating a program. Deterministic,
no solver, easy to reason about and easy to test. It also composes exactly with
what `CLAUDE.md` already names as the project's purpose: driving the tool as a
procedural 3D graphics engine, generating mesh entities from external analysis
data. That workload is already program-generated geometry; this would be the
same idea admitted into the database rather than run through `entmake` from
outside.

Both may be wanted eventually. **The second does not grow into the first**, and
building the second first is not a step toward the first -- which is the trap
worth naming here rather than discovering at the halfway point.

---

## The interchange wall, which is new

Every divergence so far has degraded honestly: ELLIPSE and SPLINE become
polylines, MTEXT becomes a run of TEXT, LEADER becomes line work. Lossy on the
way out, and openable by anything.

**Parametric has no representation in any version NotoCAD writes.** R12 plainly
not. R2000 predates AutoCAD's parametrics by a decade -- constraints live in
`AcDbAssocNetwork` and `AcDbAssoc2dConstraintGroup` objects from AC1024 onward.

So a parametric drawing degrades to dumb geometry, and unlike every other
divergence **there is no honest degrade BACK**: reopening cannot recover what
was thrown away. It is the first feature where the guardrail in `CLAUDE.md` --
"a divergence must not break DXF R12 interchange" -- is satisfied only in the
weakest sense, and that should be a decision taken deliberately.

---

## On (1), an inversion worth keeping

The instinct is that embedding an interpreter is tighter than shelling out to
one. Here it is backwards, for two reasons.

**Licensing**, which `iperl.hpp` already records: iperl is Artistic 2.0 and Perl
itself dual Artistic/GPL, so linking libperl would be a decision and a
dependency. Running a program is not linking. The same argument as the DWG
boundary, and it costs nothing -- no compile-time module, no flag in the build
graph, and the default binary stays BSD-3.

**Resumability**, which matters more. `CLAUDE.md` names the test that matters:
*a command started by one `(command ...)` call, continued by arbitrary script,
and finished by a later call.* A subprocess gets this FOR FREE -- it blocks on a
pipe, which makes it a coroutine by construction. An embedded interpreter
calling into us has to unwind its own stack to suspend, and that is where this
becomes genuinely hard rather than merely fiddly.

Which suggests what "the same interface AutoLISP has" really means. It is not a
binding. It is `entmake` / `entget` / `ssget` / `command` as a **protocol over a
pipe** -- define it once, and any language that can speak it is in, with no ABI,
no GIL, no crash coupling, and no licence question per language.

`iperl` is the precedent, already in the tree, already out-of-process.

---

## What only an AutoCAD licence can answer

Time-limited, and in the order that things cannot be reconstructed afterwards:

1. **Constrain a drawing, then save to DXF and read the file.** What the
   `AcDbAssoc*` objects actually contain is the only thing that decides how any
   of this degrades, and no amount of reasoning substitutes for the bytes.
2. **Over-constrain something deliberately** and watch what it says. Error
   behaviour is the hard part of every solver and the part nobody documents.
3. `CONSTRAINTBAR`, `DELCONSTRAINT`, and what happens to constraints under
   `EXPLODE` and block insertion.
