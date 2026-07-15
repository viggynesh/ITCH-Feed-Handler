#include "itch/optimized_book.hpp"

#include <algorithm>

namespace itch {

namespace {
// Map a price to either a flat-array slot (fast path) or an overflow map entry
// (sub-penny or out-of-window). Returns the level plus its array index (-1 for
// overflow). The returned Level address is stable for the order's lifetime.
struct LevelRef {
    Level* level;
    int32_t index;  // -1 => overflow
};

LevelRef resolve_level(SymbolBook& s, Side side, Ticks price) {
    const bool penny_aligned = (price % kTicksPerPenny) == 0;
    const Ticks penny = price / kTicksPerPenny;
    if (penny_aligned && penny >= 0 && penny < static_cast<Ticks>(kNumPennyLevels)) {
        std::vector<Level>& arr = (side == Side::Buy) ? s.bids : s.asks;
        return {&arr[static_cast<size_t>(penny)], static_cast<int32_t>(penny)};
    }
    std::map<Ticks, Level>& ov = (side == Side::Buy) ? s.bid_overflow : s.ask_overflow;
    return {&ov[price], -1};
}
}  // namespace

OptimizedBook::OptimizedBook(std::vector<uint16_t> tracked_locates, size_t pool_reserve)
    : tracked_locates_(std::move(tracked_locates)), pool_(pool_reserve), id_map_(1u << 20) {
    uint16_t max_locate = 0;
    for (uint16_t l : tracked_locates_)
        max_locate = std::max(max_locate, l);
    tracked_flag_.assign(static_cast<size_t>(max_locate) + 1, false);
    for (uint16_t l : tracked_locates_)
        tracked_flag_[l] = true;
}

SymbolBook& OptimizedBook::book_for(uint16_t locate) {
    auto it = books_.find(locate);
    if (it == books_.end()) {
        auto sb = std::make_unique<SymbolBook>();
        sb->locate = locate;
        it = books_.emplace(locate, std::move(sb)).first;
    }
    return *it->second;
}

void OptimizedBook::add_order(SymbolBook& s, uint64_t ref, Side side, Ticks price,
                              uint32_t shares) {
    LevelRef lr = resolve_level(s, side, price);
    Level* lvl = lr.level;

    OrderNode* n = pool_.allocate();
    n->ref = ref;
    n->prev = lvl->tail;
    n->next = nullptr;
    n->level = lvl;
    n->sym = &s;
    n->tick = price;
    n->level_index = lr.index;
    n->shares = shares;
    n->side = side;

    if (lvl->tail)
        lvl->tail->next = n;
    else
        lvl->head = n;
    lvl->tail = n;
    lvl->qty += shares;
    ++lvl->count;

    if (lr.index >= 0) {
        if (side == Side::Buy) {
            if (lr.index > s.best_bid_idx)
                s.best_bid_idx = lr.index;
        } else {
            if (s.best_ask_idx < 0 || lr.index < s.best_ask_idx)
                s.best_ask_idx = lr.index;
        }
    }

    id_map_.insert(ref, n);
}

void OptimizedBook::on_add(const AddOrder& a) {
    if (!is_tracked(a.stock_locate))
        return;
    add_order(book_for(a.stock_locate), a.order_ref, a.side, static_cast<Ticks>(a.price), a.shares);
}

void OptimizedBook::apply_reduce(uint64_t ref, uint32_t qty, bool full) {
    OrderNode* n = id_map_.find(ref);
    if (n == nullptr)
        return;  // untracked symbol or already gone

    const uint32_t remove_qty = full ? n->shares : std::min(qty, n->shares);
    Level* lvl = n->level;
    lvl->qty -= remove_qty;

    if (!full && remove_qty != n->shares) {
        n->shares -= remove_qty;  // partial fill / cancel, order stays resting
        return;
    }

    // Order fully gone: splice out of the intrusive FIFO in O(1).
    if (n->prev)
        n->prev->next = n->next;
    else
        lvl->head = n->next;
    if (n->next)
        n->next->prev = n->prev;
    else
        lvl->tail = n->prev;
    --lvl->count;

    // Capture what we need before the node returns to the pool.
    SymbolBook* s = n->sym;
    const Side side = n->side;
    const int32_t idx = n->level_index;
    const Ticks tick = n->tick;

    id_map_.erase(ref);
    pool_.deallocate(n);

    if (lvl->count != 0)
        return;  // level still has other resting orders

    if (idx >= 0) {
        // Flat-array level emptied: nudge the best-bid/ask cursor if it moved.
        if (side == Side::Buy && idx == s->best_bid_idx) {
            int64_t k = idx - 1;
            while (k >= 0 && s->bids[static_cast<size_t>(k)].count == 0)
                --k;
            s->best_bid_idx = k;
        } else if (side == Side::Sell && idx == s->best_ask_idx) {
            int64_t k = idx + 1;
            const int64_t n_levels = static_cast<int64_t>(kNumPennyLevels);
            while (k < n_levels && s->asks[static_cast<size_t>(k)].count == 0)
                ++k;
            s->best_ask_idx = (k < n_levels) ? k : -1;
        }
    } else {
        // Overflow level emptied: drop the map node (nothing points at it now).
        if (side == Side::Buy)
            s->bid_overflow.erase(tick);
        else
            s->ask_overflow.erase(tick);
    }
}

void OptimizedBook::on_execute(const OrderExecuted& e) {
    apply_reduce(e.order_ref, e.executed_shares, false);
}

void OptimizedBook::on_execute_price(const OrderExecuted& e) {
    apply_reduce(e.order_ref, e.executed_shares, false);
}

void OptimizedBook::on_cancel(const OrderCancel& x) {
    apply_reduce(x.order_ref, x.cancelled_shares, false);
}

void OptimizedBook::on_delete(const OrderDelete& d) {
    apply_reduce(d.order_ref, 0, true);
}

void OptimizedBook::on_replace(const OrderReplace& u) {
    OrderNode* orig = id_map_.find(u.original_order_ref);
    if (orig == nullptr)
        return;
    SymbolBook* s = orig->sym;
    const Side side = orig->side;
    apply_reduce(u.original_order_ref, 0, true);
    add_order(*s, u.new_order_ref, side, static_cast<Ticks>(u.price), u.shares);
}

// ---- read side -------------------------------------------------------------

TopOfBook OptimizedBook::top_of_book(uint16_t locate) const {
    TopOfBook tob;
    auto it = books_.find(locate);
    if (it == books_.end())
        return tob;
    const SymbolBook& s = *it->second;

    // best bid = max price; consider the array cursor and any overflow entry.
    if (s.best_bid_idx >= 0) {
        tob.has_bid = true;
        tob.bid_px = s.best_bid_idx * kTicksPerPenny;
        tob.bid_qty = s.bids[static_cast<size_t>(s.best_bid_idx)].qty;
    }
    if (!s.bid_overflow.empty()) {
        const auto& top = *s.bid_overflow.rbegin();
        if (!tob.has_bid || top.first > tob.bid_px) {
            tob.has_bid = true;
            tob.bid_px = top.first;
            tob.bid_qty = top.second.qty;
        }
    }

    // best ask = min price.
    if (s.best_ask_idx >= 0) {
        tob.has_ask = true;
        tob.ask_px = s.best_ask_idx * kTicksPerPenny;
        tob.ask_qty = s.asks[static_cast<size_t>(s.best_ask_idx)].qty;
    }
    if (!s.ask_overflow.empty()) {
        const auto& top = *s.ask_overflow.begin();
        if (!tob.has_ask || top.first < tob.ask_px) {
            tob.has_ask = true;
            tob.ask_px = top.first;
            tob.ask_qty = top.second.qty;
        }
    }
    return tob;
}

std::vector<LevelView> OptimizedBook::top_n(uint16_t locate, Side side, size_t n) const {
    std::vector<LevelView> out;
    auto it = books_.find(locate);
    if (it == books_.end())
        return out;
    const SymbolBook& s = *it->second;

    if (side == Side::Buy) {
        for (int64_t k = s.best_bid_idx; k >= 0 && out.size() < n; --k) {
            const Level& lvl = s.bids[static_cast<size_t>(k)];
            if (lvl.count)
                out.push_back({k * kTicksPerPenny, lvl.qty, lvl.count});
        }
        for (auto rit = s.bid_overflow.rbegin(); rit != s.bid_overflow.rend(); ++rit)
            out.push_back({rit->first, rit->second.qty, rit->second.count});
        std::sort(out.begin(), out.end(),
                  [](const LevelView& a, const LevelView& b) { return a.price > b.price; });
    } else {
        for (int64_t k = s.best_ask_idx;
             k >= 0 && k < static_cast<int64_t>(kNumPennyLevels) && out.size() < n; ++k) {
            const Level& lvl = s.asks[static_cast<size_t>(k)];
            if (lvl.count)
                out.push_back({k * kTicksPerPenny, lvl.qty, lvl.count});
        }
        for (const auto& [tick, lvl] : s.ask_overflow)
            out.push_back({tick, lvl.qty, lvl.count});
        std::sort(out.begin(), out.end(),
                  [](const LevelView& a, const LevelView& b) { return a.price < b.price; });
    }
    if (out.size() > n)
        out.resize(n);
    return out;
}

uint64_t OptimizedBook::total_resting_qty(uint16_t locate) const {
    auto it = books_.find(locate);
    if (it == books_.end())
        return 0;
    const SymbolBook& s = *it->second;
    uint64_t total = 0;
    for (const Level& l : s.bids)
        total += l.qty;
    for (const Level& l : s.asks)
        total += l.qty;
    for (const auto& [tick, l] : s.bid_overflow)
        total += l.qty;
    for (const auto& [tick, l] : s.ask_overflow)
        total += l.qty;
    return total;
}

}  // namespace itch
