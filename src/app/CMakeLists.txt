# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes

# The prompt logic is a library so the test suite can reach it; main.cpp stays
# argument parsing and wiring only.
add_library(noto_app STATIC prompt.cpp
  iperl.cpp)
target_include_directories(noto_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(noto_app PUBLIC noto_core noto_lisp)
target_link_libraries(noto_app PRIVATE noto_flags)

add_executable(ncad main.cpp)
target_link_libraries(ncad PRIVATE noto_app noto_flags)
