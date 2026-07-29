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
;;; Usage:  (load "t2_chain.lsp")  then  (t2all)
;;;         a single run, echoing only:  (t2 800 1.0 nil)
;;;
;;; (t2all) writes CSV to the path in *t2-out* as well as echoing to the
;;; command line, because copying sixteen-digit numbers out of the command
;;; history is a good way to lose one and not notice. Set *t2-out* to nil to
;;; echo only. Forward slashes in the path even on Windows.

(setq *t2-accumulate* T)
(setq *t2-out* "c:/temp/t2_chain.csv")

;;; Open for write, or nil. Kept separate so a bad path is a warning and a
;;; still-usable run rather than a failed one.
(defun t2-port (/ p)
  (if *t2-out*
    (progn
      (setq p (open *t2-out* "w"))
      (if (null p) (princ (strcat "\n  cannot write " *t2-out* " -- echoing only")))
      p)
    nil))

;;; One run. N segments of length LEN, reporting the closure error. Echoes a
;;; CSV row and, if PORT is non-nil, writes the same row to it. Returns the
;;; error so a caller can do its own arithmetic on it.
(defun t2 (n len port / os cm bm ss i ang p0 p1 start err row)

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

  ;; err/L is the column that matters: relative to the segment length is the
  ;; only way the L = 1 and L = 1e9 rows can be read against each other. It is
  ;; scaled by 1e15 so the interesting digits survive rtos, which reports
  ;; DECIMAL PLACES rather than significant ones -- at L = 1 the raw error is
  ;; down around 1e-16 and would print as a row of zeros.
  (setq row (strcat (itoa n) ","
                    (rtos len 2 4) ","
                    (rtos (car p0) 2 14) ","
                    (rtos (cadr p0) 2 14) ","
                    (rtos err 2 14) ","
                    (rtos (/ (* err 1e15) len) 2 6)))

  (princ (strcat "\n  " row))
  (if port (write-line row port))

  (setvar "OSMODE" os)
  (setvar "CMDECHO" cm)
  (setvar "BLIPMODE" bm)
  err)

;;; The sweep. Magnitude across the columns, chain length down the rows, so a
;;; drift that scales with coordinate size and one that scales with step count
;;; are told apart by which way the table grows.
(defun t2all (/ port head)
  (setq port (t2-port)
        head "n,len,end_x,end_y,err,err_over_len_x1e15")

  (princ "\nT2 closed chain -- accumulate=")
  (princ (if *t2-accumulate* "yes" "no"))
  (princ "\n  ") (princ head)
  (if port (write-line head port))

  (foreach len '(1.0 1.0e3 1.0e6 1.0e9)
    (foreach n '(8 80 800 8000)
      (t2 n len port)))

  (if port
    (progn (close port)
           (princ (strcat "\n\n  written to " *t2-out*))))
  (princ "\n")
  (princ))
