# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes

add_library(noto_core STATIC
  mat4.cpp
  ecs.cpp
  entities.cpp
  entities_dxf.cpp
  database.cpp
  dxf.cpp
)

target_include_directories(noto_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(noto_core PRIVATE noto_flags)

add_library(noto::core ALIAS noto_core)
