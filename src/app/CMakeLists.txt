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
