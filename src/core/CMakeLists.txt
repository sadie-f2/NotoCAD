# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes

add_library(noto_core STATIC
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
  command.cpp
  commands.cpp
  input_text.cpp
)

target_include_directories(noto_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(noto_core PRIVATE noto_flags)

add_library(noto::core ALIAS noto_core)
