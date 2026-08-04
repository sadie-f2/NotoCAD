;;; SPDX-License-Identifier: BSD-3-Clause
;;; Copyright (c) 2026, Sadie Forbes
;;;
;;; R2000 conformance drawing -- one of every entity we can make, for opening
;;; in a real reader.
;;;
;;; WHY THIS EXISTS. The AC1015 writer has been through AutoCAD exactly once,
;;; with a file containing splines on four layers. Everything else -- MTEXT,
;;; ELLIPSE, blocks, nested INSERTs, TEXT, tilted planes, bulged polylines,
;;; non-CONTINUOUS linetypes -- is verified only by our own tests and our own
;;; reader. The R12 handle bug is the standing proof that this is not enough:
;;; it passed every test we had, round-tripped through our reader perfectly,
;;; and AutoCAD still called the file corrupt. Structural self-checks cannot
;;; find what we did not think to assert.
;;;
;;; WHY A SCRIPT RATHER THAN A SAVED DRAWING. The target stays FIXED between
;;; attempts. Each iteration is (conform) then one SAVEAS, so a rejection is
;;; attributable to the change made since the last attempt rather than to
;;; having drawn something slightly different by hand. Five R2000 rejections
;;; have already been diagnosed this way and every one read like an
;;; inconsistency somebody would have tidied away.
;;;
;;; USAGE. There is no (load) in this dialect yet -- a positional argument is
;;; a LISP file, which is how ncad takes one:
;;;
;;;   ncad tests/acad/r2000_conformance.lsp -e "(conform)" -i
;;;
;;; then, at the prompt:
;;;
;;;   SETVAR  DXFVERSION  R2000
;;;   SAVEAS  r2000_conformance.dxf
;;;
;;; or in one shot, which is the form worth keeping in shell history because
;;; it writes BOTH revisions of the same drawing:
;;;
;;;   ncad tests/acad/r2000_conformance.lsp -e '(conform)' \
;;;     -e '(progn (setvar "DXFVERSION" "R2000")
;;;                (command "SAVEAS" "/tmp/cf_r2000.dxf")
;;;                (setvar "DXFVERSION" "R12")
;;;                (command "SAVEAS" "/tmp/cf_r12.dxf"))'
;;;
;;; Both, because the same drawing through both writers is what says whether a
;;; fault is in the AC1015 path or in the geometry underneath it. As measured
;;; when this was written:
;;;
;;;             R2000                  R12
;;;   ELLIPSE     2      -->            0   (polylines)
;;;   SPLINE      2      -->            0   (polylines)
;;;   MTEXT       1      -->            0   (a run of TEXT: 19 against 15)
;;;   VERTEX      8      -->          154
;;;
;;; That table IS the honest-degradation rule, in numbers. If a future R2000
;;; file shows a zero in the first column, the writer has regressed to
;;; degrading something it is supposed to name.
;;;
;;; WHAT TO LOOK AT once it opens. Every specimen sits at a labelled station,
;;; so a fault can be named rather than pointed at. The labels are TEXT on
;;; layer LABELS; freeze it to see the geometry alone. In AutoCAD, LIST a
;;; suspect specimen and compare its type against its label: the failure this
;;; is hunting is an entity arriving as the WRONG KIND (a real SPLINE versus a
;;; polyline pretending), not usually as the wrong shape.

;;; Station spacing. Wide enough that a tilted specimen cannot overlap its
;;; neighbour and be mistaken for it.
(setq *cf-step* 45.0)
(setq *cf-cols* 4)

;;; Where the MTEXT fragment is staged. MTEXT is the one entity with no
;;; command and no entmake support, so the only way to get one into a drawing
;;; is to read a file containing one -- see cf-mtext below.
(setq *cf-frag* "/tmp/cf_mtext.dxf")

;;; MINSERT is HELD OUT by default, and this is the only specimen that is.
;;;
;;; AutoCAD refuses our AC1015 MINSERT with "Class separator for class
;;; AcDbMInsertBlock expected", and has refused all three placements of that
;;; separator: before the array fields, before them with the parent class's
;;; every field written out first, and after them at the end of the record.
;;; The drawing is discarded on the first error, so this ONE record was
;;; blocking verification of everything after it -- the nested inserts, the
;;; UCS geometry, SOLID, 3DFACE, and the ELLIPSE, SPLINE and MTEXT that are
;;; the whole reason R2000 exists here.
;;;
;;; Holding it out is not giving up on it: it is refusing to let one unsolved
;;; record hide the state of thirty others. Set this to T once there is a
;;; reference MINSERT written by AutoCAD itself to match against -- guessing
;;; has now cost three attempts and produced no information.
(setq *cf-minsert* nil)

;;; --- plumbing --------------------------------------------------------------

;;; Station N as a point. Row-major, growing upward, so station 0 is bottom
;;; left and the drawing reads like the list in cf-report.
(defun cf-at (n / col row)
  (setq col (rem n *cf-cols*)
        row (fix (/ n *cf-cols*)))
  (list (* col *cf-step*) (* row *cf-step*) 0.0))

;;; A point offset from station N, which is how every specimen positions
;;; itself -- so moving a station moves the whole specimen with it.
(defun cf-off (n dx dy / p)
  (setq p (cf-at n))
  (list (+ (car p) dx) (+ (cadr p) dy) 0.0))

;;; The station's caption. Placed below the specimen rather than inside it,
;;; because a label overlapping the geometry is the thing you least want when
;;; the geometry is what you are trying to read.
(defun cf-label (n txt)
  (cf-layer "LABELS")
  (command "TEXT" (cf-off n 0.0 -4.0) 1.6 0.0 txt))

;;; Current layer, made if it is not there yet. LAYER Make does both, which is
;;; why it is Make rather than Set -- Set on a layer that does not exist is an
;;; error and would abort the script half-drawn.
(defun cf-layer (name)
  (command "LAYER" "Make" name ""))

;;; --- the drawing state this script depends on -------------------------------

;;; A running osnap would pull points onto whatever is already drawn, so a
;;; specimen would silently acquire a vertex from its neighbour. Nothing else
;;; in this file matters if this is missing -- the same trap t2_chain.lsp
;;; records, and for the same reason.
(defun cf-quiet ()
  (setq *cf-osmode* (getvar "OSMODE"))
  (setvar "OSMODE" 0)
  (princ))

(defun cf-restore ()
  (if *cf-osmode* (setvar "OSMODE" *cf-osmode*))
  (princ))

;;; Layers carrying colour and a non-CONTINUOUS linetype. Both travel in the
;;; TABLES section and neither has been through a real reader at AC1015; a
;;; linetype is also the one table entry R2000 rejected us over, so it earns a
;;; specimen of its own rather than being left as a property of layer 0.
(defun cf-tables ()
  (foreach lt '("DASHED" "HIDDEN" "CENTER" "PHANTOM" "DASHDOT")
    (command "LTYPE" "Load" lt ""))

  ;; Each option takes its OWN name list: Color asks for a colour and then for
  ;; the layers to apply it to, so an Ltype following a colour is read as a
  ;; layer name rather than as the next option. That mistake reports as
  ;; "LAYER: unknown option" one argument later, which points at the wrong
  ;; place entirely.
  (command "LAYER" "Make" "LABELS" "Color" "8" "LABELS" "")
  (command "LAYER" "Make" "GEOM"   "Color" "7" "GEOM"   "")
  (command "LAYER" "Make" "DASH"   "Color" "1" "DASH" "Ltype" "DASHED" "DASH" "")
  (command "LAYER" "Make" "HIDE"   "Color" "3" "HIDE" "Ltype" "HIDDEN" "HIDE" "")
  (command "LAYER" "Make" "CTR"    "Color" "5" "CTR"  "Ltype" "CENTER" "CTR"  "")
  (princ))

;;; --- MTEXT, which has to come in through a file -----------------------------

;;; MTEXT has no command and entmake has no branch for it, so a drawing can
;;; only acquire one by reading a file. That is worth stating plainly rather
;;; than leaving as a puzzle: this writes a minimal DXF holding one MTEXT and
;;; reads it back.
;;;
;;; DXFIN used to CLEAR the entities, which is why this runs first and
;;; everything else is drawn on top of it. It now imports alongside whatever is
;;; already in the drawing, so that ordering is no longer required -- it is kept
;;; only because there is no reason to change it and the station numbering reads
;;; better with station 0 built first. Tables always survived a read.
;;;
;;; The string carries inline codes on purpose. \P is a paragraph break, \L
;;; underline, \S a stacked fraction -- the formatting NotoCAD holds exactly
;;; and discards only at layout time. If AutoCAD shows the codes as literal
;;; text then the escaping is wrong on the way out; if it shows formatted text
;;; then the raw string survived the round trip, which is the whole claim
;;; MTEXT-as-a-database-entity rests on.
(defun cf-mtext (/ f p)
  (setq f (open *cf-frag* "w"))
  (if (null f)
    (progn (princ (strcat "\n  cannot write " *cf-frag* " -- no MTEXT specimen"))
           nil)
    (progn
      (setq p (cf-at 0))
      (foreach line
        (list "0" "SECTION" "2" "ENTITIES"
              "0" "MTEXT"
              "8" "GEOM"
              "10" (rtos (car p) 2 6)
              "20" (rtos (+ (cadr p) 20.0) 2 6)
              "30" "0.0"
              "40" "2.0"                       ; text height
              "41" "34.0"                      ; reference rectangle width
              "71" "1"                         ; attachment: top left
              "1" "MTEXT one.\\PSecond paragraph, {\\LUnderlined\\l} and \\S1^2; stacked."
              "0" "ENDSEC" "0" "EOF")
        (write-line line f))
      (close f)
      (command "DXFIN" *cf-frag*)
      T)))

;;; --- the specimens ----------------------------------------------------------

;;; Station 0 also holds the MTEXT read in above, so this only labels it.
(defun cf-s0 ()
  (cf-label 0 "0 MTEXT + TEXT")
  (cf-layer "GEOM")
  ;; TEXT at several justifications. Middle and MC differ only for a string
  ;; with a descender, which is why one is here: Middle centres on the text
  ;; including descenders, MC on the uppercase height, and any string without
  ;; a descender hides the difference.
  (command "TEXT" (cf-off 0 0.0 12.0) 2.0 0.0 "Left gyp")
  (command "TEXT" "Justify" "C" (cf-off 0 16.0 8.0) 2.0 0.0 "Centred gyp")
  (command "TEXT" "Justify" "R" (cf-off 0 32.0 4.0) 2.0 0.0 "Right gyp")
  ;; Rotated, because a rotation is stored as an angle and a tilted TEXT is
  ;; where an ECS mistake shows up first.
  (command "TEXT" (cf-off 0 0.0 0.0) 2.0 30.0 "Rotated 30")
  (princ))

;;; Straight lines on the three linetype layers. The dashes themselves are
;;; generated at render time here and by the reader there, so what is being
;;; checked is that the LTYPE table arrived, not that our dash pattern matches
;;; theirs pixel for pixel.
(defun cf-s1 ()
  (cf-label 1 "1 LINE + linetypes")
  (cf-layer "GEOM") (command "LINE" (cf-off 1 0.0 0.0)  (cf-off 1 34.0 0.0)  "")
  (cf-layer "DASH") (command "LINE" (cf-off 1 0.0 6.0)  (cf-off 1 34.0 6.0)  "")
  (cf-layer "HIDE") (command "LINE" (cf-off 1 0.0 12.0) (cf-off 1 34.0 12.0) "")
  (cf-layer "CTR")  (command "LINE" (cf-off 1 0.0 18.0) (cf-off 1 34.0 18.0) "")
  ;; A 3D line, so the drawing is not entirely flat before the tilted stations.
  (cf-layer "GEOM")
  (command "LINE" (cf-off 1 0.0 24.0) (list (+ (car (cf-at 1)) 34.0)
                                            (+ (cadr (cf-at 1)) 30.0) 12.0) "")
  (princ))

;;; CIRCLE and ARC in plan, then the same two tilted. R12 stores both as ECS
;;; coordinates plus an extrusion vector, so a tilted one is the single best
;;; test of the arbitrary axis algorithm: get it wrong and the shape is right
;;; but its position and plane are not.
(defun cf-s2 ()
  (cf-label 2 "2 CIRCLE + ARC")
  (cf-layer "GEOM")
  (command "CIRCLE" (cf-off 2 8.0 8.0) 7.0)
  (command "ARC" (cf-off 2 20.0 2.0) (cf-off 2 27.0 9.0) (cf-off 2 20.0 16.0))
  ;; Counterclockwise from 30 to 200 degrees, in radians, about a tilted
  ;; normal. entmake takes the extrusion directly, which is why the tilted
  ;; specimens are made this way rather than with the commands.
  (entmake (list '(0 . "CIRCLE")
                 (cons 8 "CTR")
                 (cons 10 (cf-off 2 8.0 26.0))
                 '(40 . 6.0)
                 '(210 0.0 0.4472 0.8944)))
  (entmake (list '(0 . "ARC")
                 (cons 8 "DASH")
                 (cons 10 (cf-off 2 26.0 26.0))
                 '(40 . 6.0)
                 (cons 50 (/ (* 30.0 pi) 180.0))
                 (cons 51 (/ (* 200.0 pi) 180.0))
                 '(210 0.7071 0.0 0.7071)))
  (princ))

;;; ELLIPSE, full and as an arc. AC1009 has no ELLIPSE at all, so at R12 these
;;; degrade to polylines and at R2000 they must arrive as real ELLIPSE
;;; entities. That difference is the entire reason the AC1015 writer exists,
;;; and it is the first thing to check in AutoCAD's LIST output.
(defun cf-s3 ()
  (cf-label 3 "3 ELLIPSE (R2000: real)")
  (cf-layer "GEOM")
  (entmake (list '(0 . "ELLIPSE")
                 (cons 10 (cf-off 3 10.0 8.0))
                 '(11 12.0 0.0 0.0)                 ; major axis FROM the centre
                 '(40 . 0.45)))
  ;; A half ellipse, so the parametric range travels too -- a full one would
  ;; hide a start/end that did not survive.
  (entmake (list '(0 . "ELLIPSE")
                 (cons 8 "HIDE")
                 (cons 10 (cf-off 3 10.0 26.0))
                 '(11 10.0 6.0 0.0)
                 '(40 . 0.5)
                 (cons 41 0.0)
                 (cons 42 pi)))
  (princ))

;;; SPLINE. The one entity already confirmed to arrive as itself, kept as the
;;; control: if this specimen is wrong then the fault is in the file as a
;;; whole rather than in the entity being examined.
(defun cf-s4 ()
  (cf-label 4 "4 SPLINE (control)")
  (cf-layer "GEOM")
  (entmake (list '(0 . "SPLINE")
                 (cons 11 (cf-off 4 0.0 0.0))
                 (cons 11 (cf-off 4 8.0 14.0))
                 (cons 11 (cf-off 4 18.0 2.0))
                 (cons 11 (cf-off 4 28.0 16.0))
                 (cons 11 (cf-off 4 34.0 6.0))))
  ;; Out of plane, because a planar spline cannot show a normal that did not
  ;; survive and every spline tested so far has been flat.
  (entmake (list '(0 . "SPLINE")
                 (cons 8 "CTR")
                 (cons 11 (cf-off 4 0.0 22.0))
                 (cons 11 (list (+ (car (cf-at 4)) 12.0) (+ (cadr (cf-at 4)) 30.0) 10.0))
                 (cons 11 (list (+ (car (cf-at 4)) 24.0) (+ (cadr (cf-at 4)) 22.0) -6.0))
                 (cons 11 (cf-off 4 34.0 30.0))))
  (princ))

;;; POLYLINE with bulges. A bulge is a signed tangent of a quarter of the
;;; included angle and its SIGN decides which way the arc leaves the chord --
;;; positive arcs BELOW a left-to-right chord, which is the trap recorded in
;;; test_polyline.cpp. A specimen with bulges of both signs is therefore worth
;;; more than two with the same sign.
(defun cf-s5 ()
  (cf-label 5 "5 PLINE + bulges")
  (cf-layer "GEOM")
  (command "PLINE" (cf-off 5 0.0 0.0)
           (cf-off 5 10.0 0.0)
           "Arc" (cf-off 5 20.0 0.0)
           "Line" (cf-off 5 30.0 0.0)
           "")
  ;; Closed, with width, so the closing segment and the width both travel.
  (command "PLINE" (cf-off 5 0.0 12.0)
           "Width" "0.8" "0.8"
           (cf-off 5 14.0 12.0)
           "Arc" (cf-off 5 20.0 20.0)
           "Line" (cf-off 5 4.0 24.0)
           "Close")
  (princ))

;;; POINT, SOLID and 3DFACE. The filled ones matter because a SOLID's third
;;; and fourth corners are given in the order that makes a bowtie if they are
;;; swapped -- a fault that is invisible in a wireframe and obvious the moment
;;; something shades it.
(defun cf-s6 ()
  (cf-label 6 "6 POINT SOLID 3DFACE")
  (cf-layer "GEOM")
  (command "POINT" (cf-off 6 2.0 2.0))
  (command "POINT" (cf-off 6 6.0 2.0))
  (command "SOLID" (cf-off 6 0.0 8.0) (cf-off 6 12.0 8.0)
                   (cf-off 6 0.0 18.0) (cf-off 6 12.0 18.0) "")
  ;; A 3DFACE genuinely out of plane, which is the only kind worth writing --
  ;; a flat one cannot distinguish a working Z from a dropped one.
  (command "3DFACE" (cf-off 6 18.0 8.0)
                    (cf-off 6 32.0 8.0)
                    (list (+ (car (cf-at 6)) 32.0) (+ (cadr (cf-at 6)) 20.0) 10.0)
                    (list (+ (car (cf-at 6)) 18.0) (+ (cadr (cf-at 6)) 20.0) 4.0)
                    "")
  (princ))

;;; A block, an INSERT of it, a rotated and scaled INSERT, and a MINSERT.
;;; Blocks travel in their own DXF section and every INSERT is a reference
;;; into it, so a block that arrives subtly wrong is wrong everywhere it is
;;; used -- which is what makes it worth more than one specimen.
(defun cf-s7 ()
  (cf-label 7 "7 BLOCK + INSERT")
  (cf-layer "GEOM")
  ;; The block's own geometry, drawn at the station and then consumed by
  ;; BLOCK. Deliberately more than one entity and more than one type.
  (command "LINE" (cf-off 7 0.0 0.0) (cf-off 7 6.0 0.0) "")
  (command "LINE" (cf-off 7 6.0 0.0) (cf-off 7 6.0 6.0) "")
  (command "LINE" (cf-off 7 6.0 6.0) (cf-off 7 0.0 0.0) "")
  (command "CIRCLE" (cf-off 7 3.0 2.0) 1.2)
  (command "BLOCK" "CF_TRI" (cf-off 7 0.0 0.0) "Window"
           (cf-off 7 -2.0 -2.0) (cf-off 7 8.0 8.0) "")
  (command "INSERT" "CF_TRI" (cf-off 7 0.0 0.0) 1.0 1.0 0.0)
  (command "INSERT" "CF_TRI" (cf-off 7 14.0 0.0) 1.5 1.5 30.0)
  (command "INSERT" "CF_TRI" (cf-off 7 28.0 0.0) 1.0 2.0 0.0)
  (if *cf-minsert*
    (command "MINSERT" "CF_TRI" (cf-off 7 0.0 16.0) 1.0 1.0 0.0 2 3 9.0 11.0)
    (princ "\n  (MINSERT held out -- see *cf-minsert*)"))
  (princ))

;;; A NESTED insert: a block whose definition contains an INSERT of another
;;; block. This is the case the R12 round trip exercised at scale (1,643 of
;;; them) and the one most likely to expose an owner pointer that resolves to
;;; the wrong record at R2000, since every entity now names its owner.
(defun cf-s8 ()
  (cf-label 8 "8 nested INSERT")
  (cf-layer "GEOM")
  (command "INSERT" "CF_TRI" (cf-off 8 0.0 0.0) 1.0 1.0 0.0)
  (command "CIRCLE" (cf-off 8 3.0 10.0) 2.5)
  (command "BLOCK" "CF_NEST" (cf-off 8 0.0 0.0) "Window"
           (cf-off 8 -3.0 -3.0) (cf-off 8 10.0 14.0) "")
  (command "INSERT" "CF_NEST" (cf-off 8 0.0 0.0) 1.0 1.0 0.0)
  (command "INSERT" "CF_NEST" (cf-off 8 16.0 4.0) 1.2 1.2 45.0)
  (princ))

;;; Geometry built under a rotated UCS. Everything above is drawn in the world
;;; system; this is the one station that asks whether the current UCS was
;;; applied on the way in and NOT applied a second time on the way out.
(defun cf-s9 ()
  (cf-label 9 "9 UCS geometry")
  (cf-layer "GEOM")
  (command "UCS" "3point" (cf-at 9) (cf-off 9 10.0 4.0) (cf-off 9 0.0 12.0))
  (command "LINE" '(0.0 0.0 0.0) '(20.0 0.0 0.0) "")
  (command "LINE" '(20.0 0.0 0.0) '(20.0 14.0 0.0) "")
  (command "CIRCLE" '(10.0 7.0 0.0) 4.0)
  (command "TEXT" '(0.0 16.0 0.0) 2.0 0.0 "in UCS")
  (command "UCS" "World")
  (princ))

;;; --- orchestration ----------------------------------------------------------

(defun cf-report ()
  (princ "\n  Conformance drawing built. Stations:")
  (princ "\n    0  MTEXT (from file) + TEXT justifications")
  (princ "\n    1  LINE, four linetypes, one 3D")
  (princ "\n    2  CIRCLE + ARC, in plan and tilted (ECS)")
  (princ "\n    3  ELLIPSE, full and half   <- R12 degrades, R2000 must not")
  (princ "\n    4  SPLINE, planar and 3D    <- the control")
  (princ "\n    5  PLINE with bulges, and a closed wide one")
  (princ "\n    6  POINT, SOLID, 3DFACE")
  (princ "\n    7  BLOCK, INSERT scaled/rotated, MINSERT")
  (princ "\n    8  nested INSERT")
  (princ "\n    9  geometry under a rotated UCS")
  (princ "\n\n  Now:  SETVAR DXFVERSION R2000   then   SAVEAS")
  (princ "\n  Then the same at R12, and compare which specimens changed type.")
  (princ))

(defun conform ()
  (cf-quiet)
  (cf-tables)
  ;; First, because reading a file clears the entities.
  (cf-mtext)
  (cf-tables)
  (cf-s0) (cf-s1) (cf-s2) (cf-s3) (cf-s4)
  (cf-s5) (cf-s6) (cf-s7) (cf-s8) (cf-s9)
  (cf-layer "GEOM")
  (cf-restore)
  (cf-report))

(princ "\n  r2000_conformance.lsp loaded.  (conform) builds the drawing.")
(princ)
