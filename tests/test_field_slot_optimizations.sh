#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INTERPRETER="$PROJECT_ROOT/build/interpreter"
TARGET="$SCRIPT_DIR/sample_class.mog"

if [[ ! -x "$INTERPRETER" ]]; then
    echo "Interpreter not found at $INTERPRETER"
    echo "Build first with: $PROJECT_ROOT/build.sh"
    exit 1
fi

set +e
PROGRAM_OUTPUT="$($INTERPRETER "$TARGET" 2>&1)"
PROGRAM_STATUS=$?
DISASSEMBLE_OUTPUT="$($INTERPRETER --disassemble "$TARGET" 2>&1)"
DISASSEMBLE_STATUS=$?
set -e

if [[ $PROGRAM_STATUS -ne 0 ]]; then
    echo "[FAIL] field slot sample failed at runtime"
    echo "$PROGRAM_OUTPUT"
    exit 1
fi

if [[ $DISASSEMBLE_STATUS -ne 0 ]]; then
    echo "[FAIL] field slot sample failed to disassemble"
    echo "$DISASSEMBLE_OUTPUT"
    exit 1
fi

EXPECTED_OUTPUT=$'42\nok'
if [[ "$PROGRAM_OUTPUT" != "$EXPECTED_OUTPUT" ]]; then
    echo "[FAIL] field slot sample produced unexpected output"
    echo "$PROGRAM_OUTPUT"
    exit 1
fi

if ! grep -q "GET_FIELD_SLOT" <<< "$DISASSEMBLE_OUTPUT"; then
    echo "[FAIL] disassembly missing GET_FIELD_SLOT"
    echo "$DISASSEMBLE_OUTPUT"
    exit 1
fi

if ! grep -q "SET_FIELD_SLOT" <<< "$DISASSEMBLE_OUTPUT"; then
    echo "[FAIL] disassembly missing SET_FIELD_SLOT"
    echo "$DISASSEMBLE_OUTPUT"
    exit 1
fi

echo "[PASS] field slot opcodes are emitted and execute correctly."
exit 0
