#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "itch/baseline_book.hpp"
#include "itch/byteswap.hpp"
#include "itch/optimized_book.hpp"
#include "itch/parser.hpp"

using namespace itch;

namespace {

// Build a small but non-trivial in-memory ITCH stream across two symbols with a
// valid order lifecycle. Prices are penny-aligned so both books agree exactly.
std::vector<uint8_t> build_feed(uint64_t n_ops, uint64_t seed) {
    struct Live {
        uint64_t ref;
        uint16_t locate;
        char side;
        uint32_t shares;
    };
    const uint32_t base[2] = {1000000, 2000000};  // $100 and $200

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> sym(0, 1);
    std::uniform_int_distribution<int> off(-20, 20);
    std::uniform_int_distribution<int> lot(1, 10);
    std::uniform_int_distribution<int> act(0, 99);
    std::uniform_int_distribution<int> coin(0, 1);

    std::vector<uint8_t> out;
    std::vector<Live> live;
    uint64_t next_ref = 1;
    uint64_t ts = 0;
    uint8_t buf[64];

    auto emit = [&](uint16_t len) {
        uint8_t lb[2];
        store_be16(lb, len);
        out.insert(out.end(), lb, lb + 2);
        out.insert(out.end(), buf, buf + len);
    };
    auto pick = [&]() -> int {
        if (live.empty())
            return -1;
        std::uniform_int_distribution<size_t> p(0, live.size() - 1);
        return static_cast<int>(p(rng));
    };
    auto drop = [&](int i) {
        live[i] = live.back();
        live.pop_back();
    };

    for (uint64_t i = 0; i < n_ops; ++i) {
        int a = act(rng);
        if (live.empty() || a < 55) {
            int si = sym(rng);
            uint16_t loc = static_cast<uint16_t>(si + 1);
            char side = coin(rng) ? 'B' : 'S';
            uint32_t sh = static_cast<uint32_t>(lot(rng)) * 100u;
            uint32_t px = static_cast<uint32_t>(static_cast<int>(base[si]) + off(rng) * 100);
            uint64_t ref = next_ref++;
            buf[0] = 'A';
            store_be16(buf + 1, loc);
            store_be16(buf + 3, 0);
            store_be48(buf + 5, ++ts);
            store_be64(buf + 11, ref);
            buf[19] = static_cast<uint8_t>(side);
            store_be32(buf + 20, sh);
            std::memset(buf + 24, ' ', 8);
            store_be32(buf + 32, px);
            emit(36);
            live.push_back({ref, loc, side, sh});
        } else if (a < 70) {
            int idx = pick();
            Live& o = live[idx];
            uint32_t ex = (o.shares > 100 && coin(rng)) ? 100u : o.shares;
            buf[0] = 'E';
            store_be16(buf + 1, o.locate);
            store_be16(buf + 3, 0);
            store_be48(buf + 5, ++ts);
            store_be64(buf + 11, o.ref);
            store_be32(buf + 19, ex);
            store_be64(buf + 23, i + 1);
            emit(31);
            o.shares -= ex;
            if (o.shares == 0)
                drop(idx);
        } else if (a < 82) {
            int idx = pick();
            Live& o = live[idx];
            uint32_t cx = (o.shares > 100 && coin(rng)) ? 100u : o.shares;
            buf[0] = 'X';
            store_be16(buf + 1, o.locate);
            store_be16(buf + 3, 0);
            store_be48(buf + 5, ++ts);
            store_be64(buf + 11, o.ref);
            store_be32(buf + 19, cx);
            emit(23);
            o.shares -= cx;
            if (o.shares == 0)
                drop(idx);
        } else if (a < 92) {
            int idx = pick();
            Live& o = live[idx];
            buf[0] = 'D';
            store_be16(buf + 1, o.locate);
            store_be16(buf + 3, 0);
            store_be48(buf + 5, ++ts);
            store_be64(buf + 11, o.ref);
            emit(19);
            drop(idx);
        } else {
            int idx = pick();
            Live& o = live[idx];
            uint64_t nref = next_ref++;
            uint32_t nsh = static_cast<uint32_t>(lot(rng)) * 100u;
            uint32_t npx =
                static_cast<uint32_t>(static_cast<int>(base[o.locate - 1]) + off(rng) * 100);
            buf[0] = 'U';
            store_be16(buf + 1, o.locate);
            store_be16(buf + 3, 0);
            store_be48(buf + 5, ++ts);
            store_be64(buf + 11, o.ref);
            store_be64(buf + 19, nref);
            store_be32(buf + 27, nsh);
            store_be32(buf + 31, npx);
            emit(35);
            o.ref = nref;
            o.shares = nsh;
        }
    }
    return out;
}

}  // namespace

TEST(Consistency, BaselineAndOptimizedAgree) {
    const std::vector<uint8_t> feed = build_feed(50000, 0xC0FFEE);
    ASSERT_FALSE(feed.empty());

    for (uint16_t locate : {uint16_t{1}, uint16_t{2}}) {
        BaselineBook base({locate});
        OptimizedBook opt({locate});
        ParseStats s1, s2;
        parse_stream(feed.data(), feed.size(), base, s1);
        parse_stream(feed.data(), feed.size(), opt, s2);

        for (Side side : {Side::Buy, Side::Sell}) {
            auto a = base.top_n(locate, side, 10);
            auto b = opt.top_n(locate, side, 10);
            ASSERT_EQ(a.size(), b.size()) << "locate " << locate << " side " << (int)side;
            for (size_t i = 0; i < a.size(); ++i) {
                EXPECT_EQ(a[i].price, b[i].price) << "level " << i;
                EXPECT_EQ(a[i].qty, b[i].qty) << "level " << i;
                EXPECT_EQ(a[i].orders, b[i].orders) << "level " << i;
            }
        }
        EXPECT_EQ(base.total_resting_qty(locate), opt.total_resting_qty(locate));
    }
}
