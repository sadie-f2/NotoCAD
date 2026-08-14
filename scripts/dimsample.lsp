; A sheet of dimensions for checking against another CAD program.
; Every kind this program can make, on geometry whose answers are obvious.

(defun c ()
  ; A 100 x 40 rectangle, dimensioned horizontally and vertically.
  (command "LINE" '(0 0) '(100 0) "")
  (command "LINE" '(100 0) '(100 40) "")
  (command "LINE" '(100 40) '(0 40) "")
  (command "LINE" '(0 40) '(0 0) "")
  (command "DIMLINEAR" '(0 0) '(100 0) '(50 -25))
  (command "DIMLINEAR" '(100 0) '(100 40) '(130 20))

  ; A 3-4-5 triangle: the aligned dimension must read exactly 50.
  (command "LINE" '(0 100) '(30 100) "")
  (command "LINE" '(30 100) '(30 140) "")
  (command "LINE" '(30 140) '(0 100) "")
  (command "DIMALIGNED" '(0 100) '(30 140) '(-20 130))

  ; A circle of radius 25, so radius reads 25 and diameter reads 50.
  (command "CIRCLE" '(200 20) 25)
  (command "DIMRADIUS" (entlast) '(230 20))
  (command "CIRCLE" '(200 100) 25)
  (command "DIMDIAMETER" (entlast) '(200 130))

  ; Two arms at 90 degrees, dimensioned both ways: the arc inside reads 90,
  ; the arc outside reads 270, and both are correct.
  (command "LINE" '(300 0) '(360 0) "")
  (command "LINE" '(300 0) '(300 60) "")
  (command "DIMANGULAR" "" '(300 0) '(360 0) '(300 60) '(320 20))
  (command "DIMANGULAR" "" '(300 0) '(360 0) '(300 60) '(270 -30))

  ; A rotated dimension: 30 degrees across the same rectangle.
  (command "DIM" "ROTATED" 30 '(0 0) '(100 40) '(60 90) "EXIT")
  (princ "\ndimension sample built")
  (princ))
