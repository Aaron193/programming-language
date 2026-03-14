#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INTERPRETER="$PROJECT_ROOT/build/interpreter"
TARGET="$SCRIPT_DIR/sample_invoke_fusion.mog"

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
TRACE_OUTPUT="$($INTERPRETER --trace "$TARGET" 2>&1)"
TRACE_STATUS=$?
set -e

if [[ $PROGRAM_STATUS -ne 0 ]]; then
    echo "[FAIL] invoke fusion sample failed at runtime"
    echo "$PROGRAM_OUTPUT"
    exit 1
fi

if [[ $DISASSEMBLE_STATUS -ne 0 ]]; then
    echo "[FAIL] invoke fusion sample failed to disassemble"
    echo "$DISASSEMBLE_OUTPUT"
    exit 1
fi

if [[ $TRACE_STATUS -ne 0 ]]; then
    echo "[FAIL] invoke fusion sample failed under trace"
    echo "$TRACE_OUTPUT"
    exit 1
fi

EXPECTED_OUTPUT=$'base-derived\nbase-derived\n2\n3'
if [[ "$PROGRAM_OUTPUT" != "$EXPECTED_OUTPUT" ]]; then
    echo "[FAIL] invoke fusion sample produced unexpected output"
    echo "$PROGRAM_OUTPUT"
    exit 1
fi

if ! grep -q "INVOKE " <<< "$DISASSEMBLE_OUTPUT"; then
    echo "[FAIL] disassembly missing INVOKE"
    echo "$DISASSEMBLE_OUTPUT"
    exit 1
fi

if ! grep -q "INVOKE_SUPER" <<< "$TRACE_OUTPUT"; then
    echo "[FAIL] trace missing INVOKE_SUPER"
    echo "$TRACE_OUTPUT"
    exit 1
fi

if grep -q "GET_SUPER" <<< "$TRACE_OUTPUT"; then
    echo "[FAIL] trace still hits GET_SUPER on fused super call"
    echo "$TRACE_OUTPUT"
    exit 1
fi

if ! grep -q "GET_PROPERTY" <<< "$DISASSEMBLE_OUTPUT"; then
    echo "[FAIL] disassembly missing GET_PROPERTY for extracted method path"
    echo "$DISASSEMBLE_OUTPUT"
    exit 1
fi

echo "[PASS] fused invoke opcodes are emitted and execute correctly."
exit 0
