#pragma once
// Pin the calling thread to one core so the benchmark isn't fighting the
// scheduler (migrations trash the cache and smear the latency histogram).
// Linux-only; a no-op returning false elsewhere (e.g. when I develop on macOS),
// which is fine because on Linux you'd also run the binary under
// `taskset -c <core>` for isolation - see scripts/perf_stat.sh.

#if defined(__linux__)
#include <sched.h>
#endif

namespace itch {

inline bool pin_thread_to_cpu(int cpu) {
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return sched_setaffinity(0, sizeof(set), &set) == 0;
#else
    (void)cpu;
    return false;  // not supported off Linux
#endif
}

}  // namespace itch
