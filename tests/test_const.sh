#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INTERPRETER="$PROJECT_ROOT/build/interpreter"
POSITIVE_TARGET="$SCRIPT_DIR/sample_const.expr"
NEGATIVE_TARGET="$SCRIPT_DIR/types/errors/assign_to_const.expr"

if [[ ! -x "$INTERPRETER" ]]; then
    echo "Interpreter not found at $INTERPRETER"
    echo "Build first with: $PROJECT_ROOT/build.sh"
    exit 1
fi

set +e
POSITIVE_OUTPUT="$($INTERPRETER "$POSITIVE_TARGET" 2>&1)"
POSITIVE_STATUS=$?
set -e

if [[ $POSITIVE_STATUS -ne 0 ]]; then
    echo "[FAIL] const sample execution failed"
    echo "$POSITIVE_OUTPUT"
    exit 1
fi

EXPECTED_OUTPUT=$'10\n12'
if [[ "$POSITIVE_OUTPUT" != "$EXPECTED_OUTPUT" ]]; then
    echo "[FAIL] const sample produced unexpected output"
    echo "Expected:"
    printf '%s\n' "$EXPECTED_OUTPUT"
    echo "Actual:"
    printf '%s\n' "$POSITIVE_OUTPUT"
    exit 1
fi

set +e
NEGATIVE_OUTPUT="$($INTERPRETER "$NEGATIVE_TARGET" 2>&1)"
NEGATIVE_STATUS=$?
set -e

if [[ $NEGATIVE_STATUS -eq 0 ]]; then
    echo "[FAIL] expected const reassignment to fail without --strict"
    exit 1
fi

if ! grep -Fq "Cannot assign to const variable 'value'." <<< "$NEGATIVE_OUTPUT"; then
    echo "[FAIL] expected non-strict compiler const error"
    echo "$NEGATIVE_OUTPUT"
    exit 1
fi

echo "[PASS] const declarations work and reassignment fails in compiler mode."
exit 0
