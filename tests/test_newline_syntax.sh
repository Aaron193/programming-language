#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INTERPRETER="$PROJECT_ROOT/build/interpreter"
NEWLINE_DIR="$SCRIPT_DIR/newline"

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

    echo "[PASS] $file"
    return 0
}

run_expect_compile_error() {
    local mode="$1"
    local file="$2"
    local expected="$3"

    local -a cmd=("$INTERPRETER")
    if [[ "$mode" == "strict" ]]; then
        cmd+=("--strict")
    fi
    cmd+=("$file")

    set +e
    local output
    output="$("${cmd[@]}" 2>&1)"
    local status=$?
    set -e

    if [[ $status -eq 0 ]]; then
        echo "[FAIL] Expected compile failure ($mode): $file"
        echo "$output"
        return 1
    fi

    if ! grep -Fq "$expected" <<< "$output"; then
        echo "[FAIL] Expected message not found ($mode): $file"
        echo "Expected: $expected"
        echo "Actual output:"
        echo "$output"
        return 1
    fi

    echo "[PASS] $mode $file"
    return 0
}

failed=0

run_expect_output "$NEWLINE_DIR/sample_newline_call_suffix.mog" "3" || failed=1
run_expect_output "$NEWLINE_DIR/sample_newline_index_suffix.mog" "2" || failed=1
run_expect_output "$NEWLINE_DIR/sample_newline_member_suffix.mog" "7" || failed=1
run_expect_output "$NEWLINE_DIR/sample_newline_operator_rhs.mog" "3" || failed=1
run_expect_output "$NEWLINE_DIR/sample_newline_prefix_statement.mog" $'2\n3' || failed=1
run_expect_output "$NEWLINE_DIR/sample_newline_print_grouped_arg.mog" "3" || failed=1

for file in \
    "$NEWLINE_DIR/fail_newline_call_break.mog" \
    "$NEWLINE_DIR/fail_newline_index_break.mog" \
    "$NEWLINE_DIR/fail_newline_member_break.mog" \
    "$NEWLINE_DIR/fail_newline_operator_break.mog" \
    "$NEWLINE_DIR/fail_newline_cast_break.mog"
do
    case "$(basename "$file")" in
        fail_newline_call_break.mog)
            expected="Continuation token '(' must stay on the previous line."
            ;;
        fail_newline_index_break.mog)
            expected="Continuation token '[' must stay on the previous line."
            ;;
        fail_newline_member_break.mog)
            expected="Continuation token '.' must stay on the previous line."
            ;;
        fail_newline_operator_break.mog)
            expected="Continuation token '+' must stay on the previous line."
            ;;
        fail_newline_cast_break.mog)
            expected="Continuation token 'as' must stay on the previous line."
            ;;
    esac

    run_expect_compile_error "default" "$file" "$expected" || failed=1
    run_expect_compile_error "strict" "$file" "$expected" || failed=1
done

run_expect_compile_error \
    "default" \
    "$NEWLINE_DIR/fail_newline_trailing_comma_call.mog" \
    "Expected expression." || failed=1

if [[ $failed -ne 0 ]]; then
    exit 1
fi

echo "[PASS] newline-sensitive syntax tests"
exit 0
