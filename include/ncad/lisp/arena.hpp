// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Sadie Forbes

// Bump allocator for interpreter data.
//
// The intended workload is AutoLISP generating meshes from external analysis
// data -- tens of thousands of faces, each arriving as an alist of dotted pairs.
// Individually malloc'ing cons cells at that rate is the wrong shape, so cells,
// strings and symbols all come from here and are released in one stroke.
//
// The arena never runs destructors. Everything allocated from it must be
// trivially destructible, which is enforced by static_assert in make<T>().
#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>
#include <vector>

namespace ncad::lisp {

class Arena {
public:
    static constexpr std::size_t kDefaultBlockSize = 64 * 1024;

    explicit Arena(std::size_t block_size = kDefaultBlockSize) : block_size_(block_size) {}

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    void* alloc(std::size_t bytes, std::size_t align);

    template <typename T, typename... Args>
    T* make(Args&&... args) {
        static_assert(std::is_trivially_destructible_v<T>,
                      "arena allocations are never destructed");
        void* p = alloc(sizeof(T), alignof(T));
        return new (p) T{std::forward<Args>(args)...};
    }

    // Copies a byte range into the arena and returns the copy.
    void* copy(const void* src, std::size_t bytes, std::size_t align);

    // Releases every block. All pointers previously handed out are invalidated.
    void reset();

    std::size_t bytes_used() const { return used_; }
    std::size_t bytes_reserved() const;
    std::size_t block_count() const { return blocks_.size(); }

private:
    struct Block {
        std::unique_ptr<std::byte[]> data;
        std::size_t size{0};
        std::size_t used{0};
    };

    void add_block(std::size_t min_size);

    std::vector<Block> blocks_;
    std::size_t block_size_;
    std::size_t used_{0};
};

}  // namespace ncad::lisp
