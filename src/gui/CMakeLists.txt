# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes

# Qt6 is LGPLv3, so it is linked DYNAMICALLY and lives in its own target. Static
# linking would create relinking obligations the BSD-3 core exists to avoid --
# the same reasoning as the DWG option, and the same requirement that the
# boundary be visible in the build graph rather than inferred. Nothing in
# src/core or src/lisp may depend on this target.
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Guard the licensing decision rather than trusting it to stay true. A static
# Qt would silently change the terms under which the binary can be conveyed.
get_target_property(_qt_core_type Qt6::Core TYPE)
if(_qt_core_type STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR
    "This Qt6 is static. NotoCAD links Qt dynamically on purpose: static "
    "linking to LGPLv3 Qt imposes relinking obligations on the distributed "
    "binary. Use a shared Qt build, or take the decision deliberately.")
endif()

set(CMAKE_AUTOMOC ON)

add_library(noto_gui STATIC
  qpainter_renderer.cpp
  viewport_widget.cpp
  command_line_widget.cpp
  main_window.cpp
)

target_include_directories(noto_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# noto_app carries PromptSession: the GUI command line and the ncad terminal
# prompt run the same code, which is the only way they stay consistent.
target_link_libraries(noto_gui PUBLIC noto_core noto_app noto_sample Qt6::Widgets)
target_link_libraries(noto_gui PRIVATE noto_flags)

# Libraries are noto_*, commands are ncad*.
add_executable(ncad_gui main.cpp)
target_link_libraries(ncad_gui PRIVATE noto_gui noto_flags)
