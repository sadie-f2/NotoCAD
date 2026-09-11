# 2. Drafting

The commands that put geometry into a drawing. Nine of them, and they are R12's
nine: `LINE`, `PLINE`, `CIRCLE`, `ARC`, `ELLIPSE`, `SPLINE`, `POINT`, `SOLID`
and `3DFACE`. If you have AutoCAD in your hands they will feel right; where
they differ, this chapter says so.

Everything here assumes [chapter 1](01-starting.md)'s conventions — typed
coordinate forms, `CANCEL`, Enter to repeat, prefix-matched keywords.

**Status: alpha.** There is no hatching, and no grid snap; object snaps and
typed coordinates are the precision mechanism. Every transcript below was run
against build `0.2.74` and its output is copied from the run.

## LINE — a chain of separate segments

```
Command: LINE
Specify first point: 0,0
Specify next point: 10,0
Specify next point or [Undo]: 10,10
Specify next point or [Close/Undo]:
```

Keep answering with points; Enter ends the chain. `Undo` steps back one vertex
without leaving the command. `Close` appears once there are at least two
segments and joins the last point to the first.

**Each segment is its own LINE entity.** The chain above is two entities, not
one, which is what `LIST` reports and what `ERASE` will act on one at a time.
If you want the chain to be a single object, draw a polyline instead — that is
the whole difference between `LINE` and `PLINE`.

```
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

## PLINE — one entity, many vertices

```
Command: PLINE
From point: 0,0
Endpoint of line or [Arc/Halfwidth/Length/Width]: 10,0
Endpoint of line or [Arc/Halfwidth/Length/Width/Undo]: 10,10
Endpoint of line or [Arc/Halfwidth/Length/Width/Close/Undo]: C
Command: LIST
Select objects or [Window/Crossing/Last/Previous/ALL/Remove/Add]: ALL
Select objects (1 found) or [...]:
POLYLINE  Handle = 1  Layer: 0
```

One entity, where the same three points as `LINE` gave two. `C` closed it.

The options are R12's: `Arc` switches to arc segments, `Width` and `Halfwidth`
set the segment width, `Length` continues in the current direction by a
distance, `Undo` steps back a vertex.

A **closed** polyline is the thing to reach for when you are drawing a profile
— one closed coplanar loop as a single object, which is what anything reading a
profile wants.

## CIRCLE — centre and radius, or diameter

```
Command: CIRCLE
Specify center point for circle: 5,5
Specify radius of circle or [Diameter]: 3
```

`Diameter` takes the other number, and `D` is enough to choose it:

```
Command: CIRCLE
Specify center point for circle: 0,0
Specify radius of circle or [Diameter]: D
Specify diameter of circle: 10
Command: LIST
Select objects or [Window/Crossing/Last/Previous/ALL/Remove/Add]: ALL
Select objects (1 found) or [...]:
CIRCLE  Handle = 1  Layer: 0
  center  X = 0.0000  Y = 0.0000  Z = 0.0000
  radius 5.0000
  circumference 31.4159
  area 78.5398
```

Diameter 10 stored as radius 5, and `LIST` volunteers the circumference and
area — often the quickest way to answer a question about a drawing without
reaching for `AREA`.

## ARC — three points, or a centre

```
Command: ARC
Center/<Start point> or [Center]: 0,0
Center/End/<Second point> or [Center/End]: 5,5
End point: 10,0
Command: LIST
Select objects or [Window/Crossing/Last/Previous/ALL/Remove/Add]: ALL
Select objects (1 found) or [...]:
ARC  Handle = 1  Layer: 0
  center  X = 5.0000  Y = 0.0000  Z = 0.0000
  radius 5.0000
  start angle 0.0000
  end angle 180.0000
  length 15.7080
```

The default is the R12 three-point arc: start, a point on the way, end. Note
what `LIST` reports — the three points you gave are *not* stored. An arc is a
centre, a radius and two angles, and the three points were the way you
specified it. Here they came out as the semicircle from `10,0` round to `0,0`.

`Center` at the first prompt starts from the centre instead; `End` at the
second lets you give the endpoint before the middle.

**Arcs are drawn counter-clockwise from start angle to end angle.** If an arc
comes out as the complement of what you wanted, that is the reason, and
swapping the start and end points fixes it.

## ELLIPSE — an axis, then the other half-width

```
Command: ELLIPSE
Center/<Axis endpoint 1> or [Center]: 0,0
Axis endpoint 2: 10,0
Rotation/<Other axis distance> or [Rotation]: 3
```

The first two points give one full axis — here the major axis from `0,0` to
`10,0` — and the third answer is the **half-width** of the other axis, not its
full length. So this is an ellipse 10 wide and 6 tall.

`Center` at the first prompt takes the centre and then one axis *endpoint*
instead. `Rotation` describes the ellipse as a circle viewed at an angle, which
is the classic R12 alternative.

A rough edge worth knowing: `LIST` reports an ellipse by name only, with no
geometry beneath it.

```
ELLIPSE  Handle = 2  Layer: 0
```

Ellipses are R2000-and-later DXF entities. Saving as R12 is still possible, but
an ellipse cannot be itself in that format — see chapter 7 when it is written.

## SPLINE — a fitted curve through points

```
Command: SPLINE
First point: 0,0
Next point: 5,8
Next point or <Enter to finish>: 10,0
Next point or <Enter to finish>: 15,8
Next point or <Enter to finish>:
Command: LIST
Select objects or [Window/Crossing/Last/Previous/ALL/Remove/Add]: ALL
Select objects (1 found) or [...]:
SPLINE  Handle = 1  Layer: 0
```

Points until Enter. The curve passes through the ones you gave. As with
`ELLIPSE`, `LIST` names it without detail, and as with `ELLIPSE` it needs
R2000 to survive a save as itself.

**`OFFSET` declines splines, and says so misleadingly.** It will not
approximate a spline with line segments — a deliberate refusal, and the same
one ellipses get. But the message names the wrong cause:

```
Command: OFFSET
Offset distance or Through or [Through]: 0.1
Select object to offset: 1
Side to offset: 5,1
; cannot offset that object that far
```

The distance is not the problem. That was 0.1 units, and a `LINE` offsets
happily at the same distance in the same drawing. Splines and ellipses are
declined *by type*, and no distance you choose will work. Read "cannot offset
that object that far" as "cannot offset that object" when the object is a
spline or an ellipse.

## POINT — a single marked location

```
Command: POINT
Point: 1,1
```

A point is a real entity with a handle, not a mark on the screen: it is
selectable, it saves, and the `NOD` object snap exists to catch it. Use them as
construction references you can snap to and erase later.

## SOLID — a filled quadrilateral

```
Command: SOLID
First point: 0,0
Second point: 10,0
Third point: 0,10
Fourth point: 10,10
Third point:
```

**Note the vertex order, because it is the classic R12 trap.** The third and
fourth points go across the diagonal, not around the perimeter: first and
second are one edge, third and fourth the *opposite* edge. Give them in
perimeter order and you get a bow-tie. The transcript above is a proper
10 × 10 square.

The prompt then asks `Third point:` again, to continue into a strip of joined
quadrilaterals; Enter ends it. Answering only three points and pressing Enter
gives a triangle.

**A `SOLID` is not a solid.** It is a flat filled quadrilateral — a
two-dimensional entity that happens to have a filled interior, and nothing to
do with solid modelling. The name is R12's and is kept for compatibility. Note
also that wireframe display means what it says: a `SOLID` draws as its edges.

## 3DFACE — a quadrilateral in space

```
Command: 3DFACE
First point: 0,0,0
Second point: 5,0,0
Third point: 5,5,0
Fourth point: 0,5,0
Third point:
```

The three-dimensional counterpart, and the vertex order is the sane one here:
**around the perimeter**, unlike `SOLID`. Points carry a Z, so the face need
not be flat in the world XY plane. Like `SOLID` it repeats to build a strip,
and Enter ends it.

`3DFACE` is the entity AutoLISP reaches for when generating meshes — the
interpreter is built to make tens of thousands of them through `entmake`
without complaint.

## What LIST tells you

`LIST` is the drafting companion command, and it has been in every transcript
above for a reason: it is how you check what you actually made rather than what
you meant to make.

```
Command: LIST
Select objects or [Window/Crossing/Last/Previous/ALL/Remove/Add]: ALL
Select objects (3 found) or [...]:
POINT  Handle = 1  Layer: 0
SOLID  Handle = 2  Layer: 0
3DFACE  Handle = 3  Layer: 0
```

Every entity has a **handle** — a stable number that names it for the whole
life of the drawing — and a layer. How much detail follows the header varies by
type: lines, arcs and circles report their full geometry, while polylines,
splines, ellipses and the face types report the header only. That unevenness is
alpha, not design.

Selection is command-then-select throughout: `ALL`, or `Window` / `Crossing`
for a region, `Last`, `Previous`, and `Remove` / `Add` to adjust a set you have
already built. Enter on the empty selection prompt ends selection and runs the
command.

## Where to go next

- [3. Editing and construction](03-editing.md) — changing what you have drawn.
  *Stub today.*
