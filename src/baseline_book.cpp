#include "itch/baseline_book.hpp"

#include <algorithm>

namespace itch {

BaselineBook::BaselineBook(std::vector<uint16_t> tracked_locates)
    : tracked_locates_(std::move(tracked_locates)) {
    uint16_t max_locate = 0;
    for (uint16_t l : tracked_locates_)
        max_locate = std::max(max_locate, l);
    tracked_flag_.assign(static_cast<size_t>(max_locate) + 1, false);
    for (uint16_t l : tracked_locates_)
        tracked_flag_[l] = true;
    orders_.reserve(1u << 20);
}

void BaselineBook::on_add(const AddOrder& a) {
    if (!is_tracked(a.stock_locate))
        return;
    orders_[a.order_ref] = OrderRec{a.stock_locate, a.side, static_cast<Ticks>(a.price), a.shares};
    SymbolBook& b = book_for(a.stock_locate);
    if (a.side == Side::Buy) {
        LevelAgg& lvl = b.bids[static_cast<Ticks>(a.price)];
        lvl.qty += a.shares;
        ++lvl.count;
    } else {
        LevelAgg& lvl = b.asks[static_cast<Ticks>(a.price)];
        lvl.qty += a.shares;
        ++lvl.count;
    }
}

void BaselineBook::apply_reduce(uint64_t ref, uint32_t qty, bool full) {
    auto it = orders_.find(ref);
    if (it == orders_.end())
        return;  // order belongs to an untracked symbol (or already gone)
    OrderRec& r = it->second;
    const uint32_t remove_qty = full ? r.shares : std::min(qty, r.shares);
    const bool order_gone = full || (remove_qty == r.shares);

    SymbolBook& b = book_for(r.locate);
    if (r.side == Side::Buy) {
        auto lit = b.bids.find(r.tick);
        if (lit != b.bids.end()) {
            lit->second.qty -= remove_qty;
            if (order_gone)
                --lit->second.count;
            if (lit->second.qty == 0)
                b.bids.erase(lit);
        }
    } else {
        auto lit = b.asks.find(r.tick);
        if (lit != b.asks.end()) {
            lit->second.qty -= remove_qty;
            if (order_gone)
                --lit->second.count;
            if (lit->second.qty == 0)
                b.asks.erase(lit);
        }
    }

    if (order_gone)
        orders_.erase(it);
    else
        r.shares -= remove_qty;
}

void BaselineBook::on_execute(const OrderExecuted& e) {
    apply_reduce(e.order_ref, e.executed_shares, false);
}

void BaselineBook::on_execute_price(const OrderExecuted& e) {
    apply_reduce(e.order_ref, e.executed_shares, false);
}

void BaselineBook::on_cancel(const OrderCancel& x) {
    apply_reduce(x.order_ref, x.cancelled_shares, false);
}

void BaselineBook::on_delete(const OrderDelete& d) {
    apply_reduce(d.order_ref, 0, true);
}

void BaselineBook::on_replace(const OrderReplace& u) {
    auto it = orders_.find(u.original_order_ref);
    if (it == orders_.end())
        return;
    const uint16_t locate = it->second.locate;
    const Side side = it->second.side;
    // Cancel the original, then add the replacement as a brand new order.
    apply_reduce(u.original_order_ref, 0, true);
    orders_[u.new_order_ref] = OrderRec{locate, side, static_cast<Ticks>(u.price), u.shares};
    SymbolBook& b = book_for(locate);
    if (side == Side::Buy) {
        LevelAgg& lvl = b.bids[static_cast<Ticks>(u.price)];
        lvl.qty += u.shares;
        ++lvl.count;
    } else {
        LevelAgg& lvl = b.asks[static_cast<Ticks>(u.price)];
        lvl.qty += u.shares;
        ++lvl.count;
    }
}

TopOfBook BaselineBook::top_of_book(uint16_t locate) const {
    TopOfBook tob;
    auto it = books_.find(locate);
    if (it == books_.end())
        return tob;
    const SymbolBook& b = it->second;
    if (!b.bids.empty()) {
        tob.has_bid = true;
        tob.bid_px = b.bids.begin()->first;
        tob.bid_qty = b.bids.begin()->second.qty;
    }
    if (!b.asks.empty()) {
        tob.has_ask = true;
        tob.ask_px = b.asks.begin()->first;
        tob.ask_qty = b.asks.begin()->second.qty;
    }
    return tob;
}

std::vector<LevelView> BaselineBook::top_n(uint16_t locate, Side side, size_t n) const {
    std::vector<LevelView> out;
    auto it = books_.find(locate);
    if (it == books_.end())
        return out;
    const SymbolBook& b = it->second;
    out.reserve(n);
    if (side == Side::Buy) {
        for (const auto& [tick, lvl] : b.bids) {
            if (out.size() >= n)
                break;
            out.push_back({tick, lvl.qty, lvl.count});
        }
    } else {
        for (const auto& [tick, lvl] : b.asks) {
            if (out.size() >= n)
                break;
            out.push_back({tick, lvl.qty, lvl.count});
        }
    }
    return out;
}

uint64_t BaselineBook::total_resting_qty(uint16_t locate) const {
    auto it = books_.find(locate);
    if (it == books_.end())
        return 0;
    uint64_t total = 0;
    for (const auto& [tick, lvl] : it->second.bids)
        total += lvl.qty;
    for (const auto& [tick, lvl] : it->second.asks)
        total += lvl.qty;
    return total;
}

}  // namespace itch
