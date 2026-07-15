#pragma once
// Cheap, high-resolution timestamp for per-op latency.
//
// On x86 this is rdtsc (a handful of cycles, no syscall). On arm64 it's the
// virtual counter cntvct_el0. Anywhere else it falls back to steady_clock so
// the code still builds and runs. The counter frequency is calibrated once at
// startup against steady_clock, so tsc_to_ns() works on all three paths.

#include <chrono>
#include <cstdint>

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif

namespace itch {

inline uint64_t rdtsc() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    return __rdtsc();
#elif defined(__aarch64__)
    uint64_t v;
    asm volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
#else
    // Portable fallback: this already returns nanoseconds.
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

// Measures the counter frequency against steady_clock. Call once before timing.
void calibrate_tsc();

// Ticks per nanosecond as measured by calibrate_tsc().
double tsc_ticks_per_ns() noexcept;

// Convert a counter delta to nanoseconds.
uint64_t tsc_to_ns(uint64_t delta_ticks) noexcept;

}  // namespace itch
