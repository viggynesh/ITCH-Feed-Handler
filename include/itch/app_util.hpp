#pragma once
// Small helpers shared by the baseline_book / optimized_book apps: choosing
// which symbol to track and printing a reconstructed book snapshot.

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "book_types.hpp"
#include "parser.hpp"

namespace itch {

// Resolve the symbol to track: an explicit ticker if given and found, else the
// busiest one from the pre-scan.
struct Target {
    uint16_t locate;
    std::string name;
};

inline Target select_target(const SymbolScan& scan, const std::optional<std::string>& want) {
    if (want) {
        for (uint16_t l = 0; l < scan.names.size(); ++l) {
            if (trim_symbol(scan.names[l]) == *want)
                return {l, *want};
        }
        std::fprintf(stderr, "symbol '%s' not found; falling back to busiest\n", want->c_str());
    }
    std::string name = scan.busiest_locate < scan.names.size()
                           ? trim_symbol(scan.names[scan.busiest_locate])
                           : std::string{};
    return {scan.busiest_locate, name};
}

inline void print_stats(const ParseStats& s) {
    std::printf("messages   : %llu\n", (unsigned long long)s.messages);
    std::printf("  adds     : %llu\n", (unsigned long long)s.adds);
    std::printf("  executes : %llu\n", (unsigned long long)s.executes);
    std::printf("  cancels  : %llu\n", (unsigned long long)s.cancels);
    std::printf("  deletes  : %llu\n", (unsigned long long)s.deletes);
    std::printf("  replaces : %llu\n", (unsigned long long)s.replaces);
    std::printf("  trades   : %llu\n", (unsigned long long)s.trades);
    std::printf("  other    : %llu\n", (unsigned long long)s.other);
    if (s.bad_length)
        std::printf("  WARNING: %llu framed lengths disagreed with the spec\n",
                    (unsigned long long)s.bad_length);
}

// Book is BaselineBook or OptimizedBook - same query surface.
template <class Book>
void print_book(const Book& book, const Target& tgt) {
    std::printf("\n=== book for %s (locate %u) ===\n", tgt.name.c_str(), tgt.locate);

    TopOfBook tob = book.top_of_book(tgt.locate);
    if (tob.has_bid && tob.has_ask) {
        std::printf("best bid : %.4f x %llu\n", tob.bid_px / 10000.0,
                    (unsigned long long)tob.bid_qty);
        std::printf("best ask : %.4f x %llu\n", tob.ask_px / 10000.0,
                    (unsigned long long)tob.ask_qty);
        std::printf("spread   : %.4f\n", (tob.ask_px - tob.bid_px) / 10000.0);
    } else {
        std::printf("book is empty / one-sided\n");
    }

    auto bids = book.top_n(tgt.locate, Side::Buy, 10);
    auto asks = book.top_n(tgt.locate, Side::Sell, 10);
    std::printf("\n%-28s | %-28s\n", "         BIDS (px x qty n)", "         ASKS (px x qty n)");
    for (size_t i = 0; i < 10; ++i) {
        char lb[32] = "", rb[32] = "";
        if (i < bids.size())
            std::snprintf(lb, sizeof(lb), "%10.4f x %7llu (%u)", bids[i].price / 10000.0,
                          (unsigned long long)bids[i].qty, bids[i].orders);
        if (i < asks.size())
            std::snprintf(rb, sizeof(rb), "%10.4f x %7llu (%u)", asks[i].price / 10000.0,
                          (unsigned long long)asks[i].qty, asks[i].orders);
        std::printf("%-28s | %-28s\n", lb, rb);
    }
    std::printf("\ntotal resting qty: %llu\n",
                (unsigned long long)book.total_resting_qty(tgt.locate));
}

}  // namespace itch
