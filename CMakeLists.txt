# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes

cmake_minimum_required(VERSION 3.20)
# The patch number is the COMMAND COUNT -- see include/noto/version.hpp.in.
project(NotoCAD VERSION 0.0.53 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(NOTO_BUILD_GUI  "Build the Qt6 GUI shell"          OFF)
option(NOTO_WITH_DWG   "Enable DWG import via LibreDWG"   OFF)
option(NOTO_BUILD_TESTS "Build the test suite"            ON)

# Restrained-dialect enforcement: no RTTI, warnings are loud.
add_library(noto_flags INTERFACE)
target_compile_options(noto_flags INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion;-fno-rtti>
  $<$<CXX_COMPILER_ID:MSVC>:/W4;/GR->
)

# Version and git hash, regenerated at build time so the stamp cannot go stale.
set(NOTO_VERSION_DIR ${CMAKE_BINARY_DIR}/generated)
set(NOTO_VERSION_HEADER ${NOTO_VERSION_DIR}/noto/version.hpp)
set(NOTO_VERSION_ARGS
  -DSRC=${PROJECT_SOURCE_DIR}
  -DIN=${PROJECT_SOURCE_DIR}/include/noto/version.hpp.in
  -DOUT=${NOTO_VERSION_HEADER}
  -DNOTO_VERSION=${PROJECT_VERSION})

# Once now, so the header exists for the very first configure of a target that
# includes it, and again on every build to keep the hash current.
execute_process(COMMAND ${CMAKE_COMMAND} ${NOTO_VERSION_ARGS}
                        -P ${PROJECT_SOURCE_DIR}/cmake/write_version.cmake)
add_custom_target(noto_version ALL
  COMMAND ${CMAKE_COMMAND} ${NOTO_VERSION_ARGS}
                           -P ${PROJECT_SOURCE_DIR}/cmake/write_version.cmake
  BYPRODUCTS ${NOTO_VERSION_HEADER}
  COMMENT "Recording version and git hash")

add_subdirectory(src/core)
add_subdirectory(src/lisp)
add_subdirectory(src/app)
add_subdirectory(tools)

# After tools/: the viewer renders the sample drawing defined there.
if(NOTO_BUILD_GUI)
  add_subdirectory(src/gui)
endif()

if(NOTO_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "NotoCAD ${PROJECT_VERSION}  build=${CMAKE_BUILD_TYPE}  gui=${NOTO_BUILD_GUI}  dwg=${NOTO_WITH_DWG}")
