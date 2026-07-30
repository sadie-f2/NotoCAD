# NotoCAD — strategy

Long-horizon direction and the decisions behind it. `SF_todo.md` is the near-term
roadmap and `CLAUDE.md` holds settled rules for how the code is written; this file
holds the choices that shape what the program becomes, with enough reasoning that
they can be revisited on purpose rather than drifted away from.

Nothing here is scheduled. Several items are years out or may never happen. They
are written down so that today's choices are made in view of them.

**Sequencing, settled: ncad ships first.** Solids are a separate project — SNcad —
and are not started until NotoCAD is released. Everything below about solids is
therefore speculative by design, and no decision recorded here may be used to
justify distorting the R12 program to accommodate a kernel it does not yet have.

## The thesis

**A modern geometry kernel behind R12's input discipline.** That combination does
not currently exist. Fusion 360 has the kernel and inherited none of AutoCAD's
command line, precise numeric entry, or object snaps; the tools that kept the
drawing interface stopped at 2D. The gap is not a missing feature in either camp,
it is that nobody joined them.

This also explains why the R12 run-alike is under 30k lines: it does not rewrite
infrastructure. AutoCAD's authors had no Qt and no free B-rep kernel and had to
build both. Using Qt for the shell, LibreDWG for DWG import and OCCT for solids is
the same trade made three times, and it is why the CAD-specific code stays small
enough to hold in one head.

## Decided: OCCT is the solids core

**Open CASCADE Technology, as an optional compile-time module.** Exact B-rep with
NURBS surfaces, exact booleans, real fillets, and STEP/IGES on the side.

Two alternatives were weighed and rejected:

**Writing a B-rep kernel in-tree.** Tolerant topology and robust surface-surface
intersection are the hardest problems in CAD and consumed decades at the companies
that solved them. It would consume this project entirely.

**A faceted polyhedral core** (mesh booleans with exact predicates — Manifold and
similar). Fast, robust, MIT-licensed, and STL falls straight out. Rejected because
the exactness it discards is exactness worth having: no exact curved faces on a
boolean result, and fillets become offsets or primitives, neither of which is good.
Sadie's call, and the deciding argument was that STL export is a solved problem
regardless of what is behind it, so choosing the weaker representation buys nothing
back.

**Representation is CSG-shaped, not a feature tree.** A tree of primitives,
transforms and operations, evaluated — not an ordered history of parametric
features. The reason is the *topological naming problem*: a feature tree stores
references like "the fillet on edge 12 of face 7", but those identities index into
derived geometry, so changing an upstream feature renumbers them and the fillet
silently migrates or regeneration fails. It is inherent to the approach rather than
a quality-of-implementation issue, and it is the source of the gotchas that make
Pro/E-lineage systems tiring to work in. A CSG tree has no persistent names into
derived geometry because the tree *is* the model.

What is given up: parametric design intent — change a dimension and watch the part
update. Editing is done by editing tree nodes. For deliberate geometry built by
someone who knows what they want, that is the right way round.

### Boundary discipline

The same rules as DWG and Qt, for the same reasons, and they are load-bearing:

- **Its own target and its own compile-time option.** The default build links no
  OCCT and remains BSD-3. The linkage is visible in the build graph.
- **`TopoDS_Shape` and every other OCCT type stay out of core headers.** Conversion
  happens at the boundary, as with LibreDWG's structs.
- **That target compiles with RTTI enabled; the core stays `-fno-rtti`.** OCCT has
  its own type system (`Standard_Transient`, `DEFINE_STANDARD_RTTIEXT`) and does not
  need C++ RTTI, but the island keeps the question from ever arising. Verify this
  early — it is a build-graph fact, cheap to establish and awkward to discover late.
- **OCCT throws; the core does not catch exceptions as control flow.** Boolean
  failures arrive as `Standard_Failure`. They are converted to status codes at the
  boundary, per `CLAUDE.md`.
- **Licensing: LGPL-2.1 with an additional linking exception.** More permissive than
  plain LGPL and widely used commercially, but confirm the current text before any
  binary ships. Dynamic linking regardless, as with Qt.

### The failure mode to design around

Coincident and tangent surfaces are where exact B-rep booleans break. A sweep cut
running along a cylinder leaves sliver faces and edges that should not exist, and
the standard workaround — make the cutting body stand proud of the surface it cuts
— is one every B-rep user learns. Sadie hits it often enough in Fusion 360, whose
ACIS-derived kernel is comparatively good at this. **OCCT is weaker here; boolean
robustness is its most-criticised area.** So this is not a risk to note and move
past, it is a known cost of the decision.

Two responses, both worth building rather than discovering:

1. **Fuzzy booleans.** OCCT exposes a tolerance on the operation itself. Where the
   sane default sits is an open question and probably depends on drawing units.
2. **Automate the overshoot.** The stand-proud trick is mechanical, and unlike a
   user drawing in Fusion, the tree knows both operands. Extending a tool body past
   the surface it cuts is something the application can do rather than something the
   operator must remember. This is the kind of divergence `CLAUDE.md` licenses: a
   modern method that is plainly better, with a reason recorded.

## Consequences

**STL is a primary output, not a convenience.** Additive prototyping is a real use,
and the export is ugly-but-solved for that purpose. OCCT owns tessellation
(`BRepMesh`) and STL writing (`StlAPI_Writer`), so this costs little. A separate
triangle sink on the entity vtable is only needed if non-solid entities must also
export — worth deciding when surface meshes exist, not before.

**DXF R12 degrades solids honestly.** AC1009 cannot name a B-rep solid at all, so
solids write as 3DFACE or PFACE tessellation. That satisfies the interchange
guardrail: richer in the database, honest on the way out. Anyone wanting the real
thing gets STEP from OCCT, which is a better answer than a lossy DXF anyway.

**`draw()` is unaffected in shape.** Edges come from exploding the shape; the
existing world-space-polyline sink takes them. The flattening render path, picking
and hit-testing all continue to work against what is drawn.

**`transform(Mat4)` composes into the tree**, exactly as `Insert` already does.

## Open questions

1. **Two exact geometry systems in one database.** NotoCAD's own entities are exact,
   and so are OCCT's solids, but they are different machines. Can a LINE osnap to a
   solid's edge? Does a solid participate in TRIM, BREAK and INTERSECT? `decompose()`
   in `intersect.cpp` already has no case for ELLIPSE or SPLINE; solids widen that
   gap by a lot. **The rule about which representation an osnap or a measurement may
   trust needs writing down before any code.**

2. **Is the CSG tree stored, or the evaluated shape?** Storing the tree keeps the
   model editable and re-evaluable; storing the shape makes load and display cheap
   and matches what DXF and STL want. Probably both, with the shape derived and
   cached — which is `InFlight`'s discipline at a larger scale, and that pattern is
   already proven here.

3. **Where fillets live in a CSG tree — resolved by STEP export, not by solving it.**
   OCCT can fillet a shape, but a fillet is not naturally a CSG node, and applying
   one to an evaluated result reintroduces exactly the naming problem CSG was chosen
   to avoid. Sadie's answer: **write STEP and fillet downstream in SolidWorks.**
   OCCT provides `STEPControl_Writer`, so the escape hatch arrives with the kernel at
   no extra cost. Fillets therefore drop out of scope rather than being designed
   around, and STEP export becomes a first-class requirement rather than a nicety —
   it is what makes the omission acceptable.

4. **What the solids UI is.** R12 had no solids, so there is no fidelity target to
   copy. This is where the project has to design rather than reconstruct.

## Not drivers, despite appearances

**FEA is philosophy, not a requirement.** The workflow that shaped Sadie's taste —
surfaces built in AutoCAD, imported to ADINA, extrude and revolve into brick-element
models — explains why deliberate geometry in the analyst's hands matters, and why a
feature tree that fights you is unacceptable. It is not a pipeline to build. ADINA
may not even be obtainable now and the work is infrequent. Do not let it constrain
the core; hex-meshability in particular is *not* a constraint on solid modelling
here, and tet-brick joining exists if it ever matters.

**Point-cloud import is context, not a feature.** The historical work was
post-processing exported point-cloud data. What that justifies is the AutoLISP
performance design — tagged compact values, interned symbols, arena cons cells,
suppressed regen in batch loops — because bulk `entmake` of tens of thousands of
small entities has to be fast. It does not justify PFACE meshes as the point of the
project.

**`CLAUDE.md` currently overstates this.** It names PFACE meshes as "the mechanism
for pulling external analysis results back in for visualisation" and builds the
AutoLISP hot-path argument on "tens of thousands of faces". The performance
conclusion stands; the stated purpose is an inference that got promoted to a
requirement. That paragraph should be corrected to say bulk `entmake` of many small
entities, with point import as the known case.

## Remote operation

The core is headless and `Renderer` is an abstract polyline sink with no Qt
dependency, which makes the good option available: **send geometry, let the client
assemble it.** Wireframe polylines compress far better than frames of pixels, and
most applications cannot do this at all.

Short of that, **a degradation ladder** — drop preview, drop window and crossing
box drawing, wireframe instead of shaded. All of it belongs in sysvars, which is the
R12-shaped answer. `InFlight` being derived-never-stored means disabling preview is
safe by construction.

**Minimised X11 works.** The engineering fact that matters is that last-hop latency
and local bandwidth dominate, not the size of the internet pipe — measured directly:
remote X is slower on a makerspace LAN over slow wifi than at home behind a smaller
connection with faster wifi. So optimise bytes and round trips per interaction, not
throughput. If pixel streaming is ever wanted, VirtualGL with TurboVNC is the proven
CAD path and Sunshine/Moonlight is the low-latency open-source one.

Note also that today's paint path repaints the entire widget on every mouse move,
antialiased, with no scene cache. The QPixmap cache already noted in `SF_todo.md`
helps locally and remotely.

## macOS — sized, not scheduled

A nice target, not a necessary one. The port is close to free:

- No CMakeLists anywhere has a platform conditional, and `AppleClang` is already in
  the `noto_flags` generator expression.
- The entire POSIX surface is two files: `iperl.cpp` (`fork`/`pipe`/`dup2`/`execlp`/
  `SIGPIPE`) and `main.cpp:54` (`isatty`). All portable. macOS ships perl, and
  `NOTO_IPERL` already overrides the script path.
- Homebrew Qt6 is shared, so the static-Qt guard in `src/gui/CMakeLists.txt` passes.

Two real hazards. **HiDPI is the one that bites silently:** `pick.hpp` measures in
pixels and `PICKBOX`, `APERTURE` and `CURSORSIZE` are pixel quantities, so on a
Retina display they are wrong by a factor of the device pixel ratio without anything
appearing broken. **Input bindings** assume a three-button mouse for middle-drag pan
and shift+middle orbit, which most Macs do not have.

Estimate: half a day including the HiDPI audit, plus a small separate task for
trackpad-friendly bindings.
