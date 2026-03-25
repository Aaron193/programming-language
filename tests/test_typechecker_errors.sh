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
    local expected_location="${3:-}"

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

    if ! grep -Eq "\\[error\\]\\[compile\\]\\[line [0-9]+:[0-9]+\\]" <<< "$output"; then
        echo "[FAIL] Expected source line:column diagnostic prefix: $file"
        echo "$output"
        return 1
    fi

    if [[ -n "$expected_location" ]] && ! grep -Fq "[error][compile][line $expected_location]" <<< "$output"; then
        echo "[FAIL] Expected exact diagnostic location not found: $file"
        echo "Expected location: $expected_location"
        echo "Actual output:"
        echo "$output"
        return 1
    fi

    echo "[PASS] $file"
    return 0
}

failed=0

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/assign_str_to_i32.mog" \
    "cannot assign 'str' to variable 'age' of type 'i32'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/function_arg_type_mismatch.mog" \
    "function argument 1 expects 'i32', got 'str'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/return_type_mismatch.mog" \
    "cannot return 'str' from function returning 'i32'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/invalid_cast_str_to_i32.mog" \
    "cannot cast 'str' to 'i32'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/subtype_assignment_invalid.mog" \
    "cannot assign 'Animal' to variable 'd' of type 'Dog'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/assign_array_str_to_array_i32.mog" \
    "cannot assign 'Array<str>' to variable 'nums' of type 'Array<i32>'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/assign_dict_value_str_to_bool.mog" \
    "cannot assign 'Dict<str, str>' to variable 'flags' of type 'Dict<str, bool>'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/assign_null_to_non_optional.mog" \
    "cannot assign 'null' to variable 'name' of type 'str'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/optional_member_access_without_check.mog" \
    "cannot access members on optional value of type 'Dog?' without a null check" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/optional_call_without_check.mog" \
    "cannot call optional value of type 'Dog?' without a null check" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/function_closure_param_count_mismatch.mog" \
    "closure parameter count mismatch: expected 1, got 2." \
    "1:23" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/assign_to_const.mog" \
    "cannot assign to const variable 'value'." || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/compound_assign_to_const.mog" \
    "cannot assign to const variable 'total'." || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/compound_bitwise_to_const.mog" \
    "cannot assign to const variable 'total'." || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/bitwise_bool_operand.mog" \
    "bitwise operators require integer operands" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/bitwise_float_operand.mog" \
    "bitwise operators require integer operands" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/unary_bitwise_str_operand.mog" \
    "unary '~' expects an integer operand" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/shift_float_rhs.mog" \
    "shift operators require integer operands" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/compound_bitwise_result_unassignable.mog" \
    "result of compound assignment is not assignable to 'u8'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/update_const.mog" \
    "cannot assign to const variable 'count'." || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/closure_assign_to_const.mog" \
    "cannot assign to const variable 'value'." || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/lambda_missing_param_type.mog" \
    "expression-bodied lambdas require explicit parameter types." \
    "1:29" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/lambda_return_type_mismatch.mog" \
    "cannot assign 'function(i32) -> str' to variable 'bad' of type 'function(i32) -> i32'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/lambda_block_body.mog" \
    "expression-bodied lambdas do not support block bodies" \
    "1:36" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/assign_handle_foreign_type.mog" \
    "cannot assign 'handle<examples:counter:CounterHandle>' to variable 'bad' of type 'handle<examples:math:CounterHandle>'" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/import_binding_type_mismatch.mog" \
    "cannot assign imported value 'function(i32, i32) -> i32' to binding 'Add' of type 'function(f64, f64) -> f64'" \
    "1:9" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/import_missing_export.mog" \
    "imported module" \
    "1:9" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/import_value_used_as_type.mog" \
    "expected type after variable name." \
    "3:5" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/import_native_binding_type_mismatch.mog" \
    "cannot assign imported value 'function(i64, i64) -> i64' to binding 'addI64' of type 'function(f64, f64) -> f64'" \
    "1:9" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/import_native_handle_binding_type_mismatch.mog" \
    "cannot assign imported value 'function(i64) -> handle<examples:counter:CounterHandle>' to binding 'create' of type 'function(i64) -> handle<examples:math:CounterHandle>'" \
    "1:9" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/import_cycle_frontend.mog" \
    "Circular import detected" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/break_outside_loop.mog" \
    "cannot break outside of a loop" \
    "1:1" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/continue_outside_loop.mog" \
    "cannot continue outside of a loop" \
    "1:1" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/unknown_loop_label.mog" \
    "unknown loop label 'missing' for 'break'" \
    "2:11" || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/duplicate_loop_label.mog" \
    "duplicate loop label 'outer'" \
    "2:5" || failed=1

if [[ $failed -ne 0 ]]; then
    exit 1
fi

echo "[PASS] type checker negative tests"
exit 0
