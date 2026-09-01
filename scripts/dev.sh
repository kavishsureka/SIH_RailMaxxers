#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ ! -f "$ROOT_DIR/.env" ]]; then
  echo "Missing .env. Run 'make env' first." >&2
  exit 1
fi

set -a
# shellcheck disable=SC1091
source "$ROOT_DIR/.env"
set +a

export OPTIMIZER_BIN="$ROOT_DIR/build/optimizer/sih-optimizer"
export DATA_ROOT="$ROOT_DIR/data/scenarios"
export OPTIMIZER_CONFIG="$ROOT_DIR/config/optimizer.conf"

cleanup() {
  if [[ -n "${API_PID:-}" ]]; then
    kill "$API_PID" 2>/dev/null || true
    wait "$API_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

echo "Starting API on ${API_ADDR:-:8080}"
(cd "$ROOT_DIR/backend" && go run ./cmd/api) &
API_PID=$!

echo "Starting dashboard at http://localhost:3000"
cd "$ROOT_DIR/frontend"
npm run dev
