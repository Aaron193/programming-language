#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INTERPRETER="$ROOT_DIR/build/interpreter"
BENCH_DIR="$ROOT_DIR/benchmarks"

if [[ ! -x "$INTERPRETER" ]]; then
  echo "Interpreter not found or not executable at: $INTERPRETER"
  echo "Build first (for example: ./build.sh)."
  exit 1
fi

echo "Running benchmarks in $BENCH_DIR"
echo

for bench in "$BENCH_DIR"/bench_*.expr; do
  [[ -f "$bench" ]] || continue
  echo "=== $(basename "$bench") ==="
  "$INTERPRETER" "$bench"
  echo
done
