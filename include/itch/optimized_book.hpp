#pragma once
// Optimized order book.
//
// Four changes vs. the baseline, each targeting a specific cost:
//   1. Price levels live in a FLAT ARRAY indexed by price (in pennies), not a
//      red-black tree. Best bid/ask and depth walks are sequential array
//      accesses - cache-friendly and branch-predictable - instead of chasing
//      tree-node pointers. Best bid/ask are tracked with two cursor indices.
//   2. Each level owns an INTRUSIVE doubly-linked FIFO of orders, so cancel /
//      execute splice out in O(1) with no search.
//   3. Order lookup is a flat open-addressing hash map (OrderIdMap), not
//      std::unordered_map, so ref -> order is one cache line, not a heap hop.
//   4. Order nodes come from an OBJECT POOL, so the add/cancel hot path never
//      calls malloc/free.
//
// Prices are indexed in pennies (the display grid for >= $1 US equities). The
// rare sub-penny / out-of-window price falls back to a small std::map so the
// result stays byte-identical to the baseline; on a normal liquid symbol that
// fallback is never touched.

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include "book_types.hpp"
#include "messages.hpp"
#include "object_pool.hpp"
#include "order_id_map.hpp"

namespace itch {

constexpr Ticks kTicksPerPenny = 100;       // ITCH price has 4 implied decimals
constexpr size_t kNumPennyLevels = 1 << 16;  // covers $0.00 .. $655.35 per symbol

struct SymbolBook;

// Intrusive FIFO node. One order == one node from the pool.
struct OrderNode {
    uint64_t ref;
    OrderNode* prev;
    OrderNode* next;
    struct Level* level;  // level this order rests on (stable address)
    SymbolBook* sym;
    Ticks tick;         // resting price in ITCH ticks
    int32_t level_index;  // index into the flat side array, or -1 if in overflow
    uint32_t shares;
    Side side;
};

struct Level {
    uint64_t qty = 0;
    uint32_t count = 0;
    OrderNode* head = nullptr;
    OrderNode* tail = nullptr;
};

struct SymbolBook {
    uint16_t locate = 0;
    std::vector<Level> bids;  // index = price/penny, higher index = better bid
    std::vector<Level> asks;  // index = price/penny, lower index = better ask
    int64_t best_bid_idx = -1;
    int64_t best_ask_idx = -1;
    std::map<Ticks, Level> bid_overflow;  // sub-penny / out-of-window (rare)
    std::map<Ticks, Level> ask_overflow;

    SymbolBook() : bids(kNumPennyLevels), asks(kNumPennyLevels) {}
};

class OptimizedBook {
public:
    explicit OptimizedBook(std::vector<uint16_t> tracked_locates,
                           size_t pool_reserve = (1u << 20));

    // --- parser handler interface ---
    void on_system_event(char) noexcept {}
    void on_stock_directory(uint16_t, const char*) noexcept {}
    void on_add(const AddOrder& a);
    void on_execute(const OrderExecuted& e);
    void on_execute_price(const OrderExecuted& e);
    void on_cancel(const OrderCancel& x);
    void on_delete(const OrderDelete& d);
    void on_replace(const OrderReplace& u);
    void on_other(char) noexcept {}

    // --- read side ---
    TopOfBook top_of_book(uint16_t locate) const;
    std::vector<LevelView> top_n(uint16_t locate, Side side, size_t n) const;
    uint64_t total_resting_qty(uint16_t locate) const;
    const std::vector<uint16_t>& tracked() const { return tracked_locates_; }

    // introspection for the write-up
    size_t pool_live() const noexcept { return pool_.live(); }
    size_t pool_blocks() const noexcept { return pool_.blocks(); }

private:
    bool is_tracked(uint16_t locate) const noexcept {
        return locate < tracked_flag_.size() && tracked_flag_[locate];
    }
    SymbolBook& book_for(uint16_t locate);
    void add_order(SymbolBook& s, uint64_t ref, Side side, Ticks price, uint32_t shares);
    void apply_reduce(uint64_t ref, uint32_t qty, bool full);

    std::vector<uint16_t> tracked_locates_;
    std::vector<bool> tracked_flag_;
    std::unordered_map<uint16_t, std::unique_ptr<SymbolBook>> books_;
    ObjectPool<OrderNode> pool_;
    OrderIdMap id_map_;
};

}  // namespace itch
