# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes
#
# Writes version.hpp with the current git hash. Run at build time, not configure
# time: a hash captured once at configure would be wrong the moment anything is
# committed, and a version stamp that lies is worse than none.
execute_process(
  COMMAND git -C "${SRC}" rev-parse --short HEAD
  OUTPUT_VARIABLE NOTO_GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)

if(NOT NOTO_GIT_HASH)
  set(NOTO_GIT_HASH "unknown")
endif()

# configure_file only rewrites when the content changes, so this does not force
# a rebuild of everything that includes the header on every build.
configure_file("${IN}" "${OUT}" @ONLY)
