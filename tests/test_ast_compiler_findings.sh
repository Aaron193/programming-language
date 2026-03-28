#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INTERPRETER="$PROJECT_ROOT/build/interpreter"
FINDINGS_DIR="$SCRIPT_DIR/findings"

if [[ ! -x "$INTERPRETER" ]]; then
    echo "Interpreter not found at $INTERPRETER"
    echo "Build first with: $PROJECT_ROOT/build.sh"
    exit 1
fi

run_expect_compile_error_contains() {
    local file="$1"
    local needle="$2"
    local expected_location="${3:-}"

    local -a cmd=("$INTERPRETER")
    cmd+=("$file")

    set +e
    local output
    output="$("${cmd[@]}" 2>&1)"
    local status=$?
    set -e

    if [[ $status -eq 0 ]]; then
        echo "[FAIL] Expected compile failure: $file"
        echo "$output"
        return 1
    fi

    if ! grep -Eiq "$needle" <<< "$output"; then
        echo "[FAIL] Expected compile error message not found: $file"
        echo "Expected regex: $needle"
        echo "Actual output:"
        echo "$output"
        return 1
    fi

    if [[ -n "$expected_location" ]] &&
       ! grep -Fq "[error][compile][line $expected_location]" <<< "$output"; then
        echo "[FAIL] Expected exact diagnostic location not found: $file"
        echo "Expected location: $expected_location"
        echo "Actual output:"
        echo "$output"
        return 1
    fi

    echo "[PASS] compile error: $file"
    return 0
}

run_expect_runtime_error_contains() {
    local file="$1"
    local needle="$2"

    set +e
    local output
    output="$($INTERPRETER "$file" 2>&1)"
    local status=$?
    set -e

    if [[ $status -eq 0 ]]; then
        echo "[FAIL] Expected runtime failure: $file"
        echo "$output"
        return 1
    fi

    if ! grep -Fq "$needle" <<< "$output"; then
        echo "[FAIL] Expected runtime error text not found: $file"
        echo "Expected: $needle"
        echo "Actual output:"
        echo "$output"
        return 1
    fi

    echo "[PASS] runtime error: $file"
    return 0
}

run_expect_success_output() {
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

run_expect_disassembly_contains() {
    local file="$1"
    local needle="$2"
    local forbidden="${3:-}"

    set +e
    local output
    output="$($INTERPRETER --disassemble "$file" 2>&1)"
    local status=$?
    set -e

    if ! grep -Fq "$needle" <<< "$output"; then
        echo "[FAIL] Expected disassembly opcode not found: $file"
        echo "Expected opcode: $needle"
        echo "$output"
        return 1
    fi

    if [[ -n "$forbidden" ]] && grep -Fq "$forbidden" <<< "$output"; then
        echo "[FAIL] Forbidden disassembly opcode found: $file"
        echo "Forbidden opcode: $forbidden"
        echo "$output"
        return 1
    fi

    echo "[PASS] disassembly check: $file"
    return 0
}

failed=0

run_expect_compile_error_contains \
    "$FINDINGS_DIR/fail_numeric_overflow_signed.mog" \
    "numeric literal" || failed=1
run_expect_compile_error_contains \
    "$FINDINGS_DIR/fail_numeric_overflow_unsigned.mog" \
    "numeric literal" || failed=1
run_expect_compile_error_contains \
    "$FINDINGS_DIR/fail_numeric_overflow_float.mog" \
    "numeric literal" || failed=1

run_expect_disassembly_contains \
    "$FINDINGS_DIR/fail_cast_downcast_runtime.mog" \
    "CHECK_INSTANCE_TYPE" || failed=1

run_expect_runtime_error_contains \
    "$FINDINGS_DIR/fail_cast_downcast_runtime.mog" \
    "Type error: expected instance of 'Dog'" || failed=1

run_expect_success_output \
    "$FINDINGS_DIR/sample_cast_downcast_runtime_ok.mog" \
    "ok" || failed=1

run_expect_compile_error_contains \
    "$FINDINGS_DIR/fail_strict_line_top_level.mog" \
    "AST frontend failed to parse source" \
    "1:1" || failed=1
run_expect_compile_error_contains \
    "$FINDINGS_DIR/fail_import_non_string_arg.mog" \
    "Expected string literal but found '@'\\." \
    "1:23" || failed=1

run_expect_compile_error_contains \
    "$FINDINGS_DIR/fail_label_on_non_loop.mog" \
    "Labels may only be attached to 'while' or 'for' statements" \
    "1:1" || failed=1

run_expect_success_output \
    "$SCRIPT_DIR/sample_break_continue.mog" \
    $'2\n1\n3\n4\n10\n12' || failed=1

run_expect_disassembly_contains \
    "$FINDINGS_DIR/sample_constructor_field_slot.mog" \
    "GET_FIELD_SLOT" \
    "GET_PROPERTY" || failed=1

if [[ $failed -ne 0 ]]; then
    exit 1
fi

echo "[PASS] AST compiler findings regression tests"
exit 0
