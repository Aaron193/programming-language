#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"
BENCH_DIR="$ROOT_DIR/benchmarks/python"

if ! command -v "$PYTHON_BIN" >/dev/null 2>&1; then
  echo "Python interpreter not found on PATH: $PYTHON_BIN"
  exit 1
fi

echo "Running Python benchmarks in $BENCH_DIR"
echo

for bench in "$BENCH_DIR"/bench_*.py; do
  [[ -f "$bench" ]] || continue
  echo "=== $(basename "$bench") ==="
  "$PYTHON_BIN" "$bench"
  echo
done
