#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCH_BIN_A="$ROOT_DIR/build/frontend_benchmark"
BENCH_BIN_B=""
ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --benchmarker-a)
      BENCH_BIN_A="$2"
      shift 2
      ;;
    --benchmarker-b)
      BENCH_BIN_B="$2"
      shift 2
      ;;
    *)
      ARGS+=("$1")
      shift
      ;;
  esac
done

if [[ -n "$BENCH_BIN_B" ]]; then
  exec "$ROOT_DIR/benchmarks/compare_benchmarks.sh" \
    --interpreter-a "$BENCH_BIN_A" \
    --interpreter-b "$BENCH_BIN_B" \
    "${ARGS[@]}"
fi

exec "$ROOT_DIR/benchmarks/compare_benchmarks.sh" \
  --interpreter-a "$BENCH_BIN_A" \
  "${ARGS[@]}"
