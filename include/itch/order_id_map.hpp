#pragma once
// Open-addressing hash map: order reference number -> order node.
//
// std::unordered_map would work, but every insert/erase touches the heap (one
// node allocation per order) and chases a pointer to a separately-allocated
// bucket node - death by cache miss on a feed doing millions of adds/cancels.
// This is a flat, power-of-two, linear-probing table living in one contiguous
// vector. Insert/find/erase are cache-friendly, and erase uses backward-shift
// deletion so there are no tombstones to degrade probe chains over a full day.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace itch {

struct OrderNode;  // defined in optimized_book.hpp

class OrderIdMap {
public:
    explicit OrderIdMap(size_t initial_capacity_pow2 = (1u << 20)) {
        size_t cap = 1;
        while (cap < initial_capacity_pow2)
            cap <<= 1;
        slots_.assign(cap, Slot{kEmpty, nullptr});
        mask_ = cap - 1;
    }

    OrderNode* find(uint64_t key) const noexcept {
        size_t i = index_of(key);
        while (slots_[i].key != kEmpty) {
            if (slots_[i].key == key)
                return slots_[i].val;
            i = (i + 1) & mask_;
        }
        return nullptr;
    }

    // Precondition: key not already present (true for unique ITCH order refs).
    void insert(uint64_t key, OrderNode* val) {
        if ((size_ + 1) * 10 >= (mask_ + 1) * 7)  // keep load factor under 0.7
            grow();
        size_t i = index_of(key);
        while (slots_[i].key != kEmpty)
            i = (i + 1) & mask_;
        slots_[i] = Slot{key, val};
        ++size_;
    }

    bool erase(uint64_t key) noexcept {
        size_t i = index_of(key);
        while (slots_[i].key != kEmpty) {
            if (slots_[i].key == key)
                break;
            i = (i + 1) & mask_;
        }
        if (slots_[i].key == kEmpty)
            return false;

        // Backward-shift deletion (Knuth 6.4 R). Pull following entries back
        // into the hole while doing so doesn't break their probe chain.
        size_t hole = i;
        size_t j = i;
        for (;;) {
            j = (j + 1) & mask_;
            if (slots_[j].key == kEmpty)
                break;
            size_t home = index_of(slots_[j].key);
            // displacement of j from home vs. distance from hole to j
            if (((j - home) & mask_) >= ((j - hole) & mask_)) {
                slots_[hole] = slots_[j];
                hole = j;
            }
        }
        slots_[hole] = Slot{kEmpty, nullptr};
        --size_;
        return true;
    }

    size_t size() const noexcept { return size_; }
    size_t capacity() const noexcept { return mask_ + 1; }

private:
    struct Slot {
        uint64_t key;
        OrderNode* val;
    };
    static constexpr uint64_t kEmpty = ~uint64_t{0};

    size_t index_of(uint64_t key) const noexcept {
        // Fibonacci hashing - cheap and spreads sequential order refs well.
        uint64_t h = key * 0x9E3779B97F4A7C15ull;
        return static_cast<size_t>(h >> 40) & mask_;
    }

    void grow() {
        std::vector<Slot> old = std::move(slots_);
        size_t new_cap = (mask_ + 1) << 1;
        slots_.assign(new_cap, Slot{kEmpty, nullptr});
        mask_ = new_cap - 1;
        size_ = 0;
        for (const Slot& s : old)
            if (s.key != kEmpty)
                insert(s.key, s.val);
    }

    std::vector<Slot> slots_;
    size_t mask_ = 0;
    size_t size_ = 0;
};

}  // namespace itch
