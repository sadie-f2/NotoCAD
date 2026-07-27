# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes

add_executable(noto_gen_sample gen_sample.cpp)
target_link_libraries(noto_gen_sample PRIVATE noto_core noto_flags)
