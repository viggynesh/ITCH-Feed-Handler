#include "itch/tsc.hpp"

#include <chrono>
#include <thread>

namespace itch {

namespace {
double g_ticks_per_ns = 1.0;  // sane default until calibrated
}

void calibrate_tsc() {
    using clock = std::chrono::steady_clock;
    // Warm the counter, then measure it over a fixed wall-clock window.
    const uint64_t t0 = rdtsc();
    const auto c0 = clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const uint64_t t1 = rdtsc();
    const auto c1 = clock::now();

    const double ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(c1 - c0).count();
    const double ticks = static_cast<double>(t1 - t0);
    if (ns > 0.0)
        g_ticks_per_ns = ticks / ns;
    if (g_ticks_per_ns <= 0.0)
        g_ticks_per_ns = 1.0;
}

double tsc_ticks_per_ns() noexcept { return g_ticks_per_ns; }

uint64_t tsc_to_ns(uint64_t delta_ticks) noexcept {
    return static_cast<uint64_t>(static_cast<double>(delta_ticks) / g_ticks_per_ns);
}

}  // namespace itch
