#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT_DIR="$ROOT_DIR/benchmark-results"
mkdir -p "$OUTPUT_DIR"
ML_PYTHON="${ML_PYTHON:-$ROOT_DIR/work/ml-venv/bin/python}"
PRIORITIES="$(mktemp "${TMPDIR:-/tmp}/railblock-priorities.XXXXXX.csv")"
trap 'rm -f "$PRIORITIES"' EXIT
cd "$ROOT_DIR"
"$ML_PYTHON" -m ml.src.inference \
  --tasks "$ROOT_DIR/data/scenarios/scenario-alpha/tasks.csv" \
  --model "$ROOT_DIR/ml/models/priority_gbr_v1.joblib" \
  --metadata "$ROOT_DIR/ml/models/priority_gbr_v1.metadata.json" \
  --output-csv "$PRIORITIES" >/dev/null
"$ROOT_DIR/build/optimizer/sih-optimizer" benchmark \
  --data "$ROOT_DIR/data/scenarios/scenario-alpha" \
  --priorities "$PRIORITIES" \
  --config "$ROOT_DIR/config/optimizer.conf" \
  --time-limit "${SOLVER_TIME_LIMIT_SECONDS:-15}" > "$OUTPUT_DIR/latest.json"
python3 "$ROOT_DIR/tools/benchmark_report.py" "$OUTPUT_DIR/latest.json" "$OUTPUT_DIR/latest.csv"
echo "benchmark written to $OUTPUT_DIR/latest.json and $OUTPUT_DIR/latest.csv"
