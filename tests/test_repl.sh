#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INTERPRETER="$PROJECT_ROOT/build/interpreter"

if [[ ! -x "$INTERPRETER" ]]; then
    echo "Interpreter not found at $INTERPRETER"
    echo "Build first with: $PROJECT_ROOT/build.sh"
    exit 1
fi

set +e
OUTPUT="$(printf 'print 1;\nprint 2;\nquit\n' | "$INTERPRETER" 2>&1)"
STATUS=$?
set -e

if [[ $STATUS -ne 0 ]]; then
    echo "[FAIL] REPL execution failed"
    echo "$OUTPUT"
    exit 1
fi

if ! grep -q "1" <<< "$OUTPUT" || ! grep -q "2" <<< "$OUTPUT"; then
    echo "[FAIL] REPL output missing expected values"
    echo "$OUTPUT"
    exit 1
fi

if ! grep -q ">>" <<< "$OUTPUT"; then
    echo "[FAIL] REPL prompt missing"
    echo "$OUTPUT"
    exit 1
fi

echo "[PASS] REPL accepts multiple lines and prints results."
exit 0
