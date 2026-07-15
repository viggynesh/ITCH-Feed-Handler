# ITCH Feed Handler + Order Book Reconstruction

A low-latency NASDAQ TotalView-ITCH 5.0 feed handler in C++20. It parses the raw
binary protocol, reconstructs the full-depth limit order book per symbol, and
benchmarks two book implementations against each other so the optimization is
measurable, not just asserted.

The interesting part isn't the parser (ITCH is a fixed binary layout) — it's the
order book. I built it twice:

* **`baseline/`** — the obvious version: `std::map` price levels,
  `std::unordered_map` for orders, a heap allocation per order.
* **`optimized/`** — a flat tick-indexed array of price levels, an intrusive
  doubly-linked FIFO per level, an open-addressing order-id map, and an object
  pool so the add/cancel hot path never touches `malloc`.

Both are diffed against each other on every run and produce byte-identical book
state; the optimized one is ~1.6x the throughput and has a much tighter latency
tail.

## Architecture

```
   mmap'd .ITCH50                parser                    order book
  ┌───────────────┐        ┌────────────────┐        ┌──────────────────┐
  │ [len][msg]     │        │ read BE length │        │ A/F  -> add       │
  │ [len][msg]     │ ─────► │ dispatch by    │ ─────► │ E/C  -> execute   │ ──► top-of-book
  │ [len][msg]     │  bytes │ type char      │  typed │ X    -> cancel    │     depth / stats
  │      ...       │        │ (static, no    │  msgs  │ D    -> delete    │
  └───────────────┘        │  vtable)       │        │ U    -> replace   │
                           └────────────────┘        └──────────────────┘
                                                       baseline  |  optimized
                                                       (std::map)| (flat array)
```

Everything is big-endian on the wire and gets converted to native on read.
Prices stay as integer ticks (4 implied decimals) the whole way through — no
float on the hot path — and are only divided by 10,000 for display.

## Quick start

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# make a synthetic feed (no download needed) and run the benchmark
./build/gen_synthetic data/synthetic.ITCH50 8000000 42
./build/run_benchmark data/synthetic.ITCH50

# or reconstruct + print a single symbol's book
./build/optimized_book data/synthetic.ITCH50 AAPL

# unit tests
ctest --test-dir build --output-on-failure     # or ./build/unit_tests
```

## Data

Sample data is **not** committed (`.ITCH50` files are gitignored — they're huge
and not mine to redistribute).

* `scripts/get_data.sh [MMDDYYYY]` documents/downloads a real NASDAQ ITCH 5.0
  full-day sample from their public mirror.
* `gen_synthetic` writes a valid ITCH 5.0 stream (correct framing, real message
  layouts, a coherent add/execute/cancel/delete/replace lifecycle across a few
  symbols) so the whole thing builds and benchmarks without the multi-GB file.
  It's deterministic given a seed.

Binaries all take the file path as `argv[1]` and an optional ticker as `argv[2]`
(defaults to the busiest symbol in the file).

## The two implementations & why

| Concern | baseline | optimized | why it's faster |
|---|---|---|---|
| Price levels | `std::map<price, level>` (red-black tree) | flat array indexed by price (in pennies), best bid/ask tracked with cursor indices | sequential, cache-friendly access + predictable branches instead of chasing tree-node pointers and rebalancing |
| Orders in a level | aggregate only | intrusive doubly-linked FIFO | cancel/execute splice out in O(1) with no search and no node allocation |
| ref → order lookup | `std::unordered_map` | open-addressing linear-probe table (`OrderIdMap`) with backward-shift delete | one contiguous table, no per-entry heap node, no tombstone rot over a full day |
| order allocation | `new`/`delete` per order | fixed-block object pool | zero `malloc`/`free` on the add/cancel hot path |
| price | integer ticks | integer ticks | deterministic, exact, and integer-indexable |

A couple of honest design notes:

* **Prices are indexed in pennies.** Displayed US equity orders ≥ $1 sit on the
  penny grid, so a `1<<16` array per side covers $0–$655.35 in ~4 MB. Sub-penny
  or out-of-window prices fall back to a small `std::map` so the result stays
  identical to the baseline; on a normal liquid symbol that fallback is never
  touched.
* **One tracked symbol per run (default: the busiest).** A full flat book is a
  few MB per symbol, so materializing one for all ~8–9k symbols in a day doesn't
  make sense in a single process. The parser still decodes *every* message — so
  the throughput number is real full-file parse cost — but book mutations only
  happen for the tracked symbol(s). Sharding a book per core across all symbols
  is the obvious next step.

## Results

Measured on an **Apple M2** (4 performance cores), 8 GB, macOS 26.4, Apple clang
17, `-O3 -march=native`, on the 8M-message synthetic feed (seed 42). Raw output
is in [`results/benchmark_synthetic.txt`](results/benchmark_synthetic.txt).

|                        | baseline (std::map) | optimized (flat array) | speedup |
|------------------------|--------------------:|-----------------------:|--------:|
| throughput (Mmsg/s)    | 18.8                | 33.0                   | 1.76x   |
| ns / message           | 53.3                | 30.3                   | 1.76x   |
| book op p50            | 0.083 µs            | 0.041 µs               | ~2x     |
| book op p99            | 0.42 µs             | 0.25 µs                | ~1.7x   |
| book op p99.9          | 3.46 µs             | 0.42 µs                | ~8x     |
| book op max            | 2064 µs             | 46 µs                  | ~45x    |

Both builds clear 10M msg/s comfortably; the flat book roughly doubles it.
Throughput is stable across runs; the interesting variance is in the **tail**.
The baseline's p99.9/max are `malloc` and tree-rebalance stalls, and they swing
a lot run-to-run — I've seen the baseline max anywhere from ~140 µs to ~2 ms
depending on when the allocator or OS decides to stall. The optimized build has
no per-op allocation and no rebalancing, so its tail stays tight and boring,
which is the entire point.

### Two caveats I want to be upfront about

1. **Latency resolution on Apple Silicon.** Per-op timing uses the CPU counter
   (`rdtsc` on x86, `cntvct` on arm64). The M2 virtual counter runs at ~24 MHz,
   so it only resolves ~41.7 ns steps — that's why p50 lands exactly on 1–2
   ticks. The mechanism is correct; for sub-ns per-op numbers, run it on a
   Linux x86 box where `rdtsc` is fine-grained. Throughput uses
   `steady_clock` (nanosecond) and is exact everywhere.
2. **Cache-miss counters.** `perf` is Linux-only. `scripts/perf_stat.sh`
   captures `cache-misses` / `cache-references` / `instructions` / `cycles` for
   both builds into `results/`; I've left a template in
   [`results/perf_stat_TEMPLATE.txt`](results/perf_stat_TEMPLATE.txt) rather than
   invent numbers I couldn't measure on macOS.

### Reproduce every number

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/gen_synthetic data/synthetic.ITCH50 8000000 42
./build/run_benchmark data/synthetic.ITCH50          # throughput + latency table
# on Linux, for cache counters + core pinning:
taskset -c 2 ./build/run_benchmark data/synthetic.ITCH50
scripts/perf_stat.sh data/synthetic.ITCH50
```

## Testing

`unit_tests` (GoogleTest, fetched via CMake `FetchContent`) covers three things:

* **parser** — every wire struct size is `static_assert`ed *and* checked in a
  test; big-endian load/store round-trips; a hand-built byte buffer for each
  message type decodes to the expected fields.
* **book** — one scripted add/execute/cancel/delete/replace scenario with the
  best bid/ask, per-level depth, and total resting quantity asserted at each
  step. The *same* scenario runs against both implementations.
* **consistency** — a generated multi-symbol feed is fed to both books and their
  top-10 levels (both sides) and total quantity must match exactly.

## What I'd do next

* **Multi-symbol sharding** — hash symbols to N worker threads, each owning its
  own set of flat books, so the whole file's books are live at once.
* **Lock-free SPSC handoff** — a single-producer/single-consumer ring between a
  decode thread and the book thread to overlap parse and book work.
* **Huge pages + prefetch** — back the flat level arrays with 2 MB pages and
  software-prefetch the level a message is about to touch.
* **Replace the `std::map` overflow fallback** with a small sorted vector so the
  optimized book has no tree anywhere.

## Repo layout

```
include/itch/   headers: parser, both books, object pool, id map, tsc, histogram
src/            baseline_book.cpp, optimized_book.cpp, tsc.cpp
apps/           baseline_book / optimized_book executables (reconstruct + print)
benchmarks/     run_benchmark.cpp (the comparison harness)
tools/          gen_synthetic.cpp (synthetic ITCH feed)
tests/          GoogleTest: parser / book / consistency
scripts/        get_data.sh, perf_stat.sh
results/        saved benchmark output + perf template
```
