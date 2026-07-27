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
  test_dxfout.cpp
)

target_include_directories(noto_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(noto_tests PRIVATE noto_core noto_lisp noto_flags)

add_test(NAME noto_tests COMMAND noto_tests)

# Smoke tests for the application binary itself. The unit suite exercises the
# library; these prove the wiring in main.cpp works and the exit codes are right.
add_test(NAME noto_cli_eval COMMAND noto -e "(princ (* 6 7))")
set_tests_properties(noto_cli_eval PROPERTIES PASS_REGULAR_EXPRESSION "42")

add_test(NAME noto_cli_entmake COMMAND noto
         -e "(entmake (list (cons 0 \"CIRCLE\") (list 10 0.0 0.0 0.0) (cons 40 5.0)))"
         -e "(princ (type (entlast)))")
set_tests_properties(noto_cli_entmake PROPERTIES PASS_REGULAR_EXPRESSION "ENAME")

# A LISP error must fail the process, not be swallowed.
add_test(NAME noto_cli_error COMMAND noto -e "(/ 1 0)")
set_tests_properties(noto_cli_error PROPERTIES WILL_FAIL TRUE)

add_test(NAME noto_cli_help COMMAND noto -h)
set_tests_properties(noto_cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage")
