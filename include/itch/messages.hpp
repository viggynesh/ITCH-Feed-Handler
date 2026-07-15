#pragma once
// NASDAQ TotalView-ITCH 5.0 message definitions.
//
// Two things live here:
//   1. #pragma pack(1) "wire" structs whose ONLY job is to pin down the exact
//      byte layout of the spec. I static_assert every size so a typo in an
//      offset fails at compile time instead of silently corrupting the book.
//      These are never read through directly (that would be wrong-endian) -
//      they document the layout and guard it.
//   2. Plain host-endian POD structs that the parser fills via load_be*. These
//      are what the order books actually consume.
//
// Field layouts are from the TotalView-ITCH 5.0 spec. Offsets are relative to
// the message type byte (offset 0).

#include <cstddef>
#include <cstdint>

#include "byteswap.hpp"

namespace itch {

// ---- message type characters ----------------------------------------------
namespace msg_type {
constexpr char SystemEvent = 'S';
constexpr char StockDirectory = 'R';
constexpr char TradingAction = 'H';
constexpr char RegSHO = 'Y';
constexpr char MarketParticipant = 'L';
constexpr char AddOrder = 'A';
constexpr char AddOrderMPID = 'F';
constexpr char OrderExecuted = 'E';
constexpr char OrderExecutedPrice = 'C';
constexpr char OrderCancel = 'X';
constexpr char OrderDelete = 'D';
constexpr char OrderReplace = 'U';
constexpr char TradeNonCross = 'P';
constexpr char TradeCross = 'Q';
}  // namespace msg_type

// ---- wire layout structs (size-checked, not read through) ------------------
#pragma pack(push, 1)

struct WireSystemEvent {
    char type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    char event_code;
};

struct WireStockDirectory {
    char type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    char stock[8];
    char market_category;
    char financial_status;
    uint32_t round_lot_size;
    char round_lots_only;
    char issue_classification;
    char issue_subtype[2];
    char authenticity;
    char short_sale_threshold;
    char ipo_flag;
    char luld_tier;
    char etp_flag;
    uint32_t etp_leverage_factor;
    char inverse_indicator;
};

struct WireAddOrder {
    char type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t order_ref;
    char buy_sell;
    uint32_t shares;
    char stock[8];
    uint32_t price;
};

struct WireAddOrderMPID {
    WireAddOrder base;
    char attribution[4];
};

struct WireOrderExecuted {
    char type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t order_ref;
    uint32_t executed_shares;
    uint64_t match_number;
};

struct WireOrderExecutedPrice {
    char type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t order_ref;
    uint32_t executed_shares;
    uint64_t match_number;
    char printable;
    uint32_t execution_price;
};

struct WireOrderCancel {
    char type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t order_ref;
    uint32_t cancelled_shares;
};

struct WireOrderDelete {
    char type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t order_ref;
};

struct WireOrderReplace {
    char type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t original_order_ref;
    uint64_t new_order_ref;
    uint32_t shares;
    uint32_t price;
};

struct WireTradeNonCross {
    char type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t order_ref;
    char buy_sell;
    uint32_t shares;
    char stock[8];
    uint32_t price;
    uint64_t match_number;
};

struct WireTradeCross {
    char type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t shares;
    char stock[8];
    uint32_t cross_price;
    uint64_t match_number;
    char cross_type;
};

#pragma pack(pop)

// Sizes straight from the spec. If any of these fail, a struct field is wrong.
static_assert(sizeof(WireSystemEvent) == 12);
static_assert(sizeof(WireStockDirectory) == 39);
static_assert(sizeof(WireAddOrder) == 36);
static_assert(sizeof(WireAddOrderMPID) == 40);
static_assert(sizeof(WireOrderExecuted) == 31);
static_assert(sizeof(WireOrderExecutedPrice) == 36);
static_assert(sizeof(WireOrderCancel) == 23);
static_assert(sizeof(WireOrderDelete) == 19);
static_assert(sizeof(WireOrderReplace) == 35);
static_assert(sizeof(WireTradeNonCross) == 44);
static_assert(sizeof(WireTradeCross) == 40);

// Expected payload length for each handled type (includes the type byte).
// Used by the parser as a sanity check against the framed length.
constexpr int expected_length(char type) noexcept {
    switch (type) {
        case msg_type::SystemEvent: return 12;
        case msg_type::StockDirectory: return 39;
        case msg_type::AddOrder: return 36;
        case msg_type::AddOrderMPID: return 40;
        case msg_type::OrderExecuted: return 31;
        case msg_type::OrderExecutedPrice: return 36;
        case msg_type::OrderCancel: return 23;
        case msg_type::OrderDelete: return 19;
        case msg_type::OrderReplace: return 35;
        case msg_type::TradeNonCross: return 44;
        case msg_type::TradeCross: return 40;
        default: return -1;  // unknown / not validated
    }
}

// ---- decoded, host-endian views passed to handlers -------------------------
enum class Side : uint8_t { Buy, Sell };

struct AddOrder {
    uint16_t stock_locate;
    uint64_t timestamp;
    uint64_t order_ref;
    Side side;
    uint32_t shares;
    char stock[8];
    uint32_t price;  // integer ticks, 4 implied decimals
};

struct OrderExecuted {
    uint16_t stock_locate;
    uint64_t timestamp;
    uint64_t order_ref;
    uint32_t executed_shares;
};

struct OrderCancel {
    uint16_t stock_locate;
    uint64_t timestamp;
    uint64_t order_ref;
    uint32_t cancelled_shares;
};

struct OrderDelete {
    uint16_t stock_locate;
    uint64_t timestamp;
    uint64_t order_ref;
};

struct OrderReplace {
    uint16_t stock_locate;
    uint64_t timestamp;
    uint64_t original_order_ref;
    uint64_t new_order_ref;
    uint32_t shares;
    uint32_t price;
};

// ---- decoders (read big-endian fields at their spec offsets) ---------------
inline AddOrder decode_add_order(const uint8_t* p) noexcept {
    AddOrder m;
    m.stock_locate = load_be16(p + 1);
    m.timestamp = load_be48(p + 5);
    m.order_ref = load_be64(p + 11);
    m.side = (p[19] == 'B') ? Side::Buy : Side::Sell;
    m.shares = load_be32(p + 20);
    for (int i = 0; i < 8; ++i)
        m.stock[i] = static_cast<char>(p[24 + i]);
    m.price = load_be32(p + 32);
    return m;
}

inline OrderExecuted decode_order_executed(const uint8_t* p) noexcept {
    OrderExecuted m;
    m.stock_locate = load_be16(p + 1);
    m.timestamp = load_be48(p + 5);
    m.order_ref = load_be64(p + 11);
    m.executed_shares = load_be32(p + 19);
    return m;
}

// C shares the leading layout of E; executed_shares sits at the same offset.
inline OrderExecuted decode_order_executed_price(const uint8_t* p) noexcept {
    return decode_order_executed(p);
}

inline OrderCancel decode_order_cancel(const uint8_t* p) noexcept {
    OrderCancel m;
    m.stock_locate = load_be16(p + 1);
    m.timestamp = load_be48(p + 5);
    m.order_ref = load_be64(p + 11);
    m.cancelled_shares = load_be32(p + 19);
    return m;
}

inline OrderDelete decode_order_delete(const uint8_t* p) noexcept {
    OrderDelete m;
    m.stock_locate = load_be16(p + 1);
    m.timestamp = load_be48(p + 5);
    m.order_ref = load_be64(p + 11);
    return m;
}

inline OrderReplace decode_order_replace(const uint8_t* p) noexcept {
    OrderReplace m;
    m.stock_locate = load_be16(p + 1);
    m.timestamp = load_be48(p + 5);
    m.original_order_ref = load_be64(p + 11);
    m.new_order_ref = load_be64(p + 19);
    m.shares = load_be32(p + 27);
    m.price = load_be32(p + 31);
    return m;
}

// R: we only care about locate -> ticker for pretty-printing / symbol filters.
inline uint16_t decode_stock_directory_locate(const uint8_t* p) noexcept {
    return load_be16(p + 1);
}

}  // namespace itch
