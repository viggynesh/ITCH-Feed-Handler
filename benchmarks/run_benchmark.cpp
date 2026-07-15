// run_benchmark: the whole point of the project. For the busiest symbol in the
// file, it measures
//   (1) THROUGHPUT of the full parse + book loop over the entire file
//       (wall-clock, file load excluded), and
//   (2) per-op LATENCY of book updates, replayed from a pre-extracted op stream
//       and timed with rdtsc into a histogram, with a warmup discard.
// It runs both book implementations and prints a side-by-side table, and sanity
// checks that the two produce an identical top-of-book.
//
//   run_benchmark <file.ITCH50> [SYMBOL]
//
// For clean latency numbers, pin to an isolated core:  taskset -c 2 run_benchmark ...

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "itch/affinity.hpp"
#include "itch/baseline_book.hpp"
#include "itch/book_types.hpp"
#include "itch/histogram.hpp"
#include "itch/mapped_file.hpp"
#include "itch/optimized_book.hpp"
#include "itch/parser.hpp"
#include "itch/tsc.hpp"

using namespace itch;

namespace {

// A single book-mutating event, pre-decoded, for the latency replay.
struct Op {
    enum Kind : uint8_t { Add, Execute, Cancel, Delete, Replace } kind;
    uint64_t ref;
    uint64_t new_ref;
    uint32_t shares;
    uint32_t price;
    Side side;
    uint16_t locate;
};

// Pulls out just the tracked symbol's book-mutating events, in order, tracking
// live shares so E/C/X/D/U membership is correct (they carry no symbol).
struct Extractor : NullHandler {
    uint16_t target;
    std::vector<Op>* ops;
    std::unordered_map<uint64_t, uint32_t> shares;

    Extractor(uint16_t t, std::vector<Op>* o) : target(t), ops(o) { shares.reserve(1u << 20); }

    void on_add(const AddOrder& a) {
        if (a.stock_locate != target)
            return;
        ops->push_back({Op::Add, a.order_ref, 0, a.shares, a.price, a.side, a.stock_locate});
        shares[a.order_ref] = a.shares;
    }
    void reduce(uint64_t ref, uint32_t qty, Op::Kind kind) {
        auto it = shares.find(ref);
        if (it == shares.end())
            return;
        ops->push_back({kind, ref, 0, qty, 0, Side::Buy, target});
        if (qty >= it->second)
            shares.erase(it);
        else
            it->second -= qty;
    }
    void on_execute(const OrderExecuted& e) { reduce(e.order_ref, e.executed_shares, Op::Execute); }
    void on_execute_price(const OrderExecuted& e) {
        reduce(e.order_ref, e.executed_shares, Op::Execute);
    }
    void on_cancel(const OrderCancel& x) { reduce(x.order_ref, x.cancelled_shares, Op::Cancel); }
    void on_delete(const OrderDelete& d) {
        auto it = shares.find(d.order_ref);
        if (it == shares.end())
            return;
        ops->push_back({Op::Delete, d.order_ref, 0, 0, 0, Side::Buy, target});
        shares.erase(it);
    }
    void on_replace(const OrderReplace& u) {
        auto it = shares.find(u.original_order_ref);
        if (it == shares.end())
            return;
        ops->push_back(
            {Op::Replace, u.original_order_ref, u.new_order_ref, u.shares, u.price, Side::Buy, target});
        shares.erase(it);
        shares[u.new_order_ref] = u.shares;
    }
};

template <class Book>
inline void apply_op(Book& b, const Op& o) {
    switch (o.kind) {
        case Op::Add: {
            AddOrder a{};
            a.stock_locate = o.locate;
            a.order_ref = o.ref;
            a.side = o.side;
            a.shares = o.shares;
            a.price = o.price;
            b.on_add(a);
            break;
        }
        case Op::Execute:
            b.on_execute(OrderExecuted{o.locate, 0, o.ref, o.shares});
            break;
        case Op::Cancel:
            b.on_cancel(OrderCancel{o.locate, 0, o.ref, o.shares});
            break;
        case Op::Delete:
            b.on_delete(OrderDelete{o.locate, 0, o.ref});
            break;
        case Op::Replace:
            b.on_replace(OrderReplace{o.locate, 0, o.ref, o.new_ref, o.shares, o.price});
            break;
    }
}

struct ThroughputResult {
    double msgs_per_sec;
    double ns_per_msg;
    uint64_t messages;
};

template <class Book>
ThroughputResult run_throughput(const uint8_t* data, size_t len, uint16_t target, Book& book) {
    ParseStats stats;
    const auto t0 = std::chrono::steady_clock::now();
    parse_stream(data, len, book, stats);
    const auto t1 = std::chrono::steady_clock::now();
    (void)target;
    const double ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    ThroughputResult r;
    r.messages = stats.messages;
    r.ns_per_msg = ns / static_cast<double>(stats.messages);
    r.msgs_per_sec = static_cast<double>(stats.messages) / (ns / 1e9);
    return r;
}

struct LatencyResult {
    uint64_t p50, p99, p999, max;  // nanoseconds
    uint64_t samples;
};

template <class Book>
LatencyResult run_latency(const std::vector<Op>& ops, size_t warmup, Book& book) {
    Histogram h;
    for (size_t i = 0; i < ops.size(); ++i) {
        const uint64_t t0 = rdtsc();
        apply_op(book, ops[i]);
        const uint64_t t1 = rdtsc();
        if (i >= warmup)
            h.record(tsc_to_ns(t1 - t0));
    }
    return {h.percentile(0.50), h.percentile(0.99), h.percentile(0.999), h.max_ns(), h.count()};
}

bool books_agree(const BaselineBook& a, const OptimizedBook& b, uint16_t locate) {
    for (Side side : {Side::Buy, Side::Sell}) {
        auto la = a.top_n(locate, side, 10);
        auto lb = b.top_n(locate, side, 10);
        if (la.size() != lb.size())
            return false;
        for (size_t i = 0; i < la.size(); ++i)
            if (la[i].price != lb[i].price || la[i].qty != lb[i].qty || la[i].orders != lb[i].orders)
                return false;
    }
    return a.total_resting_qty(locate) == b.total_resting_qty(locate);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <file.ITCH50> [SYMBOL]\n", argv[0]);
        return 1;
    }
    std::optional<std::string> want;
    if (argc > 2)
        want = std::string(argv[2]);

    calibrate_tsc();
    const bool pinned = pin_thread_to_cpu(0);

    try {
        MappedFile file(argv[1]);
        file.prefault();

        SymbolScan scan = scan_symbols(file.data(), file.size());
        uint16_t target = scan.busiest_locate;
        std::string name;
        if (want) {
            bool found = false;
            for (uint16_t l = 0; l < scan.names.size(); ++l)
                if (trim_symbol(scan.names[l]) == *want) {
                    target = l;
                    name = *want;
                    found = true;
                    break;
                }
            if (!found)
                std::fprintf(stderr, "symbol '%s' not found, using busiest\n", want->c_str());
        }
        if (name.empty() && target < scan.names.size())
            name = trim_symbol(scan.names[target]);

        std::printf("file        : %s (%.1f MB)\n", argv[1], file.size() / (1024.0 * 1024.0));
        std::printf("symbol      : %s (locate %u)\n", name.c_str(), target);
        std::printf("tsc freq    : %.3f GHz (calibrated)\n", tsc_ticks_per_ns());
        std::printf("core pinned : %s\n\n", pinned ? "yes (cpu 0)" : "no (not Linux)");

        // Pre-extract the tracked symbol's op stream for the latency replay.
        std::vector<Op> ops;
        {
            Extractor ex(target, &ops);
            ParseStats s;
            parse_stream(file.data(), file.size(), ex, s);
        }
        const size_t warmup = std::min<size_t>(1'000'000, ops.size() / 4);
        std::printf("tracked-symbol book ops: %zu (warmup discard: %zu)\n\n", ops.size(), warmup);

        // --- throughput + consistency (full parse over the whole file) ---
        BaselineBook base_full({target});
        OptimizedBook opt_full({target});
        ThroughputResult base_tp = run_throughput(file.data(), file.size(), target, base_full);
        ThroughputResult opt_tp = run_throughput(file.data(), file.size(), target, opt_full);
        const bool consistent = books_agree(base_full, opt_full, target);

        // --- latency (op replay on fresh books) ---
        BaselineBook base_lat({target});
        OptimizedBook opt_lat({target});
        LatencyResult base_l = run_latency(ops, warmup, base_lat);
        LatencyResult opt_l = run_latency(ops, warmup, opt_lat);

        auto us = [](uint64_t ns) { return ns / 1000.0; };

        std::printf("%-22s %15s %15s %12s\n", "", "baseline", "optimized", "speedup");
        std::printf("%-22s %15s %15s %12s\n", "", "(std::map)", "(flat array)", "");
        std::printf("----------------------------------------------------------------------\n");
        std::printf("%-22s %15.2f %15.2f %11.2fx\n", "throughput (Mmsg/s)",
                    base_tp.msgs_per_sec / 1e6, opt_tp.msgs_per_sec / 1e6,
                    opt_tp.msgs_per_sec / base_tp.msgs_per_sec);
        std::printf("%-22s %15.1f %15.1f %11.2fx\n", "ns / message", base_tp.ns_per_msg,
                    opt_tp.ns_per_msg, base_tp.ns_per_msg / opt_tp.ns_per_msg);
        std::printf("%-22s %15.3f %15.3f %11.2fx\n", "book op p50 (us)", us(base_l.p50),
                    us(opt_l.p50), opt_l.p50 ? (double)base_l.p50 / opt_l.p50 : 0.0);
        std::printf("%-22s %15.3f %15.3f %11.2fx\n", "book op p99 (us)", us(base_l.p99),
                    us(opt_l.p99), opt_l.p99 ? (double)base_l.p99 / opt_l.p99 : 0.0);
        std::printf("%-22s %15.3f %15.3f %11.2fx\n", "book op p99.9 (us)", us(base_l.p999),
                    us(opt_l.p999), opt_l.p999 ? (double)base_l.p999 / opt_l.p999 : 0.0);
        std::printf("%-22s %15.3f %15.3f %11.2fx\n", "book op max (us)", us(base_l.max),
                    us(opt_l.max), opt_l.max ? (double)base_l.max / opt_l.max : 0.0);
        std::printf("----------------------------------------------------------------------\n");
        std::printf("consistency (top-10 both sides + total qty): %s\n",
                    consistent ? "PASS" : "FAIL");
        std::printf("latency samples: %zu (baseline) / %zu (optimized)\n",
                    (size_t)base_l.samples, (size_t)opt_l.samples);

        return consistent ? 0 : 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
