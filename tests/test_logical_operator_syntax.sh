#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INTERPRETER="$PROJECT_ROOT/build/interpreter"
IDENTIFIER_TARGET="$SCRIPT_DIR/sample_logical_identifiers.expr"
LEGACY_TARGET="$SCRIPT_DIR/sample_legacy_logical_keywords.expr"

if [[ ! -x "$INTERPRETER" ]]; then
    echo "Interpreter not found at $INTERPRETER"
    echo "Build first with: $PROJECT_ROOT/build.sh"
    exit 1
fi

IDENTIFIER_OUTPUT="$($INTERPRETER "$IDENTIFIER_TARGET" 2>&1)"
if [[ $? -ne 0 ]]; then
    echo "[FAIL] expected 'and'/'or' identifiers to compile and run"
    echo "$IDENTIFIER_OUTPUT"
    exit 1
fi

if ! grep -q "^true$" <<< "$IDENTIFIER_OUTPUT" || \
   ! grep -q "^false$" <<< "$IDENTIFIER_OUTPUT"; then
    echo "[FAIL] unexpected output for logical identifier sample"
    echo "$IDENTIFIER_OUTPUT"
    exit 1
fi

set +e
LEGACY_OUTPUT="$($INTERPRETER "$LEGACY_TARGET" 2>&1)"
LEGACY_STATUS=$?
set -e

if [[ $LEGACY_STATUS -eq 0 ]]; then
    echo "[FAIL] expected legacy 'and' syntax to fail"
    exit 1
fi

echo "[PASS] logical operators require &&/|| and legacy keywords fail."
exit 0
