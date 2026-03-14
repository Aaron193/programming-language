#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INTERPRETER="$PROJECT_ROOT/build/interpreter"
IDENTIFIER_TARGET="$SCRIPT_DIR/sample_logical_identifiers.mog"
INVALID_TARGET="$SCRIPT_DIR/sample_invalid_logical_word_sequence.mog"

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
INVALID_OUTPUT="$($INTERPRETER "$INVALID_TARGET" 2>&1)"
INVALID_STATUS=$?
set -e

if [[ $INVALID_STATUS -eq 0 ]]; then
    echo "[FAIL] expected invalid word-based logical operator syntax to fail"
    exit 1
fi

echo "[PASS] logical operators require &&/||."
exit 0
