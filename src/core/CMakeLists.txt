# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes

add_library(ncad_core STATIC
  mat4.cpp
  ecs.cpp
  entities.cpp
  entities_dxf.cpp
  entities_draw.cpp
  entities_grip.cpp
  dash.cpp
  polyline.cpp
  spline.cpp
  faces.cpp
  text.cpp
  mtext.cpp
  font_hershey.cpp
  proxy.cpp
  insert.cpp
  curve_edit.cpp
  intersect.cpp
  ucs.cpp
  osnap_derived.cpp
  selection.cpp
  sysvar.cpp
  undo.cpp
  pick.cpp
  osnap_search.cpp
  render.cpp
  scene.cpp
  viewport.cpp
  database.cpp
  dxf.cpp
  dxf_read.cpp
  offset.cpp
  corner.cpp
  clipboard.cpp
  command.cpp
  commands.cpp
  about.cpp
  host.cpp
  paths.cpp
  input_text.cpp
)

target_include_directories(ncad_core PUBLIC ${PROJECT_SOURCE_DIR}/include ${NCAD_VERSION_DIR})
# The generated version header. PUBLIC so ncad and the tests can name the build
# they are, and so the command-count convention can be asserted rather than
# merely written down.
add_dependencies(ncad_core ncad_version)
target_link_libraries(ncad_core PRIVATE ncad_flags)

add_library(ncad::core ALIAS ncad_core)
