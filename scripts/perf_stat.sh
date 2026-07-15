#!/usr/bin/env bash
# Capture hardware performance counters for both book builds and save them under
# results/. This is the "before/after" cache-miss evidence for the README.
#
# Linux only (needs `perf`). On a laptop you may need:
#   sudo sysctl kernel.perf_event_paranoid=1
# Pin to an isolated core with taskset for stable numbers.
#
# Usage:
#   scripts/perf_stat.sh <file.ITCH50> [SYMBOL] [CORE]
set -euo pipefail

FILE="${1:?usage: perf_stat.sh <file.ITCH50> [SYMBOL] [CORE]}"
SYMBOL="${2:-}"
CORE="${3:-2}"

BUILD_DIR="build"
RESULTS_DIR="results"
mkdir -p "$RESULTS_DIR"

if ! command -v perf >/dev/null 2>&1; then
    echo "error: 'perf' not found. This script requires Linux perf." >&2
    echo "On macOS there is no perf; run ./build/run_benchmark directly for" >&2
    echo "throughput + latency, and capture cache counters on a Linux box." >&2
    exit 1
fi

EVENTS="cache-misses,cache-references,instructions,cycles"

run_one() {
    local name="$1"       # baseline_book | optimized_book
    local out="${RESULTS_DIR}/perf_${name}.txt"
    echo "==> ${name}  (taskset -c ${CORE}, perf ${EVENTS})"
    perf stat -e "$EVENTS" \
        taskset -c "$CORE" "${BUILD_DIR}/${name}" "$FILE" $SYMBOL \
        > "${out}" 2>&1 || true
    echo "    saved ${out}"
}

run_one baseline_book
run_one optimized_book

echo
echo "cache-miss summary:"
grep -H "cache-misses" "${RESULTS_DIR}"/perf_*.txt || true
