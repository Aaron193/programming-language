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

run_expect_compile_error() {
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

    echo "[PASS] $file"
    return 0
}

failed=0

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/assign_str_to_i32.expr" \
    "cannot assign 'str' to variable 'age' of type 'i32'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/function_arg_type_mismatch.expr" \
    "function argument 1 expects 'i32', got 'str'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/return_type_mismatch.expr" \
    "cannot return 'str' from function returning 'i32'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/invalid_cast_str_to_i32.expr" \
    "cannot cast 'str' to 'i32'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/subtype_assignment_invalid.expr" \
    "cannot assign 'Animal' to variable 'd' of type 'Dog'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/assign_array_str_to_array_i32.expr" \
    "cannot assign 'Array<str>' to variable 'nums' of type 'Array<i32>'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/assign_dict_value_str_to_bool.expr" \
    "cannot assign 'Dict<str, str>' to variable 'flags' of type 'Dict<str, bool>'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/assign_null_to_non_optional.expr" \
    "cannot assign 'null' to variable 'name' of type 'str'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/optional_member_access_without_check.expr" \
    "cannot access members on optional value of type 'Dog?' without a null check" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/optional_call_without_check.expr" \
    "cannot call optional value of type 'Dog?' without a null check" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/function_closure_param_count_mismatch.expr" \
    "closure parameter count mismatch: expected 1, got 2." || failed=1

if [[ $failed -ne 0 ]]; then
    exit 1
fi

echo "[PASS] type checker negative tests"
exit 0
