;;; SPDX-License-Identifier: BSD-3-Clause
;;; Copyright (c) 2026, Sadie Forbes
;;;
;;; T2 -- accumulated error in a closed chain of lines. Reference measurement,
;;; to be run in AutoCAD so NotoCAD has something to be compared against.
;;;
;;; N segments of length L, each drawn at an angle pi/4 beyond the last. Every
;;; eight segments the direction returns to zero, and the eight unit vectors at
;;; 0, pi/4 ... 7pi/4 sum to EXACTLY zero. So for N a multiple of 8 the chain
;;; closes on its own start point, the expected endpoint is the start with no
;;; oracle needed, and the distance between them is pure accumulated error.
;;;
;;; Two things about the method, both deliberate:
;;;
;;;   The angle is ACCUMULATED (ang = ang + pi/4) rather than computed as
;;;   i*pi/4. Accumulating rounds once per step, so the direction itself drifts
;;;   and that drift is part of what is being measured. Computing it from the
;;;   index rounds once in total and measures only the vertex arithmetic. The
;;;   accumulating form is what a user would actually write, so it is the one
;;;   under test -- but set *t2-accumulate* to nil to measure the other, and the
;;;   difference between the two runs is the cost of the accumulation alone.
;;;
;;;   Each new start point is read back with (entget (entlast)) instead of
;;;   being carried in a variable. Passing the variable forward would measure
;;;   LISP arithmetic; reading it back measures the whole path through the
;;;   command, the database and entity storage, which is what we care about.
;;;
;;; Usage:  (load "t2_chain.lsp")  then  (t2all)  or e.g.  (t2 800 1.0)

(setq *t2-accumulate* T)

;;; One run. N segments of length LEN, reporting the closure error.
(defun t2 (n len / os cm bm ss i ang p0 p1 start err)

  (setq os (getvar "OSMODE")
        cm (getvar "CMDECHO")
        bm (getvar "BLIPMODE"))

  ;; A running osnap would pull each new start onto the previous endpoint and
  ;; repair the very error being measured. Nothing else in this file matters
  ;; if this line is missing.
  (setvar "OSMODE" 0)
  (setvar "CMDECHO" 0)
  (setvar "BLIPMODE" 0)

  ;; polar works in the current UCS, so the measurement has to fix it.
  (command "_.UCS" "_World")

  ;; Start clean: entlast must find this run's line, not the last run's.
  (if (setq ss (ssget "_X")) (command "_.ERASE" ss ""))

  (setq start (list 0.0 0.0 0.0)
        p0    start
        ang   0.0
        i     0)

  (while (< i n)
    (setq p1 (polar p0 ang len))
    (command "_.LINE" p0 p1 "")

    (setq p0 (cdr (assoc 11 (entget (entlast))))
          ang (if *t2-accumulate*
                (+ ang (/ pi 4.0))
                (* (1+ i) (/ pi 4.0)))
          i   (1+ i)))

  (setq err (distance p0 start))

  ;; Reported relative to the segment length, because that is the only way the
  ;; L = 1 and L = 1e6 rows can be read against each other. Scaled by 1e15 so
  ;; the interesting digits are visible without relying on how many places rtos
  ;; is willing to give.
  (princ "\n  N=")        (princ n)
  (princ "  L=")          (princ (rtos len 2 4))
  (princ "   end=(")      (princ (rtos (car p0) 2 12))
  (princ ", ")            (princ (rtos (cadr p0) 2 12))
  (princ ")   err=")      (princ (rtos err 2 12))
  (princ "   err/L*1e15=") (princ (rtos (/ (* err 1e15) len) 2 3))

  (setvar "OSMODE" os)
  (setvar "CMDECHO" cm)
  (setvar "BLIPMODE" bm)
  (princ))

;;; The sweep. Magnitude across the columns, chain length down the rows, so a
;;; drift that scales with coordinate size and one that scales with step count
;;; are told apart by which way the table grows.
(defun t2all (/ )
  (princ "\nT2 closed chain -- accumulate=")
  (princ (if *t2-accumulate* "yes" "no"))
  (foreach len '(1.0 1.0e3 1.0e6 1.0e9)
    (princ "\n")
    (foreach n '(8 80 800 8000)
      (t2 n len)))
  (princ "\n")
  (princ))
