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

run_and_capture() {
    local target="$1"
    local output_var="$2"
    local status_var="$3"
    local disassembly_var="$4"
    local disassembly_status_var="$5"

    local output
    local status
    local disassembly
    local disassembly_status

    set +e
    output="$($INTERPRETER --strict "$target" 2>&1)"
    status=$?
    disassembly="$($INTERPRETER --strict --disassemble "$target" 2>&1)"
    disassembly_status=$?
    set -e

    printf -v "$output_var" '%s' "$output"
    printf -v "$status_var" '%s' "$status"
    printf -v "$disassembly_var" '%s' "$disassembly"
    printf -v "$disassembly_status_var" '%s' "$disassembly_status"
}

run_and_capture "$SCRIPT_DIR/sample_ast_opt_constant_fold.mog" \
    fold_output fold_status fold_disassembly fold_disassembly_status

if [[ $fold_status -ne 0 || $fold_disassembly_status -ne 0 ]]; then
    echo "[FAIL] constant-fold sample failed"
    echo "$fold_output"
    echo "$fold_disassembly"
    exit 1
fi

if [[ "$fold_output" != "7" ]]; then
    echo "[FAIL] constant-fold sample produced unexpected output"
    echo "$fold_output"
    exit 1
fi

if grep -q "ADD" <<< "$fold_disassembly" || grep -q "MULT" <<< "$fold_disassembly"; then
    echo "[FAIL] constant-fold sample still emits arithmetic opcodes"
    echo "$fold_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_dead_if.mog" \
    if_output if_status if_disassembly if_disassembly_status

if [[ $if_status -ne 0 || $if_disassembly_status -ne 0 ]]; then
    echo "[FAIL] dead-if sample failed"
    echo "$if_output"
    echo "$if_disassembly"
    exit 1
fi

if [[ "$if_output" != "then" ]]; then
    echo "[FAIL] dead-if sample produced unexpected output"
    echo "$if_output"
    exit 1
fi

if grep -q "JUMP_IF_FALSE_POP" <<< "$if_disassembly" || grep -q "JUMP " <<< "$if_disassembly"; then
    echo "[FAIL] dead-if sample still emits branch jumps"
    echo "$if_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_dead_while.mog" \
    while_output while_status while_disassembly while_disassembly_status

if [[ $while_status -ne 0 || $while_disassembly_status -ne 0 ]]; then
    echo "[FAIL] dead-while sample failed"
    echo "$while_output"
    echo "$while_disassembly"
    exit 1
fi

if [[ "$while_output" != "done" ]]; then
    echo "[FAIL] dead-while sample produced unexpected output"
    echo "$while_output"
    exit 1
fi

if grep -q "JUMP_IF_FALSE_POP" <<< "$while_disassembly" || grep -q "LOOP" <<< "$while_disassembly"; then
    echo "[FAIL] dead-while sample still emits loop control flow"
    echo "$while_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_typed_numeric_fold.mog" \
    typed_output typed_status typed_disassembly typed_disassembly_status

if [[ $typed_status -ne 0 || $typed_disassembly_status -ne 0 ]]; then
    echo "[FAIL] typed-fold sample failed"
    echo "$typed_output"
    echo "$typed_disassembly"
    exit 1
fi

if [[ "$typed_output" != $'3\n3.5\ntrue\ntrue' ]]; then
    echo "[FAIL] typed-fold sample produced unexpected output"
    echo "$typed_output"
    exit 1
fi

if grep -Eq "UADD|ADD|IGREATER_EQ|UGREATER_EQ|GREATER_EQUAL_THAN|NOT" <<< "$typed_disassembly"; then
    echo "[FAIL] typed-fold sample still emits typed arithmetic or comparison opcodes"
    echo "$typed_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_comparison_branch_fold.mog" \
    cmp_branch_output cmp_branch_status cmp_branch_disassembly cmp_branch_disassembly_status

if [[ $cmp_branch_status -ne 0 || $cmp_branch_disassembly_status -ne 0 ]]; then
    echo "[FAIL] comparison-branch sample failed"
    echo "$cmp_branch_output"
    echo "$cmp_branch_disassembly"
    exit 1
fi

if [[ "$cmp_branch_output" != $'then\nok' ]]; then
    echo "[FAIL] comparison-branch sample produced unexpected output"
    echo "$cmp_branch_output"
    exit 1
fi

if grep -q "JUMP_IF_FALSE_POP" <<< "$cmp_branch_disassembly" || grep -q "JUMP " <<< "$cmp_branch_disassembly"; then
    echo "[FAIL] comparison-branch sample still emits branch jumps"
    echo "$cmp_branch_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_no_side_effect_fold.mog" \
    side_effect_output side_effect_status side_effect_disassembly side_effect_disassembly_status

if [[ $side_effect_status -ne 0 || $side_effect_disassembly_status -ne 0 ]]; then
    echo "[FAIL] side-effect sample failed"
    echo "$side_effect_output"
    echo "$side_effect_disassembly"
    exit 1
fi

if [[ "$side_effect_output" != $'call\n3' ]]; then
    echo "[FAIL] side-effect sample produced unexpected output"
    echo "$side_effect_output"
    exit 1
fi

if ! grep -q "CALL" <<< "$side_effect_disassembly" || ! grep -q "ADD" <<< "$side_effect_disassembly"; then
    echo "[FAIL] side-effect sample was folded too aggressively"
    echo "$side_effect_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_bitwise_shift_fold.mog" \
    bitwise_output bitwise_status bitwise_disassembly bitwise_disassembly_status

if [[ $bitwise_status -ne 0 || $bitwise_disassembly_status -ne 0 ]]; then
    echo "[FAIL] bitwise/shift-fold sample failed"
    echo "$bitwise_output"
    echo "$bitwise_disassembly"
    exit 1
fi

if [[ "$bitwise_output" != $'2\n7\n5\n-7\n16\n8\n4\n8\n3' ]]; then
    echo "[FAIL] bitwise/shift-fold sample produced unexpected output"
    echo "$bitwise_output"
    exit 1
fi

if grep -Eq "BITWISE_AND|BITWISE_OR|BITWISE_XOR|BITWISE_NOT|SHIFT_LEFT|SHIFT_RIGHT" <<< "$bitwise_disassembly"; then
    echo "[FAIL] bitwise/shift-fold sample still emits bitwise or shift opcodes"
    echo "$bitwise_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_unreachable_return.mog" \
    return_output return_status return_disassembly return_disassembly_status

if [[ $return_status -ne 0 || $return_disassembly_status -ne 0 ]]; then
    echo "[FAIL] unreachable-return sample failed"
    echo "$return_output"
    echo "$return_disassembly"
    exit 1
fi

if [[ "$return_output" != "7" ]]; then
    echo "[FAIL] unreachable-return sample produced unexpected output"
    echo "$return_output"
    exit 1
fi

if grep -q "after" <<< "$return_disassembly"; then
    echo "[FAIL] unreachable-return sample still emits dead constants or statements"
    echo "$return_disassembly"
    exit 1
fi

echo "[PASS] AST optimizer folds pure constants, removes unreachable return tails, and preserves side-effectful expressions."
