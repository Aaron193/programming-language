#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INTERPRETER="$PROJECT_ROOT/build/interpreter"
TARGET="$SCRIPT_DIR/sample_var.expr"

if [[ ! -x "$INTERPRETER" ]]; then
    echo "Interpreter not found at $INTERPRETER"
    echo "Build first with: $PROJECT_ROOT/build.sh"
    exit 1
fi

set +e
TRACE_OUTPUT="$($INTERPRETER --trace "$TARGET" 2>&1)"
TRACE_STATUS=$?
SHOW_RETURN_OUTPUT="$($INTERPRETER --show-return "$TARGET" 2>&1)"
SHOW_RETURN_STATUS=$?
DISASSEMBLE_OUTPUT="$($INTERPRETER --disassemble "$TARGET" 2>&1)"
DISASSEMBLE_STATUS=$?
set -e

if [[ $TRACE_STATUS -ne 0 ]]; then
    echo "[FAIL] --trace execution failed"
    echo "$TRACE_OUTPUT"
    exit 1
fi
if ! grep -q "LINE:" <<< "$TRACE_OUTPUT"; then
    echo "[FAIL] --trace output missing instruction trace"
    echo "$TRACE_OUTPUT"
    exit 1
fi

if [[ $SHOW_RETURN_STATUS -ne 0 ]]; then
    echo "[FAIL] --show-return execution failed"
    echo "$SHOW_RETURN_OUTPUT"
    exit 1
fi
if ! grep -q "Return constant:" <<< "$SHOW_RETURN_OUTPUT"; then
    echo "[FAIL] --show-return output missing return value"
    echo "$SHOW_RETURN_OUTPUT"
    exit 1
fi

if [[ $DISASSEMBLE_STATUS -ne 0 ]]; then
    echo "[FAIL] --disassemble execution failed"
    echo "$DISASSEMBLE_OUTPUT"
    exit 1
fi
if ! grep -q "== disassembly ==" <<< "$DISASSEMBLE_OUTPUT"; then
    echo "[FAIL] --disassemble output missing header"
    echo "$DISASSEMBLE_OUTPUT"
    exit 1
fi

if ! grep -q "42" <<< "$TRACE_OUTPUT" || ! grep -q "42" <<< "$SHOW_RETURN_OUTPUT" || ! grep -q "42" <<< "$DISASSEMBLE_OUTPUT"; then
    echo "[FAIL] expected program output missing for one or more flag runs"
    exit 1
fi

echo "[PASS] CLI flags --trace, --show-return, --disassemble work."
exit 0
