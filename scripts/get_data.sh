#!/usr/bin/env bash
# Download a free NASDAQ TotalView-ITCH 5.0 sample file.
#
# NASDAQ publishes full-day sample files on their public FTP/HTTPS mirror. They
# are large (a few GB uncompressed), so this is NOT committed to the repo - run
# this script to fetch one, or use the synthetic generator (see below) if you
# just want to build and test.
#
# Usage:
#   scripts/get_data.sh            # downloads a default sample into data/
#   scripts/get_data.sh 12302019   # pick a specific date (MMDDYYYY)
#
# The binaries take the .ITCH50 path as argv[1], e.g.
#   ./build/baseline_book data/01302019.NASDAQ_ITCH50 AAPL
set -euo pipefail

DATE="${1:-01302019}"
OUT_DIR="data"
BASE_URL="https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH"
FILE="${DATE}.NASDAQ_ITCH50"
GZ="${FILE}.gz"

mkdir -p "$OUT_DIR"

if [[ -f "${OUT_DIR}/${FILE}" ]]; then
    echo "already have ${OUT_DIR}/${FILE}"
    exit 0
fi

echo "downloading ${GZ} from NASDAQ ..."
echo "(if this 404s, browse ${BASE_URL}/ for an available date and pass it as \$1)"
curl -fL --retry 3 -o "${OUT_DIR}/${GZ}" "${BASE_URL}/${GZ}"

echo "decompressing ..."
gunzip "${OUT_DIR}/${GZ}"

echo "done: ${OUT_DIR}/${FILE}"
echo
echo "No network / don't want the full file? Generate a synthetic feed instead:"
echo "  ./build/gen_synthetic data/synthetic.ITCH50 5000000"
