#pragma once
// Baseline order book. Deliberately the "obvious" implementation:
//   - price levels in a std::map (red-black tree), bids descending / asks
//     ascending, so best bid/ask is begin() and top-N is a short walk;
//   - resting orders in a std::unordered_map<ref, record>;
//   - a fresh heap node for every tree/hash insert.
//
// It is simple and easy to convince yourself it's correct, which is exactly why
// it's the reference the optimized book is diffed against. It is also where all
// the time goes: pointer-chasing tree nodes and malloc/free per order.

#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "book_types.hpp"
#include "messages.hpp"

namespace itch {

class BaselineBook {
public:
    explicit BaselineBook(std::vector<uint16_t> tracked_locates);

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

private:
    struct LevelAgg {
        uint64_t qty = 0;
        uint32_t count = 0;
    };
    struct OrderRec {
        uint16_t locate;
        Side side;
        Ticks tick;
        uint32_t shares;
    };
    struct SymbolBook {
        std::map<Ticks, LevelAgg, std::greater<Ticks>> bids;
        std::map<Ticks, LevelAgg, std::less<Ticks>> asks;
    };

    bool is_tracked(uint16_t locate) const noexcept {
        return locate < tracked_flag_.size() && tracked_flag_[locate];
    }
    SymbolBook& book_for(uint16_t locate) { return books_[locate]; }

    // Reduce a resting order by `qty` (or remove it entirely if full==true),
    // keeping the level aggregate and order map consistent.
    void apply_reduce(uint64_t ref, uint32_t qty, bool full);

    std::vector<uint16_t> tracked_locates_;
    std::vector<bool> tracked_flag_;
    std::unordered_map<uint16_t, SymbolBook> books_;
    std::unordered_map<uint64_t, OrderRec> orders_;
};

}  // namespace itch
