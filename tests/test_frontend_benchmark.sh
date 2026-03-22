#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BENCHMARK_BIN="$PROJECT_ROOT/build/frontend_benchmark"
BENCHMARK_SCRIPT="$PROJECT_ROOT/benchmarks/compare_frontend_benchmarks.sh"
TARGET="$SCRIPT_DIR/sample_var.mog"

if [[ ! -x "$BENCHMARK_BIN" ]]; then
    echo "Frontend benchmark binary not found at $BENCHMARK_BIN"
    echo "Build first with: $PROJECT_ROOT/build.sh"
    exit 1
fi

if [[ ! -x "$BENCHMARK_SCRIPT" ]]; then
    echo "Frontend benchmark wrapper not executable at $BENCHMARK_SCRIPT"
    exit 1
fi

set +e
PLAIN_OUTPUT="$($BENCHMARK_BIN "$TARGET" 2>&1)"
PLAIN_STATUS=$?
JSON_OUTPUT="$($BENCHMARK_BIN --json "$TARGET" 2>&1)"
JSON_STATUS=$?
WRAPPER_OUTPUT="$($BENCHMARK_SCRIPT --quiet --iterations 1 --warmup 0 --filter "$TARGET" 2>&1)"
WRAPPER_STATUS=$?
set -e

if [[ $PLAIN_STATUS -ne 0 ]]; then
    echo "[FAIL] frontend_benchmark plain run failed"
    echo "$PLAIN_OUTPUT"
    exit 1
fi
if ! [[ "$PLAIN_OUTPUT" =~ ^[0-9]+$ ]]; then
    echo "[FAIL] frontend_benchmark plain output was not numeric"
    echo "$PLAIN_OUTPUT"
    exit 1
fi

if [[ $JSON_STATUS -ne 0 ]]; then
    echo "[FAIL] frontend_benchmark JSON run failed"
    echo "$JSON_OUTPUT"
    exit 1
fi
if ! grep -q '"totalMicros":[0-9][0-9]*' <<< "$JSON_OUTPUT"; then
    echo "[FAIL] frontend_benchmark JSON output missing totalMicros"
    echo "$JSON_OUTPUT"
    exit 1
fi

if [[ $WRAPPER_STATUS -ne 0 ]]; then
    echo "[FAIL] compare_frontend_benchmarks wrapper failed"
    echo "$WRAPPER_OUTPUT"
    exit 1
fi
if ! grep -q "benchmark" <<< "$WRAPPER_OUTPUT"; then
    echo "[FAIL] compare_frontend_benchmarks wrapper output missing summary table"
    echo "$WRAPPER_OUTPUT"
    exit 1
fi

echo "[PASS] frontend benchmark helper and wrapper work."
