# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes

# The sample drawing is its own library because both gen_sample and the GUI
# viewer render it, and they are only a useful cross-check on each other while
# the content is identical.
add_library(noto_sample STATIC sample_drawing.cpp)
target_include_directories(noto_sample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(noto_sample PUBLIC noto_core)
target_link_libraries(noto_sample PRIVATE noto_flags)

add_executable(noto_gen_sample gen_sample.cpp)
target_link_libraries(noto_gen_sample PRIVATE noto_sample noto_flags)
