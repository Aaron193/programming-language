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

PASS_FILE="$SCRIPT_DIR/types/runtime/runtime_type_guard_pass.expr"
FAIL_FILE="$SCRIPT_DIR/types/runtime/runtime_type_guard_fail.expr"
PARAM_FAIL_FILE="$SCRIPT_DIR/types/runtime/runtime_typed_param_guard_fail.expr"
RETURN_FAIL_FILE="$SCRIPT_DIR/types/runtime/runtime_typed_return_guard_fail.expr"

if ! "$INTERPRETER" "$PASS_FILE" >/tmp/phase8_pass.out 2>/tmp/phase8_pass.err; then
    echo "[FAIL] Expected successful runtime type check: $PASS_FILE"
    cat /tmp/phase8_pass.err
    exit 1
fi

set +e
FAIL_OUTPUT="$($INTERPRETER "$FAIL_FILE" 2>&1)"
FAIL_STATUS=$?
set -e

if [[ $FAIL_STATUS -eq 0 ]]; then
    echo "[FAIL] Expected runtime type error: $FAIL_FILE"
    exit 1
fi

if ! grep -Fq "expected instance of 'Dog', got 'Cat'" <<< "$FAIL_OUTPUT"; then
    echo "[FAIL] Expected runtime type guard message was not found."
    echo "$FAIL_OUTPUT"
    exit 1
fi

set +e
PARAM_FAIL_OUTPUT="$($INTERPRETER "$PARAM_FAIL_FILE" 2>&1)"
PARAM_FAIL_STATUS=$?
set -e

if [[ $PARAM_FAIL_STATUS -eq 0 ]]; then
    echo "[FAIL] Expected runtime type error: $PARAM_FAIL_FILE"
    exit 1
fi

if ! grep -Fq "expected instance of 'Dog', got 'Cat'" <<< "$PARAM_FAIL_OUTPUT"; then
    echo "[FAIL] Expected typed parameter runtime type guard message was not found."
    echo "$PARAM_FAIL_OUTPUT"
    exit 1
fi

set +e
RETURN_FAIL_OUTPUT="$($INTERPRETER "$RETURN_FAIL_FILE" 2>&1)"
RETURN_FAIL_STATUS=$?
set -e

if [[ $RETURN_FAIL_STATUS -eq 0 ]]; then
    echo "[FAIL] Expected runtime type error: $RETURN_FAIL_FILE"
    exit 1
fi

if ! grep -Fq "expected instance of 'Dog', got 'Cat'" <<< "$RETURN_FAIL_OUTPUT"; then
    echo "[FAIL] Expected typed return runtime type guard message was not found."
    echo "$RETURN_FAIL_OUTPUT"
    exit 1
fi

echo "[PASS] runtime class type checks"
exit 0
