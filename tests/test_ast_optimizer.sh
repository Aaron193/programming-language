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

run_and_capture_mode() {
    local target="$1"
    local mode="$2"
    local output_var="$3"
    local status_var="$4"
    local disassembly_var="$5"
    local disassembly_status_var="$6"

    local output
    local status
    local disassembly
    local disassembly_status
    local -a cmd=("$INTERPRETER")
    local -a disassemble_cmd=("$INTERPRETER")

    if [[ "$mode" == "strict" ]]; then
        cmd+=("--strict")
        disassemble_cmd+=("--strict")
    fi

    cmd+=("$target")
    disassemble_cmd+=("--disassemble" "$target")

    set +e
    output="$("${cmd[@]}" 2>&1)"
    status=$?
    disassembly="$("${disassemble_cmd[@]}" 2>&1)"
    disassembly_status=$?
    set -e

    printf -v "$output_var" '%s' "$output"
    printf -v "$status_var" '%s' "$status"
    printf -v "$disassembly_var" '%s' "$disassembly"
    printf -v "$disassembly_status_var" '%s' "$disassembly_status"
}

run_and_capture_strict() {
    run_and_capture_mode "$1" "strict" "$2" "$3" "$4" "$5"
}

run_and_capture_non_strict() {
    run_and_capture_mode "$1" "non-strict" "$2" "$3" "$4" "$5"
}

run_and_capture_strict "$SCRIPT_DIR/sample_ast_opt_constant_fold.mog" \
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

run_and_capture_strict "$SCRIPT_DIR/sample_ast_opt_dead_if.mog" \
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

run_and_capture_strict "$SCRIPT_DIR/sample_ast_opt_dead_while.mog" \
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

run_and_capture_strict "$SCRIPT_DIR/sample_ast_opt_dead_for_decl_init.mog" \
    dead_for_decl_output dead_for_decl_status dead_for_decl_disassembly dead_for_decl_disassembly_status

if [[ $dead_for_decl_status -ne 0 || $dead_for_decl_disassembly_status -ne 0 ]]; then
    echo "[FAIL] dead-for-decl sample failed"
    echo "$dead_for_decl_output"
    echo "$dead_for_decl_disassembly"
    exit 1
fi

if [[ "$dead_for_decl_output" != $'init\ndone' ]]; then
    echo "[FAIL] dead-for-decl sample produced unexpected output"
    echo "$dead_for_decl_output"
    exit 1
fi

if grep -Eq "JUMP_IF_FALSE_POP|LOOP" <<< "$dead_for_decl_disassembly"; then
    echo "[FAIL] dead-for-decl sample still emits loop control flow"
    echo "$dead_for_decl_disassembly"
    exit 1
fi

run_and_capture_strict "$SCRIPT_DIR/sample_ast_opt_dead_for_expr_init.mog" \
    dead_for_expr_output dead_for_expr_status dead_for_expr_disassembly dead_for_expr_disassembly_status

if [[ $dead_for_expr_status -ne 0 || $dead_for_expr_disassembly_status -ne 0 ]]; then
    echo "[FAIL] dead-for-expr sample failed"
    echo "$dead_for_expr_output"
    echo "$dead_for_expr_disassembly"
    exit 1
fi

if [[ "$dead_for_expr_output" != $'init\n1' ]]; then
    echo "[FAIL] dead-for-expr sample produced unexpected output"
    echo "$dead_for_expr_output"
    exit 1
fi

if grep -Eq "JUMP_IF_FALSE_POP|LOOP" <<< "$dead_for_expr_disassembly"; then
    echo "[FAIL] dead-for-expr sample still emits loop control flow"
    echo "$dead_for_expr_disassembly"
    exit 1
fi

run_and_capture_strict "$SCRIPT_DIR/sample_ast_opt_typed_numeric_fold.mog" \
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

run_and_capture_strict "$SCRIPT_DIR/sample_ast_opt_identity_fold.mog" \
    identity_output identity_status identity_disassembly identity_disassembly_status

if [[ $identity_status -ne 0 || $identity_disassembly_status -ne 0 ]]; then
    echo "[FAIL] identity-fold sample failed"
    echo "$identity_output"
    echo "$identity_disassembly"
    exit 1
fi

if [[ "$identity_output" != $'call\n5\ncall\n5\ncall\n5\ncall\n5\ncall\n5\ncall\n5\ncall\n5\ncall\n5\ncall\n5\ncall\n5' ]]; then
    echo "[FAIL] identity-fold sample produced unexpected output"
    echo "$identity_output"
    exit 1
fi

if ! grep -q "CALL" <<< "$identity_disassembly" || grep -Eq "IADD|ISUB|IMULT|IDIV|BITWISE_OR|BITWISE_XOR|SHIFT_LEFT|SHIFT_RIGHT" <<< "$identity_disassembly"; then
    echo "[FAIL] identity-fold sample still emits arithmetic, bitwise, or shift opcodes"
    echo "$identity_disassembly"
    exit 1
fi

run_and_capture_strict "$SCRIPT_DIR/sample_ast_opt_comparison_branch_fold.mog" \
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

run_and_capture_strict "$SCRIPT_DIR/sample_ast_opt_short_circuit_identity.mog" \
    short_circuit_output short_circuit_status short_circuit_disassembly short_circuit_disassembly_status

if [[ $short_circuit_status -ne 0 || $short_circuit_disassembly_status -ne 0 ]]; then
    echo "[FAIL] short-circuit identity sample failed"
    echo "$short_circuit_output"
    echo "$short_circuit_disassembly"
    exit 1
fi

if [[ "$short_circuit_output" != $'rhs-true\ntrue\nrhs-false\nfalse' ]]; then
    echo "[FAIL] short-circuit identity sample produced unexpected output"
    echo "$short_circuit_output"
    exit 1
fi

if grep -Eq "JUMP_IF_FALSE|JUMP_IF_FALSE_POP" <<< "$short_circuit_disassembly" || grep -q "JUMP " <<< "$short_circuit_disassembly"; then
    echo "[FAIL] short-circuit identity sample still emits short-circuit jumps"
    echo "$short_circuit_disassembly"
    exit 1
fi

run_and_capture_strict "$SCRIPT_DIR/sample_ast_opt_logical_pure_drop.mog" \
    logical_drop_output logical_drop_status logical_drop_disassembly logical_drop_disassembly_status

if [[ $logical_drop_status -ne 0 || $logical_drop_disassembly_status -ne 0 ]]; then
    echo "[FAIL] logical-pure-drop sample failed"
    echo "$logical_drop_output"
    echo "$logical_drop_disassembly"
    exit 1
fi

if [[ "$logical_drop_output" != $'false\ntrue' ]]; then
    echo "[FAIL] logical-pure-drop sample produced unexpected output"
    echo "$logical_drop_output"
    exit 1
fi

if grep -Eq "JUMP_IF_FALSE|JUMP_IF_FALSE_POP" <<< "$logical_drop_disassembly" || grep -q "JUMP " <<< "$logical_drop_disassembly"; then
    echo "[FAIL] logical-pure-drop sample still emits short-circuit jumps"
    echo "$logical_drop_disassembly"
    exit 1
fi

run_and_capture_strict "$SCRIPT_DIR/sample_ast_opt_no_side_effect_fold.mog" \
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

run_and_capture_strict "$SCRIPT_DIR/sample_ast_opt_identity_promotion_guard.mog" \
    promotion_guard_output promotion_guard_status promotion_guard_disassembly promotion_guard_disassembly_status

if [[ $promotion_guard_status -ne 0 || $promotion_guard_disassembly_status -ne 0 ]]; then
    echo "[FAIL] identity-promotion-guard sample failed"
    echo "$promotion_guard_output"
    echo "$promotion_guard_disassembly"
    exit 1
fi

if [[ "$promotion_guard_output" != $'call\n5' ]]; then
    echo "[FAIL] identity-promotion-guard sample produced unexpected output"
    echo "$promotion_guard_output"
    exit 1
fi

if ! grep -q "UADD" <<< "$promotion_guard_disassembly"; then
    echo "[FAIL] identity-promotion-guard sample was folded across numeric promotion"
    echo "$promotion_guard_disassembly"
    exit 1
fi

run_and_capture_strict "$SCRIPT_DIR/sample_ast_opt_no_fold_numeric_guard.mog" \
    numeric_guard_output numeric_guard_status numeric_guard_disassembly numeric_guard_disassembly_status

if [[ $numeric_guard_status -ne 0 || $numeric_guard_disassembly_status -ne 0 ]]; then
    echo "[FAIL] numeric-guard sample failed"
    echo "$numeric_guard_output"
    echo "$numeric_guard_disassembly"
    exit 1
fi

if [[ "$numeric_guard_output" != "ok" ]]; then
    echo "[FAIL] numeric-guard sample produced unexpected output"
    echo "$numeric_guard_output"
    exit 1
fi

if ! grep -q "IADD" <<< "$numeric_guard_disassembly" || ! grep -q "IDIV" <<< "$numeric_guard_disassembly"; then
    echo "[FAIL] numeric-guard sample folded overflow or divide-by-zero cases"
    echo "$numeric_guard_disassembly"
    exit 1
fi

run_and_capture_strict "$SCRIPT_DIR/sample_ast_opt_bitwise_shift_fold.mog" \
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

run_and_capture_strict "$SCRIPT_DIR/sample_ast_opt_unreachable_return.mog" \
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

run_and_capture_non_strict "$SCRIPT_DIR/sample_ast_opt_non_strict_dead_for.mog" \
    non_strict_for_output non_strict_for_status non_strict_for_disassembly non_strict_for_disassembly_status

if [[ $non_strict_for_status -ne 0 || $non_strict_for_disassembly_status -ne 0 ]]; then
    echo "[FAIL] non-strict dead-for sample failed"
    echo "$non_strict_for_output"
    echo "$non_strict_for_disassembly"
    exit 1
fi

if [[ "$non_strict_for_output" != $'init\n1' ]]; then
    echo "[FAIL] non-strict dead-for sample produced unexpected output"
    echo "$non_strict_for_output"
    exit 1
fi

if grep -Eq "JUMP_IF_FALSE_POP|LOOP" <<< "$non_strict_for_disassembly"; then
    echo "[FAIL] non-strict dead-for sample still emits loop control flow"
    echo "$non_strict_for_disassembly"
    exit 1
fi

run_and_capture_non_strict "$SCRIPT_DIR/sample_ast_opt_non_strict_identity_fold.mog" \
    non_strict_identity_output non_strict_identity_status non_strict_identity_disassembly non_strict_identity_disassembly_status

if [[ $non_strict_identity_status -ne 0 || $non_strict_identity_disassembly_status -ne 0 ]]; then
    echo "[FAIL] non-strict identity-fold sample failed"
    echo "$non_strict_identity_output"
    echo "$non_strict_identity_disassembly"
    exit 1
fi

if [[ "$non_strict_identity_output" != $'call\n5\ncall\n5' ]]; then
    echo "[FAIL] non-strict identity-fold sample produced unexpected output"
    echo "$non_strict_identity_output"
    exit 1
fi

if ! grep -q "CALL" <<< "$non_strict_identity_disassembly" || grep -Eq "IADD|BITWISE_OR" <<< "$non_strict_identity_disassembly"; then
    echo "[FAIL] non-strict identity-fold sample still emits arithmetic or bitwise opcodes"
    echo "$non_strict_identity_disassembly"
    exit 1
fi

run_and_capture_non_strict "$SCRIPT_DIR/sample_ast_opt_non_strict_short_circuit.mog" \
    non_strict_short_output non_strict_short_status non_strict_short_disassembly non_strict_short_disassembly_status

if [[ $non_strict_short_status -ne 0 || $non_strict_short_disassembly_status -ne 0 ]]; then
    echo "[FAIL] non-strict short-circuit sample failed"
    echo "$non_strict_short_output"
    echo "$non_strict_short_disassembly"
    exit 1
fi

if [[ "$non_strict_short_output" != $'rhs-true\ntrue\nrhs-false\nfalse' ]]; then
    echo "[FAIL] non-strict short-circuit sample produced unexpected output"
    echo "$non_strict_short_output"
    exit 1
fi

if grep -Eq "JUMP_IF_FALSE|JUMP_IF_FALSE_POP" <<< "$non_strict_short_disassembly" || grep -q "JUMP " <<< "$non_strict_short_disassembly"; then
    echo "[FAIL] non-strict short-circuit sample still emits short-circuit jumps"
    echo "$non_strict_short_disassembly"
    exit 1
fi

echo "[PASS] AST optimizer handles conservative helper-driven control-flow and local expression rewrites in strict and non-strict modes."
