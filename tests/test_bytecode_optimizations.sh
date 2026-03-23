#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INTERPRETER="$PROJECT_ROOT/build/interpreter"
TARGET="$SCRIPT_DIR/sample_strict_loop_optimizations.mog"

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
    echo "[FAIL] optimized loop sample failed at runtime"
    echo "$PROGRAM_OUTPUT"
    exit 1
fi

if [[ $DISASSEMBLE_STATUS -ne 0 ]]; then
    echo "[FAIL] optimized loop sample failed to disassemble"
    echo "$DISASSEMBLE_OUTPUT"
    exit 1
fi

if ! grep -q "^7$" <<< "$PROGRAM_OUTPUT"; then
    echo "[FAIL] optimized loop sample produced unexpected output"
    echo "$PROGRAM_OUTPUT"
    exit 1
fi

if ! grep -q "JUMP_IF_FALSE_POP" <<< "$DISASSEMBLE_OUTPUT"; then
    echo "[FAIL] disassembly missing JUMP_IF_FALSE_POP"
    echo "$DISASSEMBLE_OUTPUT"
    exit 1
fi

if ! grep -q "ITER_HAS_NEXT_JUMP" <<< "$DISASSEMBLE_OUTPUT"; then
    echo "[FAIL] disassembly missing ITER_HAS_NEXT_JUMP"
    echo "$DISASSEMBLE_OUTPUT"
    exit 1
fi

if ! grep -q "ITER_NEXT_SET_LOCAL" <<< "$DISASSEMBLE_OUTPUT"; then
    echo "[FAIL] disassembly missing ITER_NEXT_SET_LOCAL"
    echo "$DISASSEMBLE_OUTPUT"
    exit 1
fi

echo "[PASS] fused loop bytecode opcodes are emitted and execute correctly."
exit 0
