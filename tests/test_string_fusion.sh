#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INTERPRETER="$PROJECT_ROOT/build/interpreter"
TARGET="$SCRIPT_DIR/sample_string_concat_fusion.mog"

if [[ ! -x "$INTERPRETER" ]]; then
    echo "Interpreter not found at $INTERPRETER"
    echo "Build first with: $PROJECT_ROOT/build.sh"
    exit 1
fi

set +e
PROGRAM_OUTPUT="$($INTERPRETER "$TARGET" 2>&1)"
PROGRAM_STATUS=$?
set -e

if [[ $PROGRAM_STATUS -ne 0 ]]; then
    echo "[FAIL] string fusion sample failed at runtime"
    echo "$PROGRAM_OUTPUT"
    exit 1
fi

EXPECTED_OUTPUT=$'100\n5\nvalue:42\n42!\nxy'
if [[ "$PROGRAM_OUTPUT" != "$EXPECTED_OUTPUT" ]]; then
    echo "[FAIL] string fusion sample produced unexpected output"
    echo "$PROGRAM_OUTPUT"
    exit 1
fi

echo "[PASS] fused string generation preserves runtime behavior."
exit 0
