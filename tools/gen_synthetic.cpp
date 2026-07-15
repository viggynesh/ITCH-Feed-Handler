// Synthetic ITCH 5.0 feed generator.
//
// The real NASDAQ sample is multiple GB and needs an FTP download, which makes
// the repo impossible to build-and-run in one step. This emits a *valid* ITCH
// 5.0 binary stream (correct big-endian framing, real message layouts, a
// coherent order lifecycle) so tests and the benchmark run out of the box.
//
// It is not a market simulator - just enough add/execute/cancel/delete/replace
// traffic across a few symbols to build multi-level books and stress the hot
// path. Deterministic given a seed so results are reproducible.
//
//   gen_synthetic <out.ITCH50> [num_messages=5000000] [seed=42]

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "itch/byteswap.hpp"

using namespace itch;

namespace {

struct Symbol {
    char name[8];
    uint32_t base_price;  // in ITCH ticks (4 implied decimals)
};

struct Live {
    uint64_t ref;
    uint16_t locate;
    char side;
    uint32_t shares;
    uint32_t price;
};

void put_stock(uint8_t* dst, const char* name) {
    std::memset(dst, ' ', 8);
    size_t n = std::strlen(name);
    std::memcpy(dst, name, n > 8 ? 8 : n);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <out.ITCH50> [num_messages] [seed]\n", argv[0]);
        return 1;
    }
    const std::string out_path = argv[1];
    const uint64_t num_messages = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 5'000'000ULL;
    const uint64_t seed = (argc > 3) ? std::strtoull(argv[3], nullptr, 10) : 42ULL;

    const Symbol symbols[] = {
        {{'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '}, 1500000},  // $150.00
        {{'M', 'S', 'F', 'T', ' ', ' ', ' ', ' '}, 4200000},  // $420.00
        {{'T', 'S', 'L', 'A', ' ', ' ', ' ', ' '}, 2500000},  // $250.00
        {{'A', 'M', 'Z', 'N', ' ', ' ', ' ', ' '}, 1850000},  // $185.00
    };
    const int num_symbols = sizeof(symbols) / sizeof(symbols[0]);

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> sym_pick(0, num_symbols - 1);
    std::uniform_int_distribution<int> depth(1, 50);  // 1..50 pennies away from base
    std::uniform_int_distribution<int> lot(1, 20);    // round lots of 100
    std::uniform_int_distribution<int> action(0, 99);
    std::uniform_int_distribution<int> coin(0, 1);

    // Buys rest at or below base, sells at or above, so the book isn't crossed
    // (there's no matching engine here - this just keeps bid < ask like a real
    // continuous book).
    auto price_for = [&](char side, int si) {
        const int d = depth(rng) * 100;  // pennies -> ticks
        const int base = static_cast<int>(symbols[si].base_price);
        return static_cast<uint32_t>(side == 'B' ? base - d : base + d);
    };

    std::vector<uint8_t> out;
    out.reserve(num_messages * 36);

    auto emit = [&](const uint8_t* payload, uint16_t len) {
        uint8_t lb[2];
        store_be16(lb, len);
        out.insert(out.end(), lb, lb + 2);
        out.insert(out.end(), payload, payload + len);
    };

    uint64_t ts = 34ULL * 3600 * 1'000'000'000;  // ~09:34, arbitrary start
    auto tick = [&]() { ts += 1 + (rng() & 0x3F); return ts; };

    uint8_t buf[64];

    // System event: 'O' = start of messages.
    buf[0] = 'S';
    store_be16(buf + 1, 0);
    store_be16(buf + 3, 0);
    store_be48(buf + 5, tick());
    buf[11] = 'O';
    emit(buf, 12);

    // Stock directory for each symbol; locate = index + 1.
    for (int i = 0; i < num_symbols; ++i) {
        std::memset(buf, 0, 39);
        buf[0] = 'R';
        store_be16(buf + 1, static_cast<uint16_t>(i + 1));
        store_be16(buf + 3, 0);
        store_be48(buf + 5, tick());
        put_stock(buf + 11, symbols[i].name);
        buf[19] = 'Q';   // market category
        buf[20] = 'N';   // financial status
        store_be32(buf + 21, 100);  // round lot size
        buf[25] = 'N';
        buf[26] = 'C';
        emit(buf, 39);
    }

    std::vector<Live> live;
    live.reserve(1u << 20);
    uint64_t next_ref = 1;
    uint64_t match_no = 1;
    uint64_t emitted = 0;

    auto emit_add = [&]() {
        const int si = sym_pick(rng);
        const uint16_t locate = static_cast<uint16_t>(si + 1);
        const char side = coin(rng) ? 'B' : 'S';
        const uint32_t shares = static_cast<uint32_t>(lot(rng)) * 100u;
        const uint32_t price = price_for(side, si);
        const uint64_t ref = next_ref++;

        buf[0] = 'A';
        store_be16(buf + 1, locate);
        store_be16(buf + 3, 0);
        store_be48(buf + 5, tick());
        store_be64(buf + 11, ref);
        buf[19] = static_cast<uint8_t>(side);
        store_be32(buf + 20, shares);
        put_stock(buf + 24, symbols[si].name);
        store_be32(buf + 32, price);
        emit(buf, 36);

        live.push_back({ref, locate, side, shares, price});
    };

    auto pick_live = [&]() -> int {
        if (live.empty())
            return -1;
        std::uniform_int_distribution<size_t> pick(0, live.size() - 1);
        return static_cast<int>(pick(rng));
    };

    auto remove_live = [&](int idx) {
        live[idx] = live.back();
        live.pop_back();
    };

    while (emitted < num_messages) {
        const int a = action(rng);
        if (live.empty() || a < 50) {
            emit_add();
        } else if (a < 70) {
            // Order Executed (E): partial or full.
            int idx = pick_live();
            Live& o = live[idx];
            uint32_t exec = (o.shares > 100 && coin(rng)) ? 100u : o.shares;
            buf[0] = 'E';
            store_be16(buf + 1, o.locate);
            store_be16(buf + 3, 0);
            store_be48(buf + 5, tick());
            store_be64(buf + 11, o.ref);
            store_be32(buf + 19, exec);
            store_be64(buf + 23, match_no++);
            emit(buf, 31);
            o.shares -= exec;
            if (o.shares == 0)
                remove_live(idx);
        } else if (a < 80) {
            // Order Cancel (X): partial reduce.
            int idx = pick_live();
            Live& o = live[idx];
            uint32_t canc = (o.shares > 100 && coin(rng)) ? 100u : o.shares;
            buf[0] = 'X';
            store_be16(buf + 1, o.locate);
            store_be16(buf + 3, 0);
            store_be48(buf + 5, tick());
            store_be64(buf + 11, o.ref);
            store_be32(buf + 19, canc);
            emit(buf, 23);
            o.shares -= canc;
            if (o.shares == 0)
                remove_live(idx);
        } else if (a < 90) {
            // Order Delete (D).
            int idx = pick_live();
            Live& o = live[idx];
            buf[0] = 'D';
            store_be16(buf + 1, o.locate);
            store_be16(buf + 3, 0);
            store_be48(buf + 5, tick());
            store_be64(buf + 11, o.ref);
            emit(buf, 19);
            remove_live(idx);
        } else {
            // Order Replace (U): new ref, new price/size, same side/symbol.
            int idx = pick_live();
            Live& o = live[idx];
            const uint64_t new_ref = next_ref++;
            const uint32_t new_shares = static_cast<uint32_t>(lot(rng)) * 100u;
            const uint32_t new_price = price_for(o.side, o.locate - 1);
            buf[0] = 'U';
            store_be16(buf + 1, o.locate);
            store_be16(buf + 3, 0);
            store_be48(buf + 5, tick());
            store_be64(buf + 11, o.ref);
            store_be64(buf + 19, new_ref);
            store_be32(buf + 27, new_shares);
            store_be32(buf + 31, new_price);
            emit(buf, 35);
            o.ref = new_ref;
            o.shares = new_shares;
            o.price = new_price;
        }
        ++emitted;
    }

    FILE* f = std::fopen(out_path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s for writing\n", out_path.c_str());
        return 1;
    }
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);

    std::printf("wrote %llu messages (%zu bytes) to %s\n",
                static_cast<unsigned long long>(emitted), out.size(), out_path.c_str());
    return 0;
}
