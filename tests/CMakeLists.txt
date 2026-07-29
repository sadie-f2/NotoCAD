# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes

add_executable(noto_tests
  test_main.cpp
  test_math.cpp
  test_entities.cpp
  test_dxf.cpp
  test_lisp.cpp
  test_eval.cpp
  test_entmake.cpp
  test_getvar.cpp
  test_dxfout.cpp
  test_command.cpp
  test_prompt.cpp
  test_osnap_derived.cpp
  test_sysvar.cpp
  test_pick.cpp
  test_osnap_search.cpp
  test_grip.cpp
  test_osnap_override.cpp
  test_undo.cpp
  test_selection.cpp
  test_move.cpp
  test_transform.cpp
  test_array.cpp
  test_stretch.cpp
  test_ssget.cpp
  test_inquiry.cpp
  test_view.cpp
  test_polyline.cpp
  test_pline.cpp
  test_draw_commands.cpp
  test_pedit.cpp
  test_intersect.cpp
  test_blocks.cpp
  test_break.cpp
  test_trim.cpp
  test_ucs.cpp
  test_layer.cpp
  test_dash.cpp
  test_dxf_read.cpp
  test_entities7.cpp
  test_rotate3d.cpp
  test_render.cpp
  test_viewport.cpp
  test_numeric.cpp
  test_inflight.cpp
  test_arc.cpp
  test_registry.cpp
  test_ellipse.cpp
)

target_include_directories(noto_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(noto_tests PRIVATE noto_core noto_lisp noto_app noto_flags)

add_test(NAME noto_tests COMMAND noto_tests)

# Smoke tests for the application binary itself. The unit suite exercises the
# library; these prove the wiring in main.cpp works and the exit codes are right.
add_test(NAME ncad_cli_eval COMMAND ncad -e "(princ (* 6 7))")
set_tests_properties(ncad_cli_eval PROPERTIES PASS_REGULAR_EXPRESSION "42")

add_test(NAME ncad_cli_entmake COMMAND ncad
         -e "(entmake (list (cons 0 \"CIRCLE\") (list 10 0.0 0.0 0.0) (cons 40 5.0)))"
         -e "(princ (type (entlast)))")
set_tests_properties(ncad_cli_entmake PROPERTIES PASS_REGULAR_EXPRESSION "ENAME")

# A LISP error must fail the process, not be swallowed.
add_test(NAME ncad_cli_error COMMAND ncad -e "(/ 1 0)")
set_tests_properties(ncad_cli_error PROPERTIES WILL_FAIL TRUE)

add_test(NAME ncad_cli_sysvar COMMAND ncad
         -e "(setvar \"OSMODE\" 47)"
         -e "(princ (getvar \"OSMODE\"))")
set_tests_properties(ncad_cli_sysvar PROPERTIES PASS_REGULAR_EXPRESSION "47")

add_test(NAME ncad_cli_help COMMAND ncad -h)
set_tests_properties(ncad_cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage")

# The command state machine reached from LISP, through the real binary.
add_test(NAME ncad_cli_command COMMAND ncad
         -e "(command \"LINE\" '(0 0 0) '(10 0 0) \"\")"
         -e "(princ (cdr (assoc 11 (entget (entlast)))))")
set_tests_properties(ncad_cli_command PROPERTIES PASS_REGULAR_EXPRESSION "10.0")

# The command prompt reads stdin, so driving it needs a shell to pipe with.
if(UNIX)
  add_test(NAME ncad_cli_prompt COMMAND sh -c
    "printf 'LINE\\n0,0\\n10,0\\n\\n(princ (cdr (assoc 11 (entget (entlast)))))\\n' | $<TARGET_FILE:ncad>")
  set_tests_properties(ncad_cli_prompt PROPERTIES PASS_REGULAR_EXPRESSION "10.0")

  add_test(NAME ncad_cli_bang_variable COMMAND sh -c
    "printf '(setq r 7.5)\\nCIRCLE\\n0,0\\n!r\\n(princ (cdr (assoc 40 (entget (entlast)))))\\n' | $<TARGET_FILE:ncad>")
  set_tests_properties(ncad_cli_bang_variable PROPERTIES PASS_REGULAR_EXPRESSION "7.5")
endif()

  # A file name with parens must not be evaluated as LISP at a string prompt.
  add_test(NAME ncad_cli_paren_filename COMMAND sh -c
    "cd \"$<TARGET_FILE_DIR:ncad>\" && rm -f 'out (rev 2).dxf' && printf 'CIRCLE 0,0 5\\nDXFOUT\\nout (rev 2).dxf\\n' | ./ncad && test -f 'out (rev 2).dxf' && rm -f 'out (rev 2).dxf'")
