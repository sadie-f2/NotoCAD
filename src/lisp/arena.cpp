// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

#include "noto/lisp/arena.hpp"

#include <algorithm>
#include <cstring>
#include <new>

namespace noto::lisp {

void Arena::add_block(std::size_t min_size) {
    // Oversized requests get their own exactly-sized block rather than forcing
    // the standard block size upward.
    const std::size_t size = std::max(min_size, block_size_);
    Block b;
    b.data = std::make_unique<std::byte[]>(size);
    b.size = size;
    b.used = 0;
    blocks_.push_back(std::move(b));
}

void* Arena::alloc(std::size_t bytes, std::size_t align) {
    if (bytes == 0) bytes = 1;

    if (!blocks_.empty()) {
        Block& b = blocks_.back();
        const std::size_t base = reinterpret_cast<std::size_t>(b.data.get());
        const std::size_t cur = base + b.used;
        const std::size_t aligned = (cur + align - 1) & ~(align - 1);
        const std::size_t pad = aligned - cur;
        if (b.used + pad + bytes <= b.size) {
            b.used += pad + bytes;
            used_ += pad + bytes;
            return reinterpret_cast<void*>(aligned);
        }
    }

    add_block(bytes + align);
    Block& b = blocks_.back();
    const std::size_t base = reinterpret_cast<std::size_t>(b.data.get());
    const std::size_t aligned = (base + align - 1) & ~(align - 1);
    b.used = (aligned - base) + bytes;
    used_ += b.used;
    return reinterpret_cast<void*>(aligned);
}

void* Arena::copy(const void* src, std::size_t bytes, std::size_t align) {
    void* dst = alloc(bytes, align);
    std::memcpy(dst, src, bytes);
    return dst;
}

void Arena::reset() {
    blocks_.clear();
    used_ = 0;
}

std::size_t Arena::bytes_reserved() const {
    std::size_t total = 0;
    for (const Block& b : blocks_) total += b.size;
    return total;
}

}  // namespace noto::lisp
