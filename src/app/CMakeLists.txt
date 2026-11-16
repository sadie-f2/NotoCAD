# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes

# The prompt logic is a library so the test suite can reach it; main.cpp stays
# argument parsing and wiring only.
add_library(ncad_app STATIC prompt.cpp
  iperl.cpp)
target_include_directories(ncad_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ncad_app PUBLIC ncad_core ncad_lisp)
target_link_libraries(ncad_app PRIVATE ncad_flags)

add_executable(ncad main.cpp)
target_link_libraries(ncad PRIVATE ncad_app ncad_flags)

# Linked to the executable and not to ncad_app, so the prompt tests (and the
# command-count test) see a NotoCAD with no laser in it.
if(NCAD_WITH_LASER)
  target_link_libraries(ncad PRIVATE ncad_laser_ncad)
  target_compile_definitions(ncad PRIVATE NCAD_HAVE_LASER)
endif()
