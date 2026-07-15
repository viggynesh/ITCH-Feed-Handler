#include <gtest/gtest.h>

#include "itch/baseline_book.hpp"
#include "itch/optimized_book.hpp"

using namespace itch;

namespace {

AddOrder mk_add(uint16_t locate, uint64_t ref, Side side, uint32_t shares, uint32_t price) {
    AddOrder a{};
    a.stock_locate = locate;
    a.order_ref = ref;
    a.side = side;
    a.shares = shares;
    a.price = price;
    return a;
}

// The same scripted add/execute/cancel/delete/replace scenario is run against
// both book implementations; every assertion must hold identically.
template <class Book>
void run_scenario() {
    Book b({1});  // track locate 1

    // 1) resting buy 100 @ 100.0000
    b.on_add(mk_add(1, /*ref*/ 1, Side::Buy, 100, 1000000));
    {
        TopOfBook t = b.top_of_book(1);
        EXPECT_TRUE(t.has_bid);
        EXPECT_FALSE(t.has_ask);
        EXPECT_EQ(t.bid_px, 1000000);
        EXPECT_EQ(t.bid_qty, 100u);
        EXPECT_EQ(b.total_resting_qty(1), 100u);
    }

    // 2) resting buy 200 @ 99.9900 (worse bid, top unchanged)
    b.on_add(mk_add(1, 2, Side::Buy, 200, 999900));
    {
        TopOfBook t = b.top_of_book(1);
        EXPECT_EQ(t.bid_px, 1000000);
        EXPECT_EQ(t.bid_qty, 100u);
        EXPECT_EQ(b.total_resting_qty(1), 300u);
    }

    // 3) resting sell 300 @ 100.0100 sets the ask
    b.on_add(mk_add(1, 3, Side::Sell, 300, 1000100));
    {
        TopOfBook t = b.top_of_book(1);
        EXPECT_TRUE(t.has_ask);
        EXPECT_EQ(t.ask_px, 1000100);
        EXPECT_EQ(t.ask_qty, 300u);
        EXPECT_EQ(b.total_resting_qty(1), 600u);
    }

    // 4) another buy 50 @ 100.0000 joins the top level (FIFO)
    b.on_add(mk_add(1, 4, Side::Buy, 50, 1000000));
    {
        TopOfBook t = b.top_of_book(1);
        EXPECT_EQ(t.bid_px, 1000000);
        EXPECT_EQ(t.bid_qty, 150u);
        auto bids = b.top_n(1, Side::Buy, 10);
        ASSERT_GE(bids.size(), 1u);
        EXPECT_EQ(bids[0].price, 1000000);
        EXPECT_EQ(bids[0].qty, 150u);
        EXPECT_EQ(bids[0].orders, 2u);
        EXPECT_EQ(b.total_resting_qty(1), 650u);
    }

    // 5) execute 40 of ref1 -> top level qty 150-40 = 110
    b.on_execute(OrderExecuted{1, 0, /*ref*/ 1, 40});
    {
        TopOfBook t = b.top_of_book(1);
        EXPECT_EQ(t.bid_qty, 110u);
        EXPECT_EQ(b.total_resting_qty(1), 610u);
    }

    // 6) cancel all 200 of ref2 -> its level disappears
    b.on_cancel(OrderCancel{1, 0, /*ref*/ 2, 200});
    {
        auto bids = b.top_n(1, Side::Buy, 10);
        ASSERT_EQ(bids.size(), 1u);  // only the 100.0000 level remains
        EXPECT_EQ(bids[0].price, 1000000);
        EXPECT_EQ(bids[0].qty, 110u);
        EXPECT_EQ(b.total_resting_qty(1), 410u);
    }

    // 7) delete ref3 -> ask side empties
    b.on_delete(OrderDelete{1, 0, /*ref*/ 3});
    {
        TopOfBook t = b.top_of_book(1);
        EXPECT_FALSE(t.has_ask);
        EXPECT_EQ(b.total_resting_qty(1), 110u);
    }

    // 8) replace ref1 (60 left) with ref5, 500 @ 100.0200
    b.on_replace(OrderReplace{1, 0, /*orig*/ 1, /*new*/ 5, 500, 1000200});
    {
        TopOfBook t = b.top_of_book(1);
        EXPECT_TRUE(t.has_bid);
        EXPECT_EQ(t.bid_px, 1000200);
        EXPECT_EQ(t.bid_qty, 500u);
        auto bids = b.top_n(1, Side::Buy, 10);
        ASSERT_EQ(bids.size(), 2u);
        EXPECT_EQ(bids[0].price, 1000200);  // ref5
        EXPECT_EQ(bids[0].qty, 500u);
        EXPECT_EQ(bids[1].price, 1000000);  // ref4 leftover
        EXPECT_EQ(bids[1].qty, 50u);
        EXPECT_EQ(b.total_resting_qty(1), 550u);
    }
}

}  // namespace

TEST(Book, BaselineScenario) { run_scenario<BaselineBook>(); }
TEST(Book, OptimizedScenario) { run_scenario<OptimizedBook>(); }

// Messages for untracked symbols must not affect a tracked book.
template <class Book>
void run_ignores_other_symbols() {
    Book b({1});
    b.on_add(mk_add(2, 100, Side::Buy, 999, 1000000));  // locate 2, not tracked
    EXPECT_EQ(b.total_resting_qty(1), 0u);
    b.on_execute(OrderExecuted{2, 0, 100, 10});  // no-op on our book
    EXPECT_EQ(b.total_resting_qty(1), 0u);
}

TEST(Book, BaselineIgnoresOtherSymbols) { run_ignores_other_symbols<BaselineBook>(); }
TEST(Book, OptimizedIgnoresOtherSymbols) { run_ignores_other_symbols<OptimizedBook>(); }
