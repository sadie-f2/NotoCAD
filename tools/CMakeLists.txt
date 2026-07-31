# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes

# The sample drawing is its own library because both gen_sample and the GUI
# viewer render it, and they are only a useful cross-check on each other while
# the content is identical.
add_library(ncad_sample STATIC sample_drawing.cpp)
target_include_directories(ncad_sample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ncad_sample PUBLIC ncad_core)
target_link_libraries(ncad_sample PRIVATE ncad_flags)

add_executable(ncad_gen_sample gen_sample.cpp)
target_link_libraries(ncad_gen_sample PRIVATE ncad_sample ncad_flags)
