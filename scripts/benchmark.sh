#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT_DIR="$ROOT_DIR/benchmark-results"
mkdir -p "$OUTPUT_DIR"
"$ROOT_DIR/build/optimizer/sih-optimizer" benchmark \
  --data "$ROOT_DIR/data/demo" \
  --config "$ROOT_DIR/config/optimizer.conf" \
  --time-limit "${SOLVER_TIME_LIMIT_SECONDS:-15}" > "$OUTPUT_DIR/latest.json"
python3 "$ROOT_DIR/tools/benchmark_report.py" "$OUTPUT_DIR/latest.json" "$OUTPUT_DIR/latest.csv"
echo "benchmark written to $OUTPUT_DIR/latest.json and $OUTPUT_DIR/latest.csv"
