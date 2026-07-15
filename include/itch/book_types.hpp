#pragma once
// Shared types for both order-book implementations plus a small helper that
// picks which symbol(s) to actually build books for.
//
// Design note: a full flat tick-indexed book (optimized/) costs a few MB per
// symbol, so building one for every one of the ~8-9k symbols in a trading day
// is not sensible in a single process. Both books therefore track a small,
// explicit set of symbols (default: the single busiest one). The parser still
// decodes every message - so the throughput number reflects full-file parse
// cost - but book mutations only happen for tracked symbols. Sharding a book
// per core across all symbols is the natural next step (see README).

#include <cstdint>
#include <string>
#include <vector>

#include "parser.hpp"

namespace itch {

using Ticks = int64_t;  // price in integer ticks (4 implied decimals); signed for arithmetic

// One aggregated price level as seen by a reader (best bid, depth, etc.).
struct LevelView {
    Ticks price = 0;
    uint64_t qty = 0;
    uint32_t orders = 0;
};

struct TopOfBook {
    bool has_bid = false;
    bool has_ask = false;
    Ticks bid_px = 0;
    Ticks ask_px = 0;
    uint64_t bid_qty = 0;
    uint64_t ask_qty = 0;
};

// Result of the untimed pre-scan: which symbols exist and which are busiest.
struct SymbolScan {
    // locate -> ticker (right-padded with spaces as in ITCH); index by locate.
    std::vector<std::string> names;
    std::vector<uint64_t> add_counts;  // adds seen per locate
    uint16_t busiest_locate = 0;
    uint64_t busiest_adds = 0;
};

namespace detail {
struct ScanHandler : NullHandler {
    SymbolScan* scan;
    explicit ScanHandler(SymbolScan* s) : scan(s) {}

    void ensure(uint16_t locate) {
        if (locate >= scan->names.size()) {
            scan->names.resize(locate + 1);
            scan->add_counts.resize(locate + 1, 0);
        }
    }
    void on_stock_directory(uint16_t locate, const char* stock8) noexcept {
        ensure(locate);
        scan->names[locate].assign(stock8, 8);
    }
    void on_add(const AddOrder& a) noexcept {
        ensure(a.stock_locate);
        uint64_t c = ++scan->add_counts[a.stock_locate];
        if (c > scan->busiest_adds) {
            scan->busiest_adds = c;
            scan->busiest_locate = a.stock_locate;
        }
    }
};
}  // namespace detail

// Untimed single pass to learn symbol names and the busiest instrument.
inline SymbolScan scan_symbols(const uint8_t* data, size_t len) {
    SymbolScan scan;
    detail::ScanHandler h(&scan);
    ParseStats stats;
    parse_stream(data, len, h, stats);
    return scan;
}

// Trim ITCH's space padding off a ticker for display.
inline std::string trim_symbol(const std::string& s) {
    size_t end = s.find_last_not_of(' ');
    return end == std::string::npos ? std::string{} : s.substr(0, end + 1);
}

}  // namespace itch
