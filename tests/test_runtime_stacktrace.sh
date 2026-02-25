#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INTERPRETER="$PROJECT_ROOT/build/interpreter"
TARGET="$SCRIPT_DIR/sample_runtime_stacktrace.expr"

if [[ ! -x "$INTERPRETER" ]]; then
    echo "Interpreter not found at $INTERPRETER"
    echo "Build first with: $PROJECT_ROOT/build.sh"
    exit 1
fi

set +e
OUTPUT="$($INTERPRETER "$TARGET" 2>&1)"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "[FAIL] Expected runtime failure but execution succeeded."
    exit 1
fi

if ! grep -q "\[trace\]\[runtime\] stack:" <<< "$OUTPUT"; then
    echo "[FAIL] Missing runtime stack trace header."
    echo "$OUTPUT"
    exit 1
fi

if ! grep -q "at c()" <<< "$OUTPUT" || ! grep -q "at b()" <<< "$OUTPUT" || ! grep -q "at a()" <<< "$OUTPUT"; then
    echo "[FAIL] Missing expected function frames in stack trace."
    echo "$OUTPUT"
    exit 1
fi

echo "[PASS] runtime stack trace includes nested call frames."
exit 0
