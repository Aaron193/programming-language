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

run_expect_output() {
    local file="$1"
    local expected="$2"

    set +e
    local output
    output="$($INTERPRETER "$file" 2>&1)"
    local status=$?
    set -e

    if [[ $status -ne 0 ]]; then
        echo "[FAIL] Expected success: $file"
        echo "$output"
        return 1
    fi

    if [[ "$output" != "$expected" ]]; then
        echo "[FAIL] Unexpected output: $file"
        echo "Expected:"
        printf '%s\n' "$expected"
        echo "Actual:"
        printf '%s\n' "$output"
        return 1
    fi

    echo "[PASS] runtime output: $file"
    return 0
}

run_expect_compile_error_contains() {
    local file="$1"
    local expected="$2"

    set +e
    local output
    output="$($INTERPRETER "$file" 2>&1)"
    local status=$?
    set -e

    if [[ $status -eq 0 ]]; then
        echo "[FAIL] Expected compile failure: $file"
        echo "$output"
        return 1
    fi

    if ! grep -Fq "$expected" <<< "$output"; then
        echo "[FAIL] Expected message not found: $file"
        echo "Expected: $expected"
        echo "Actual output:"
        echo "$output"
        return 1
    fi

    echo "[PASS] compile error: $file"
    return 0
}

failed=0

run_expect_output \
    "$SCRIPT_DIR/sample_contextual_numeric_literals.mog" \
    $'3\n1\n2\n3\n[1, 2, 3]\n9\ntrue\ntrue' || failed=1

run_expect_compile_error_contains \
    "$SCRIPT_DIR/types/errors/contextual_numeric_overflow_u8.mog" \
    "integer literal '256' is out of range for type 'u8'" || failed=1

run_expect_compile_error_contains \
    "$SCRIPT_DIR/types/errors/contextual_decimal_to_integer.mog" \
    "decimal literal '1.5' cannot be inferred as integer type 'i32'" || failed=1

run_expect_compile_error_contains \
    "$SCRIPT_DIR/types/errors/invalid_numeric_suffix_contextual.mog" \
    "Invalid numeric literal suffix." || failed=1

if [[ $failed -ne 0 ]]; then
    exit 1
fi

echo "[PASS] contextual numeric literal tests"
exit 0
