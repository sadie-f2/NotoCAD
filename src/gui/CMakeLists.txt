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

add_library(ncad_gui_lib STATIC
  qpainter_renderer.cpp
  viewport_widget.cpp
  command_line_widget.cpp
  main_window.cpp
  command_icons.cpp
)

target_include_directories(ncad_gui_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${NCAD_VERSION_DIR})
add_dependencies(ncad_gui_lib ncad_version)
# ncad_app carries PromptSession: the GUI command line and the ncad terminal
# prompt run the same code, which is the only way they stay consistent.
target_link_libraries(ncad_gui_lib PUBLIC ncad_core ncad_app ncad_sample Qt6::Widgets)
target_link_libraries(ncad_gui_lib PRIVATE ncad_flags)

# The library is ncad_gui_lib to avoid colliding with the ncad_gui executable.
add_executable(ncad_gui main.cpp)
target_link_libraries(ncad_gui PRIVATE ncad_gui_lib ncad_flags)

# The deployable shape, opted into so day-to-day builds keep their flat
# binary path. CMake generates Contents/MacOS + Info.plist from the
# properties below; packaging/mac_bundle.sh does the rest (Qt frameworks in,
# stray Homebrew paths checked, signed, disk image). No icon yet -- when one
# exists, MACOSX_BUNDLE_ICON_FILE names it here.
if(APPLE AND NCAD_MACOS_BUNDLE)
  set_target_properties(ncad_gui PROPERTIES
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME "NotoCAD"
    MACOSX_BUNDLE_BUNDLE_NAME "NotoCAD"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.notocad.NotoCAD"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
endif()
