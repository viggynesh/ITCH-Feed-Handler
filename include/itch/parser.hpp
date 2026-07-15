#pragma once
// Streaming ITCH 5.0 parser.
//
// A raw ITCH file (NASDAQ "BinaryFILE" format) is just a flat stream of:
//     [2-byte big-endian length N][N bytes of message payload]
// where payload[0] is the message type char. There is no other framing.
//
// parse_stream walks the buffer, decodes each supported message into a
// host-endian struct, and dispatches to the handler by static (compile-time)
// call - no virtual dispatch on the hot path. Handlers derive from
// NullHandler and override only the callbacks they care about.

#include <cstddef>
#include <cstdint>

#include "messages.hpp"

namespace itch {

// Per-run tallies. Handy for the benchmark and for picking the busiest symbol.
struct ParseStats {
    uint64_t messages = 0;
    uint64_t adds = 0;
    uint64_t executes = 0;
    uint64_t cancels = 0;
    uint64_t deletes = 0;
    uint64_t replaces = 0;
    uint64_t trades = 0;
    uint64_t other = 0;
    uint64_t bad_length = 0;  // framed length disagreed with the spec
};

// Base handler: every callback is an empty inline no-op. Override what you need.
struct NullHandler {
    void on_system_event(char /*event_code*/) noexcept {}
    void on_stock_directory(uint16_t /*locate*/, const char* /*stock8*/) noexcept {}
    void on_add(const AddOrder&) noexcept {}
    void on_execute(const OrderExecuted&) noexcept {}
    void on_execute_price(const OrderExecuted&) noexcept {}
    void on_cancel(const OrderCancel&) noexcept {}
    void on_delete(const OrderDelete&) noexcept {}
    void on_replace(const OrderReplace&) noexcept {}
    void on_other(char /*type*/) noexcept {}
};

// Returns the byte offset one past the last fully-parsed message. `stats` is
// filled with per-type counts.
template <class Handler>
size_t parse_stream(const uint8_t* data, size_t len, Handler& h, ParseStats& stats) noexcept {
    size_t off = 0;
    while (off + 2 <= len) {
        const uint16_t msg_len = load_be16(data + off);
        if (msg_len == 0)
            break;  // 0-length framing marks end of some feeds
        const size_t body = off + 2;
        if (body + msg_len > len)
            break;  // truncated tail - stop cleanly

        const uint8_t* p = data + body;
        const char type = static_cast<char>(p[0]);

        const int exp = expected_length(type);
        if (exp > 0 && exp != static_cast<int>(msg_len))
            ++stats.bad_length;

        switch (type) {
            case msg_type::AddOrder:
            case msg_type::AddOrderMPID:  // same layout for the fields we read
                h.on_add(decode_add_order(p));
                ++stats.adds;
                break;
            case msg_type::OrderExecuted:
                h.on_execute(decode_order_executed(p));
                ++stats.executes;
                break;
            case msg_type::OrderExecutedPrice:
                h.on_execute_price(decode_order_executed_price(p));
                ++stats.executes;
                break;
            case msg_type::OrderCancel:
                h.on_cancel(decode_order_cancel(p));
                ++stats.cancels;
                break;
            case msg_type::OrderDelete:
                h.on_delete(decode_order_delete(p));
                ++stats.deletes;
                break;
            case msg_type::OrderReplace:
                h.on_replace(decode_order_replace(p));
                ++stats.replaces;
                break;
            case msg_type::TradeNonCross:
            case msg_type::TradeCross:
                h.on_other(type);
                ++stats.trades;
                break;
            case msg_type::SystemEvent:
                h.on_system_event(static_cast<char>(p[11]));
                ++stats.other;
                break;
            case msg_type::StockDirectory:
                h.on_stock_directory(load_be16(p + 1),
                                     reinterpret_cast<const char*>(p + 11));
                ++stats.other;
                break;
            default:
                h.on_other(type);
                ++stats.other;
                break;
        }

        ++stats.messages;
        off = body + msg_len;
    }
    return off;
}

}  // namespace itch
