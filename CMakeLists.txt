# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes

cmake_minimum_required(VERSION 3.20)
project(NotoCAD VERSION 0.0.1 LANGUAGES CXX)

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

add_subdirectory(src/core)
add_subdirectory(src/lisp)
add_subdirectory(src/app)
add_subdirectory(tools)

if(NOTO_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "NotoCAD ${PROJECT_VERSION}  build=${CMAKE_BUILD_TYPE}  gui=${NOTO_BUILD_GUI}  dwg=${NOTO_WITH_DWG}")
