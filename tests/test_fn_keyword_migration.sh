#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INTERPRETER="$PROJECT_ROOT/build/interpreter"
TARGET="$SCRIPT_DIR/types/errors/legacy_function_keyword.expr"

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
    echo "[FAIL] Expected legacy 'function' syntax to fail."
    exit 1
fi

if ! grep -q "use 'fn'" <<< "$OUTPUT"; then
    echo "[FAIL] Expected migration guidance for legacy 'function' syntax."
    echo "$OUTPUT"
    exit 1
fi

echo "[PASS] legacy 'function' keyword reports migration guidance."
exit 0
