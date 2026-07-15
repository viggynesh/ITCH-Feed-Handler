#pragma once
// Fixed-block object pool. The hot path (add/cancel/replace) allocates and
// frees order nodes constantly; going through malloc/free for each one is the
// single biggest cost in the baseline. The pool hands back a pre-reserved slot
// in O(1) with no syscall and no locking.
//
// Freed slots are threaded into an intrusive free list (the free-list pointer
// lives *in* the freed slot's storage), so there is no side vector to grow.
// If the pool is exhausted it grabs one more block from the heap - that only
// happens when the live-order count climbs past the reserve, never per-op.

#include <cstddef>
#include <memory>
#include <type_traits>
#include <vector>

namespace itch {

template <class T>
class ObjectPool {
    static_assert(std::is_trivially_destructible_v<T>,
                  "pool never runs destructors; T must be trivially destructible");

    struct FreeNode {
        FreeNode* next;
    };
    static_assert(sizeof(T) >= sizeof(FreeNode), "T too small to thread a free list through");

public:
    explicit ObjectPool(size_t reserve_elems) : block_elems_(reserve_elems ? reserve_elems : 1024) {
        add_block(block_elems_);
    }

    // Grab an uninitialised slot. Caller assigns fields (T is a POD node).
    T* allocate() {
        if (free_head_ == nullptr) [[unlikely]]
            add_block(block_elems_);
        FreeNode* node = free_head_;
        free_head_ = node->next;
        ++live_;
        return reinterpret_cast<T*>(node);
    }

    void deallocate(T* p) noexcept {
        FreeNode* node = reinterpret_cast<FreeNode*>(p);
        node->next = free_head_;
        free_head_ = node;
        --live_;
    }

    size_t live() const noexcept { return live_; }
    size_t capacity() const noexcept { return capacity_; }
    size_t blocks() const noexcept { return blocks_.size(); }

private:
    void add_block(size_t n) {
        auto block = std::make_unique<T[]>(n);
        // Thread every element of the new block onto the free list.
        for (size_t i = 0; i < n; ++i) {
            FreeNode* node = reinterpret_cast<FreeNode*>(&block[i]);
            node->next = free_head_;
            free_head_ = node;
        }
        capacity_ += n;
        blocks_.push_back(std::move(block));
    }

    std::vector<std::unique_ptr<T[]>> blocks_;
    FreeNode* free_head_ = nullptr;
    size_t block_elems_;
    size_t live_ = 0;
    size_t capacity_ = 0;
};

}  // namespace itch
