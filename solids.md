# NotoCAD — the solids engine

**Status: a proposal, one conversation old.** `SF_strategy.md` records what was
decided about solids before this file existed -- OCCT as the kernel, SNcad as a
separate project, ncad ships first -- and those decisions stand. What is new
here is the *shape of the engine*, and almost all of it was worked out in a
single sitting. It is written as an argument rather than in `CLAUDE.md`'s
settled voice, deliberately: cheaper to disagree with, and it keeps the
distinction between a decision taken and a decision drifted into.

Each section says which it is.

---

## Plan A and Plan B, and the designer chooses

**Sadie's framing, and it belongs at the top because it changes how everything
below should be read.**

**Plan A is OCCT** -- exact B-rep, NURBS surfaces, real booleans, STEP. It is
the harder thing, which is why the conversation that produced this file stayed
on it.

**Plan B already exists**: parametric solid elements -- primitives described by
their parameters rather than by their boundary, composed into the larger engine.
No topology, no tolerance model, exact by construction, tessellated only for
display. It cannot do arbitrary booleans on arbitrary curved geometry, and for a
great many real parts it does not need to.

The two are not rivals to be settled. What this leaves the designer is **use
what works for the problem at hand** -- which is a stated design principle, not
an absence of one, and it means the engine must not assume every solid is a
B-rep. A parametric prism and an OCCT shape should both be things the database
can hold and the viewport can draw.

Worth noting Plan B is also where `programming.md` points: OpenSCAD is
parametric primitives plus CSG, and the expression-based model composes with it
naturally. Plan A needs the constraint-solver flavour instead.

---

## The boundary: a separate process (proposed)

`SF_strategy.md` describes solids as an optional compile-time module inside
ncad's build graph. **That is superseded here.** The kernel runs in its own
process, and the reasons are specific rather than aesthetic:

- **Booleans can take arbitrarily long, or hang.** In-process that is a frozen
  application; out-of-process it is a cancellable request.
- **OCCT throws.** `Standard_Failure` on boolean failure, in a core that
  deliberately does not use exceptions as control flow.
- **It is already optional.** The compile-time module was reaching for the same
  isolation with a weaker mechanism.
- **It is the one licence boundary.** No OCCT-linked binary need ever be
  conveyed with ncad at all -- a stronger position than the module gave, and the
  same reasoning that keeps LibreDWG at arm's length.

And the boundary is genuinely thin, which is the test the viewport fails: it
carries profiles in, tessellation and answers out. Compare the render path,
which was measured at 2.2 million points a frame before culling -- that boundary
is the whole scene, which is why the viewport stays in-process and this does not.

The isolation is worth more than it sounds. A linked kernel that segfaults takes
the drawing with it.

---

## The rule that keeps two representations honest (proposed)

ncad holds tessellation. The kernel holds truth. `SF_strategy`'s open question 1
says the rule about which one a measurement may trust **must be written down
before any code**. Proposed rule, in one sentence:

> **Tessellation is never authoritative for anything numeric.** It is for
> display and broad-phase picking only. Every number that reaches the user -- a
> snap point, a distance, a dimension -- comes from the exact model.

The obvious objection is chattiness: osnap tracking runs on mouse-move, and a
round trip per frame is dead. **The answer is to transfer the exact DESCRIPTION
of a feature, not the answer to each query.**

When the broad phase lands on an edge, ncad asks once and is told *this edge is
a circle, centre C, radius r, axis A* -- or a NURBS description where it is not
analytic. ncad then computes END, MID, CEN, QUA and NEA against that exact
description locally, at frame rate, until the candidate feature changes.

So ncad holds exact geometry for the handful of features under the cursor and
triangles for everything else, and the two representations stop competing
because only one of them is ever allowed to produce a number.

---

## Picking, in two phases (proposed, and mostly already built)

`pick.hpp` measures against the flattened wireframe **in pixels**, deliberately,
"so it needs no switch on entity type and picks exactly what is drawn." That
property survives solids untouched, because what is drawn is the tessellation.

- **Broad phase -- pick on the tessellation.** Screen-space, cheap, already
  written, no exact geometry required.
- **Narrow phase -- snap exactly**, against the cached feature description above.

`osnap_search.hpp`'s existing tiering carries over without modification:
vertices and edge centres are discrete, face projections are continuous, and the
rule that discrete beats continuous already knows what to do with that.

---

## What the kernel already answers, so we do not design it

**Established, not proposed.** Three things that look like design problems and
are not:

**Surface versus solid is the kernel's own type system.** OCCT's shapes run
`VERTEX -> EDGE -> WIRE -> FACE -> SHELL -> SOLID -> COMPSOLID -> COMPOUND`, and
a SHELL may be open or closed. Extruding a WIRE yields a SHELL -- a surface.
Extruding a FACE yields a SOLID. Sadie's "declare it a surface and extruding
makes a solid" is exactly the difference between handing the kernel a wire and
handing it a face. Thickening an open shell into a solid is
`BRepOffsetAPI_MakeThickSolid`.

**Seams are not an artifact to design away.** A cylinder's *u* wraps 0 to 2*pi,
and u=0 and u=2*pi are the same line in space, so a bounded face on a closed
surface must include a seam edge. It is longitude and the date line: the
wraparound has to be somewhere. Parasolid, ACIS and every commercial kernel have
them and none show them. `BRep_Tool::IsClosed` flags a seam;
`BRep_Tool::Degenerated` flags a cone apex or sphere pole. **The osnap rule is a
predicate, not a redesign** -- skip seams, skip degenerates, offer the rest.

**Exact means exact.** An OCCT cylinder carries a `Geom_CylindricalSurface` with
an axis and a radius. Not a NURBS approximation, not facets. Asking for its
centre is a lookup, not a fit -- which is the whole reason to pay for an exact
kernel.

The genuinely awkward topology is what **booleans** leave: split faces, slivers,
edges along curves nobody drew. Those carry no flag and are shape-dependent.

---

## Honest note on OCCT, and what the STEP hatch is really for

**Established, and it revises how one earlier decision should be read.**

Fusion uses ASM (an ACIS fork); SolidWorks uses Parasolid. Both are exact NURBS
B-rep, so the razor edges Fusion produces on hard sweeps are not a coarser
representation -- they are a **tolerance strategy**, and a willingness to trim a
self-intersecting sweep rather than refuse it. Permissiveness, not imprecision.

**OCCT is in the same family and is generally reckoned less robust than either.**
Boolean failures on difficult geometry are a standing complaint wherever it is
used. If the goal were "cleaner than Fusion on hard sweeps", no architecture
delivers that.

Which makes `SF_strategy`'s existing answer better than it looked when written:
**keep operations simple, write STEP, finish downstream.** That plays to OCCT's
real strengths -- exact analytic primitives, solid STEP export -- and routes
around its known weakness rather than pretending it away. The hatch is not a
convenience; it is load-bearing.

---

## Checking the kernel's work against our own (proposed)

**Sadie's, and it is the practical answer to the section above** -- if OCCT will
sometimes leave slivers and razor edges without saying so, the useful question is
how we find out.

The idea: a solid's creation also puts geometry in ncad's database, so check
whether the two agree. Whether that is worth anything turns on ONE thing:

- **Tautological if the prediction comes from the kernel.** Ask OCCT for the
  edges, store them, then compare them to OCCT's solid, and you have compared a
  thing to itself.
- **A real check if the prediction comes from ncad's own knowledge.** ncad owns
  the input profile EXACTLY -- ordinary 2D geometry it built. From that plus "the
  operation was an extrude by V", it can predict properties of the result without
  asking anything. Two independent computations then meet at the boundary, and
  disagreement means something.

This project already runs both idioms and knows the difference. `Dimension` shares
one `regenerate()` **so the screen and the file cannot disagree**; rendering the
same database two ways is kept **because the two disagreeing is a real signal**.
This is the second kind, and the process boundary makes it honest rather than
contrived -- profile on one side, shape on the other, no shared code quietly
making them agree.

**What is independently predictable, and cheap:**

- **Euler-Poincare.** Extruding a closed N-segment profile predicts the topology
  outright: N+2 faces, 3N edges, 2N vertices, V - E + F = 2. Compared against
  `TopExp_Explorer` counts. Kernels already use this internally as a validity
  check, so it is proven rather than speculative.
- **Volume, and this is the pretty one.** A straight extrude is profile area times
  height, and ncad already computes area -- `AREA` exists. A revolve is **Pappus's
  centroid theorem**, V = 2*pi*rho*A with rho the centroid's distance from the
  axis: genuinely independent mathematics, computed from the 2D profile alone and
  checked against `BRepGProp::VolumeProperties`. The same theorem's first form
  gives surface area from the profile's perimeter and ITS centroid.
- **Bounding box.** The profile's box swept along the vector. Crude, free, catches
  gross errors.

**What it catches, and this is why it earns its place:** a sweep that
self-intersects and is silently trimmed leaves extra faces and edges; a boolean
that leaves slivers raises the face count; a degenerate input collapses it. None
of those announce themselves -- that is the entire complaint about the failure
mode -- but all of them move the counts off the prediction.

    extrude of a 4-segment profile: expected 6 faces, 12 edges; got 9, 21

Not a diagnosis. But it is the difference between finding out now and finding out
when the part comes off the printer wrong.

**The costs, honestly.** A profile with tangent-continuous arcs may correctly
produce FEWER faces than the naive count, because the kernel merged them -- so
either the predictor models merging or the check is asymmetric, treating extra
faces as suspicious and fewer as possibly fine. A general sweep is NURBS-fitted to
a tolerance, so volume agreement needs a relative epsilon, and choosing it is a
judgement rather than a constant. And it does not extend to arbitrary booleans:
subtracting one general solid from another has no cheap independent prediction,
and writing one would be writing a second modeller.

That last limit is not much of a limit, because the operations that DO admit a
prediction are exactly the ones this engine starts with.

**Shape: a warning, per operation, with the predictor living on ncad's side of the
boundary and never consulting the kernel.** The moment it asks, it stops being a
check.

---

## The part that is not solved: evaluation, and parent-child

**Sadie's, and the sharpest open question here.** One way or another there are
parent-child relationships that want to be right:

- **profile -> solid.** Extrude a wire; change the wire; does the solid follow?
- **solid -> faces**, if a face is ever to be selected, thickened or referenced.
- **solid -> solid** in a feature or CSG tree.

**ncad's database has no mechanism for any of it.** Nothing holds a handle to
another entity -- which is exactly why `Leader` *owns* its annotation instead of
pointing at one, and the reasoning there applies here at one more level of
difficulty.

And there is a tension worth stating plainly: **the boundary that makes solids
easy makes evaluation hard.** The process split is clean precisely because it is
thin -- but a parametric relationship *crosses* it, with the profile living in
ncad and the shape living in the kernel. The isolation and the dependency pull
against each other.

Three shapes, none chosen:

1. **The kernel owns every relationship.** ncad holds an opaque shape id and its
   tessellation, and does not know a solid came from a profile. Cleanest, and
   consistent with the boundary -- but erasing the profile in ncad leaves the
   kernel referencing something gone, and somebody has to reconcile that. It is
   the same shape of problem as two owners of a lock file.
2. **ncad's database grows real references.** Honest, and it drags in dangling
   handles, erase ordering, undo, and clipboard remapping -- the whole list that
   `Leader` was designed to avoid.
3. **Weak reference in ncad, truth in the kernel.** A middle that has to define
   what happens when the two disagree, which is the hard half of both.

**Persistent naming sits underneath all three.** Extrude a circle, reference
"the top face", change the radius, re-evaluate -- is it still the top face? Add
a boolean that splits it, and which half is it? Every vendor has a partial
answer and all of them leak. `SF_strategy` already brushed this deciding fillets
were out of scope. **A face identity that is only ever "the thing just picked,
now" avoids it entirely; the moment one is stored and replayed, we own it.**

---

## Still open

1. **Evaluation and parent-child**, above. The one that gates code.
2. **What the solids UI is.** R12 had no solids, so there is no fidelity target
   to reconstruct. Carried unchanged from `SF_strategy`, and still untouched.
3. **The protocol is named but unspecified.** `programming.md` argues the right
   shape is `entmake`/`entget`/`ssget`/`command` as a protocol over a pipe rather
   than a binding; solids need more than that -- shape ids, tessellation
   transfer, feature-description queries, cancellation.
4. **Tessellation volume across a pipe wants measuring**, not designing around.
   Tens of thousands of faces is the stated workload.
5. **Intent at creation or at operation.** A circle that knows it is a surface,
   versus an extrude that asks. The first is more R12 in spirit -- current layer,
   OSMODE -- and cheaper; the second is what Fusion and SolidWorks do and does
   not require re-declaring to change your mind.
6. **Who owns a shape's placement.** `SF_strategy` has `transform(Mat4)`
   composing into the tree "exactly as `Insert` already does". Across a process
   boundary that needs rethinking.
7. **Whether the cross-check's predictor models face merging**, or whether the
   check is simply asymmetric. The cheap answer is asymmetry; the right answer
   depends on how often tangent-continuous profiles turn up in real use, which
   nobody has measured.

## Superseded in SF_strategy

Marked there rather than deleted, so the reasoning stays legible:

- Solids as an **optional compile-time module** -> a separate process.
- **The RTTI island** (that target with RTTI, the core `-fno-rtti`) -> moot;
  different process, different binary, no shared build constraint.
- **`TopoDS_Shape` out of core headers** -> stronger and free; OCCT is not linked.
- **Open question 1** (which representation a measurement trusts) -> answered
  above.
- **Open question 2** (CSG tree stored, or evaluated shape) -> no longer ncad's
  question. The kernel's private business, and ncad is better for not knowing.
