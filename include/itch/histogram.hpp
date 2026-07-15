#pragma once
// Dead-simple latency histogram: one bucket per nanosecond up to ~1 ms, plus a
// tracked true-max for the tail. That's 4 MB of counters, all we need to read
// p50/p99/p99.9 off a cumulative sum. Book-update latencies here are hundreds
// of ns to single-digit microseconds, so 1 ns resolution is plenty and nothing
// realistic clips the top bucket.
//
// (HdrHistogram would be the "proper" choice, but for a fixed, narrow range a
// flat array is faster to record into and trivial to reason about.)

#include <algorithm>
#include <cstdint>
#include <vector>

namespace itch {

class Histogram {
public:
    static constexpr uint64_t kCap = 1u << 20;  // 1,048,576 ns ~= 1.05 ms

    Histogram() : buckets_(kCap, 0) {}

    inline void record(uint64_t ns) noexcept {
        if (ns > max_ns_)
            max_ns_ = ns;
        if (ns >= kCap)
            ns = kCap - 1;  // clip into the top bucket (true max kept above)
        ++buckets_[ns];
        ++count_;
    }

    // p in [0,1]; returns the nanosecond value at that percentile.
    uint64_t percentile(double p) const noexcept {
        if (count_ == 0)
            return 0;
        uint64_t target = static_cast<uint64_t>(p * static_cast<double>(count_));
        if (target >= count_)
            target = count_ - 1;
        uint64_t cumulative = 0;
        for (uint64_t ns = 0; ns < kCap; ++ns) {
            cumulative += buckets_[ns];
            if (cumulative > target)
                return ns;
        }
        return kCap - 1;
    }

    uint64_t max_ns() const noexcept { return max_ns_; }
    uint64_t count() const noexcept { return count_; }

    void reset() noexcept {
        std::fill(buckets_.begin(), buckets_.end(), 0u);
        count_ = 0;
        max_ns_ = 0;
    }

private:
    std::vector<uint32_t> buckets_;
    uint64_t count_ = 0;
    uint64_t max_ns_ = 0;
};

}  // namespace itch
