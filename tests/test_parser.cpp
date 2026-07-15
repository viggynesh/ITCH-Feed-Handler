#include <gtest/gtest.h>

#include <cstring>

#include "itch/byteswap.hpp"
#include "itch/messages.hpp"

using namespace itch;

TEST(ByteSwap, RoundTrip16) {
    uint8_t b[2];
    store_be16(b, 0xABCD);
    EXPECT_EQ(b[0], 0xAB);
    EXPECT_EQ(b[1], 0xCD);
    EXPECT_EQ(load_be16(b), 0xABCDu);
}

TEST(ByteSwap, RoundTrip32) {
    uint8_t b[4];
    store_be32(b, 0x01020304);
    EXPECT_EQ(b[0], 0x01);
    EXPECT_EQ(b[3], 0x04);
    EXPECT_EQ(load_be32(b), 0x01020304u);
}

TEST(ByteSwap, RoundTrip48) {
    uint8_t b[6];
    const uint64_t v = 0x0000112233445566ull & 0xFFFFFFFFFFFFull;
    store_be48(b, v);
    EXPECT_EQ(load_be48(b), v);
}

TEST(ByteSwap, RoundTrip64) {
    uint8_t b[8];
    store_be64(b, 0x0102030405060708ull);
    EXPECT_EQ(load_be64(b), 0x0102030405060708ull);
}

// Struct sizes are also static_asserted in the header; assert here too so a
// mistake shows up as a readable test failure, not just a build break.
TEST(Messages, WireSizes) {
    EXPECT_EQ(sizeof(WireSystemEvent), 12u);
    EXPECT_EQ(sizeof(WireStockDirectory), 39u);
    EXPECT_EQ(sizeof(WireAddOrder), 36u);
    EXPECT_EQ(sizeof(WireAddOrderMPID), 40u);
    EXPECT_EQ(sizeof(WireOrderExecuted), 31u);
    EXPECT_EQ(sizeof(WireOrderExecutedPrice), 36u);
    EXPECT_EQ(sizeof(WireOrderCancel), 23u);
    EXPECT_EQ(sizeof(WireOrderDelete), 19u);
    EXPECT_EQ(sizeof(WireOrderReplace), 35u);
    EXPECT_EQ(sizeof(WireTradeNonCross), 44u);
    EXPECT_EQ(sizeof(WireTradeCross), 40u);
}

TEST(Messages, ExpectedLengths) {
    EXPECT_EQ(expected_length('A'), 36);
    EXPECT_EQ(expected_length('F'), 40);
    EXPECT_EQ(expected_length('E'), 31);
    EXPECT_EQ(expected_length('C'), 36);
    EXPECT_EQ(expected_length('X'), 23);
    EXPECT_EQ(expected_length('D'), 19);
    EXPECT_EQ(expected_length('U'), 35);
    EXPECT_EQ(expected_length('Z'), -1);  // unknown
}

TEST(Decode, AddOrder) {
    uint8_t buf[36] = {0};
    buf[0] = 'A';
    store_be16(buf + 1, 7);            // stock locate
    store_be48(buf + 5, 123456789);    // timestamp
    store_be64(buf + 11, 42);          // order ref
    buf[19] = 'B';                     // buy
    store_be32(buf + 20, 500);         // shares
    std::memcpy(buf + 24, "AAPL    ", 8);
    store_be32(buf + 32, 1500000);     // price = $150.0000

    AddOrder m = decode_add_order(buf);
    EXPECT_EQ(m.stock_locate, 7u);
    EXPECT_EQ(m.timestamp, 123456789u);
    EXPECT_EQ(m.order_ref, 42u);
    EXPECT_EQ(m.side, Side::Buy);
    EXPECT_EQ(m.shares, 500u);
    EXPECT_EQ(m.price, 1500000u);
    EXPECT_EQ(std::memcmp(m.stock, "AAPL    ", 8), 0);
}

TEST(Decode, AddOrderSellSide) {
    uint8_t buf[36] = {0};
    buf[0] = 'A';
    buf[19] = 'S';
    AddOrder m = decode_add_order(buf);
    EXPECT_EQ(m.side, Side::Sell);
}

TEST(Decode, OrderExecuted) {
    uint8_t buf[31] = {0};
    buf[0] = 'E';
    store_be16(buf + 1, 3);
    store_be64(buf + 11, 99);
    store_be32(buf + 19, 250);
    OrderExecuted m = decode_order_executed(buf);
    EXPECT_EQ(m.stock_locate, 3u);
    EXPECT_EQ(m.order_ref, 99u);
    EXPECT_EQ(m.executed_shares, 250u);
}

TEST(Decode, OrderCancel) {
    uint8_t buf[23] = {0};
    buf[0] = 'X';
    store_be64(buf + 11, 77);
    store_be32(buf + 19, 33);
    OrderCancel m = decode_order_cancel(buf);
    EXPECT_EQ(m.order_ref, 77u);
    EXPECT_EQ(m.cancelled_shares, 33u);
}

TEST(Decode, OrderDelete) {
    uint8_t buf[19] = {0};
    buf[0] = 'D';
    store_be64(buf + 11, 555);
    OrderDelete m = decode_order_delete(buf);
    EXPECT_EQ(m.order_ref, 555u);
}

TEST(Decode, OrderReplace) {
    uint8_t buf[35] = {0};
    buf[0] = 'U';
    store_be64(buf + 11, 10);   // original
    store_be64(buf + 19, 20);   // new
    store_be32(buf + 27, 900);  // shares
    store_be32(buf + 31, 1000200);
    OrderReplace m = decode_order_replace(buf);
    EXPECT_EQ(m.original_order_ref, 10u);
    EXPECT_EQ(m.new_order_ref, 20u);
    EXPECT_EQ(m.shares, 900u);
    EXPECT_EQ(m.price, 1000200u);
}
