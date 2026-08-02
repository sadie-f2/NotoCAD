# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes

add_library(ncad_lisp STATIC
  arena.cpp
  value.cpp
  reader.cpp
  eval.cpp
  builtins.cpp
  entity_subrs.cpp
  sset_subrs.cpp
  sysvar_subrs.cpp
  file_subrs.cpp
  interp_script_loader.cpp
  geom_subrs.cpp
  io_subrs.cpp
  wildcard.cpp
  table_subrs.cpp
  command_subr.cpp
  command_input.cpp
)

target_include_directories(ncad_lisp PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(ncad_lisp PUBLIC ncad_core)
target_link_libraries(ncad_lisp PRIVATE ncad_flags)

add_library(ncad::lisp ALIAS ncad_lisp)
