#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INTERPRETER="$PROJECT_ROOT/build/interpreter"
TARGET="$SCRIPT_DIR/sample_compile_multi_error.expr"

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
    echo "[FAIL] Expected compile failure but execution succeeded."
    exit 1
fi

ERROR_COUNT=$(grep -c "\[error\]\[compile\]" <<< "$OUTPUT")
if [[ $ERROR_COUNT -lt 2 ]]; then
    echo "[FAIL] Expected at least 2 compile errors, got $ERROR_COUNT"
    echo "$OUTPUT"
    exit 1
fi

echo "[PASS] compile recovery reported multiple errors ($ERROR_COUNT)."
exit 0
