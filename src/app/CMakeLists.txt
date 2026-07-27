# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes

add_executable(noto main.cpp)
target_link_libraries(noto PRIVATE noto_core noto_lisp noto_flags)
