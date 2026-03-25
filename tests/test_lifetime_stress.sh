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

run_expect_exact_output() {
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

    echo "[PASS] $file"
    return 0
}

run_expect_cache_and_finalizers() {
    local file="$1"
    local expected_total="$2"
    local expected_loads="$3"
    local expected_finalizers="$4"

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

    local load_count
    load_count=$(grep -c "^cache_handle_target_loaded$" <<< "$output")
    if [[ $load_count -ne $expected_loads ]]; then
        echo "[FAIL] Unexpected module cache load count: $file"
        echo "Expected loads: $expected_loads"
        echo "Actual loads: $load_count"
        echo "$output"
        return 1
    fi

    local finalizer_count
    finalizer_count=$(grep -c "^counter_handle_released$" <<< "$output")
    if [[ $finalizer_count -ne $expected_finalizers ]]; then
        echo "[FAIL] Unexpected handle finalizer count: $file"
        echo "Expected finalizers: $expected_finalizers"
        echo "Actual finalizers: $finalizer_count"
        echo "$output"
        return 1
    fi

    local total_line
    total_line=$(grep -v "^cache_handle_target_loaded$" <<< "$output" | \
        grep -v "^counter_handle_released$" | tail -n 1)
    if [[ "$total_line" != "$expected_total" ]]; then
        echo "[FAIL] Unexpected total output: $file"
        echo "Expected total: $expected_total"
        echo "Actual total: $total_line"
        echo "$output"
        return 1
    fi

    echo "[PASS] $file"
    return 0
}

failed=0

run_expect_exact_output \
    "$SCRIPT_DIR/sample_stress_nested_upvalue_chain.mog" \
    "2009000" || failed=1

run_expect_exact_output \
    "$SCRIPT_DIR/sample_stress_native_package_functions.mog" \
    "10200" || failed=1

run_expect_cache_and_finalizers \
    "$SCRIPT_DIR/sample_stress_import_cache_handles.mog" \
    "2550" \
    1 \
    50 || failed=1

run_expect_exact_output \
    "$SCRIPT_DIR/sample_stress_method_dispatch_chain.mog" \
    "21100" || failed=1

run_expect_exact_output \
    "$SCRIPT_DIR/sample_ast_opt_import_member_cast_stress.mog" \
    "2200" || failed=1

if [[ $failed -ne 0 ]]; then
    exit 1
fi

echo "[PASS] lifetime stress regressions"
exit 0
