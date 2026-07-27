# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes

add_library(noto_lisp STATIC
  arena.cpp
  value.cpp
  reader.cpp
  eval.cpp
  builtins.cpp
  entity_subrs.cpp
  file_subrs.cpp
)

target_include_directories(noto_lisp PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(noto_lisp PUBLIC noto_core)
target_link_libraries(noto_lisp PRIVATE noto_flags)

add_library(noto::lisp ALIAS noto_lisp)
