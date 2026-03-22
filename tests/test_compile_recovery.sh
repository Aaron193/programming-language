#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INTERPRETER="$PROJECT_ROOT/build/interpreter"
TARGET="$SCRIPT_DIR/sample_compile_multi_error.mog"

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

if ! grep -q "\[error\]\[compile\]\[line 1:1\] AST frontend failed to parse source\." <<< "$OUTPUT"; then
    echo "[FAIL] Expected direct AST frontend parse failure."
    echo "$OUTPUT"
    exit 1
fi

if ! grep -q "\[error\]\[compile\]\[line 2:1\] Expected expression\." <<< "$OUTPUT"; then
    echo "[FAIL] Expected source-accurate parser diagnostic."
    echo "$OUTPUT"
    exit 1
fi

if grep -q "at 'print' Expected expression\." <<< "$OUTPUT"; then
    echo "[FAIL] Legacy parser diagnostics appeared; normal compilation still fell back."
    echo "$OUTPUT"
    exit 1
fi

if grep -q "at end Expected expression\." <<< "$OUTPUT"; then
    echo "[FAIL] Legacy parser diagnostics appeared; normal compilation still fell back."
    echo "$OUTPUT"
    exit 1
fi

echo "[PASS] AST frontend compile failure surfaced without legacy fallback."
exit 0
