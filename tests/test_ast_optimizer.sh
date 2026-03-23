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
    local -a cmd=("$INTERPRETER")
    local -a disassemble_cmd=("$INTERPRETER")

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

run_and_capture "$SCRIPT_DIR/sample_ast_opt_dead_for_decl_init.mog" \
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

run_and_capture "$SCRIPT_DIR/sample_ast_opt_dead_for_expr_init.mog" \
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

run_and_capture "$SCRIPT_DIR/sample_ast_opt_identity_fold.mog" \
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

run_and_capture "$SCRIPT_DIR/sample_ast_opt_short_circuit_identity.mog" \
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

run_and_capture "$SCRIPT_DIR/sample_ast_opt_logical_rhs_identity.mog" \
    logical_rhs_output logical_rhs_status logical_rhs_disassembly logical_rhs_disassembly_status

if [[ $logical_rhs_status -ne 0 || $logical_rhs_disassembly_status -ne 0 ]]; then
    echo "[FAIL] logical-rhs-identity sample failed"
    echo "$logical_rhs_output"
    echo "$logical_rhs_disassembly"
    exit 1
fi

if [[ "$logical_rhs_output" != $'lhs-true\ntrue\nlhs-false\nfalse' ]]; then
    echo "[FAIL] logical-rhs-identity sample produced unexpected output"
    echo "$logical_rhs_output"
    exit 1
fi

if grep -Eq "JUMP_IF_FALSE|JUMP_IF_FALSE_POP" <<< "$logical_rhs_disassembly" || grep -q "JUMP " <<< "$logical_rhs_disassembly"; then
    echo "[FAIL] logical-rhs-identity sample still emits short-circuit jumps"
    echo "$logical_rhs_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_logical_pure_drop.mog" \
    logical_drop_output logical_drop_status logical_drop_disassembly logical_drop_disassembly_status

if [[ $logical_drop_status -ne 0 || $logical_drop_disassembly_status -ne 0 ]]; then
    echo "[FAIL] logical-pure-drop sample failed"
    echo "$logical_drop_output"
    echo "$logical_drop_disassembly"
    exit 1
fi

if [[ "$logical_drop_output" != $'false\ntrue\nfalse\ntrue' ]]; then
    echo "[FAIL] logical-pure-drop sample produced unexpected output"
    echo "$logical_drop_output"
    exit 1
fi

if grep -Eq "JUMP_IF_FALSE|JUMP_IF_FALSE_POP" <<< "$logical_drop_disassembly" || grep -q "JUMP " <<< "$logical_drop_disassembly"; then
    echo "[FAIL] logical-pure-drop sample still emits short-circuit jumps"
    echo "$logical_drop_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_logical_impure_guard.mog" \
    logical_impure_output logical_impure_status logical_impure_disassembly logical_impure_disassembly_status

if [[ $logical_impure_status -ne 0 || $logical_impure_disassembly_status -ne 0 ]]; then
    echo "[FAIL] logical-impure-guard sample failed"
    echo "$logical_impure_output"
    echo "$logical_impure_disassembly"
    exit 1
fi

if [[ "$logical_impure_output" != $'call-true\nfalse\ncall-false\ntrue' ]]; then
    echo "[FAIL] logical-impure-guard sample produced unexpected output"
    echo "$logical_impure_output"
    exit 1
fi

if ! grep -q "CALL" <<< "$logical_impure_disassembly" || ! grep -Eq "JUMP_IF_FALSE|JUMP_IF_FALSE_POP|JUMP " <<< "$logical_impure_disassembly"; then
    echo "[FAIL] logical-impure-guard sample dropped required short-circuit control flow"
    echo "$logical_impure_disassembly"
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

run_and_capture "$SCRIPT_DIR/sample_ast_opt_identity_promotion_guard.mog" \
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

run_and_capture "$SCRIPT_DIR/sample_ast_opt_no_fold_numeric_guard.mog" \
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

run_and_capture "$SCRIPT_DIR/sample_ast_opt_bool_equality_identity.mog" \
    bool_identity_output bool_identity_status bool_identity_disassembly bool_identity_disassembly_status

if [[ $bool_identity_status -ne 0 || $bool_identity_disassembly_status -ne 0 ]]; then
    echo "[FAIL] bool-equality-identity sample failed"
    echo "$bool_identity_output"
    echo "$bool_identity_disassembly"
    exit 1
fi

if [[ "$bool_identity_output" != $'lhs-true\ntrue\nlhs-true\ntrue\nlhs-false\ntrue\nlhs-false\ntrue' ]]; then
    echo "[FAIL] bool-equality-identity sample produced unexpected output"
    echo "$bool_identity_output"
    exit 1
fi

if grep -Eq "EQUAL|NOT_EQUAL" <<< "$bool_identity_disassembly"; then
    echo "[FAIL] bool-equality-identity sample still emits equality opcodes"
    echo "$bool_identity_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_double_not.mog" \
    double_not_output double_not_status double_not_disassembly double_not_disassembly_status

if [[ $double_not_status -ne 0 || $double_not_disassembly_status -ne 0 ]]; then
    echo "[FAIL] double-not sample failed"
    echo "$double_not_output"
    echo "$double_not_disassembly"
    exit 1
fi

if [[ "$double_not_output" != $'call\ntrue\nfalse' ]]; then
    echo "[FAIL] double-not sample produced unexpected output"
    echo "$double_not_output"
    exit 1
fi

if grep -q "NOT" <<< "$double_not_disassembly"; then
    echo "[FAIL] double-not sample still emits unary not opcodes"
    echo "$double_not_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_int_double_tilde.mog" \
    double_tilde_output double_tilde_status double_tilde_disassembly double_tilde_disassembly_status

if [[ $double_tilde_status -ne 0 || $double_tilde_disassembly_status -ne 0 ]]; then
    echo "[FAIL] integer-double-tilde sample failed"
    echo "$double_tilde_output"
    echo "$double_tilde_disassembly"
    exit 1
fi

if [[ "$double_tilde_output" != $'call\n7\n0' ]]; then
    echo "[FAIL] integer-double-tilde sample produced unexpected output"
    echo "$double_tilde_output"
    exit 1
fi

if grep -q "BITWISE_NOT" <<< "$double_tilde_disassembly"; then
    echo "[FAIL] integer-double-tilde sample still emits bitwise not opcodes"
    echo "$double_tilde_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_mul_zero.mog" \
    mul_zero_output mul_zero_status mul_zero_disassembly mul_zero_disassembly_status

if [[ $mul_zero_status -ne 0 || $mul_zero_disassembly_status -ne 0 ]]; then
    echo "[FAIL] multiply-zero sample failed"
    echo "$mul_zero_output"
    echo "$mul_zero_disassembly"
    exit 1
fi

if [[ "$mul_zero_output" != $'0\n0' ]]; then
    echo "[FAIL] multiply-zero sample produced unexpected output"
    echo "$mul_zero_output"
    exit 1
fi

if grep -Eq "IMULT|UMULT|MULT" <<< "$mul_zero_disassembly"; then
    echo "[FAIL] multiply-zero sample still emits multiply opcodes"
    echo "$mul_zero_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_mul_zero_impure_guard.mog" \
    mul_zero_impure_output mul_zero_impure_status mul_zero_impure_disassembly mul_zero_impure_disassembly_status

if [[ $mul_zero_impure_status -ne 0 || $mul_zero_impure_disassembly_status -ne 0 ]]; then
    echo "[FAIL] multiply-zero impure-guard sample failed"
    echo "$mul_zero_impure_output"
    echo "$mul_zero_impure_disassembly"
    exit 1
fi

if [[ "$mul_zero_impure_output" != $'call\n0\ncall\n0' ]]; then
    echo "[FAIL] multiply-zero impure-guard sample produced unexpected output"
    echo "$mul_zero_impure_output"
    exit 1
fi

if ! grep -q "CALL" <<< "$mul_zero_impure_disassembly" || ! grep -Eq "IMULT|UMULT|MULT" <<< "$mul_zero_impure_disassembly"; then
    echo "[FAIL] multiply-zero impure-guard sample dropped required call or multiply opcodes"
    echo "$mul_zero_impure_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_mul_zero_type_guard.mog" \
    mul_zero_type_guard_output mul_zero_type_guard_status mul_zero_type_guard_disassembly mul_zero_type_guard_disassembly_status

if [[ $mul_zero_type_guard_status -ne 0 || $mul_zero_type_guard_disassembly_status -ne 0 ]]; then
    echo "[FAIL] multiply-zero type-guard sample failed"
    echo "$mul_zero_type_guard_output"
    echo "$mul_zero_type_guard_disassembly"
    exit 1
fi

if [[ "$mul_zero_type_guard_output" != "0" ]]; then
    echo "[FAIL] multiply-zero type-guard sample produced unexpected output"
    echo "$mul_zero_type_guard_output"
    exit 1
fi

if ! grep -Eq "IMULT|UMULT|MULT" <<< "$mul_zero_type_guard_disassembly"; then
    echo "[FAIL] multiply-zero type-guard sample was rewritten across type promotion"
    echo "$mul_zero_type_guard_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_bitwise_and_zero.mog" \
    and_zero_output and_zero_status and_zero_disassembly and_zero_disassembly_status

if [[ $and_zero_status -ne 0 || $and_zero_disassembly_status -ne 0 ]]; then
    echo "[FAIL] bitwise-and-zero sample failed"
    echo "$and_zero_output"
    echo "$and_zero_disassembly"
    exit 1
fi

if [[ "$and_zero_output" != $'0\n0' ]]; then
    echo "[FAIL] bitwise-and-zero sample produced unexpected output"
    echo "$and_zero_output"
    exit 1
fi

if grep -q "BITWISE_AND" <<< "$and_zero_disassembly"; then
    echo "[FAIL] bitwise-and-zero sample still emits bitwise and opcodes"
    echo "$and_zero_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_bitwise_and_zero_impure_guard.mog" \
    and_zero_impure_output and_zero_impure_status and_zero_impure_disassembly and_zero_impure_disassembly_status

if [[ $and_zero_impure_status -ne 0 || $and_zero_impure_disassembly_status -ne 0 ]]; then
    echo "[FAIL] bitwise-and-zero impure-guard sample failed"
    echo "$and_zero_impure_output"
    echo "$and_zero_impure_disassembly"
    exit 1
fi

if [[ "$and_zero_impure_output" != $'call\n0\ncall\n0' ]]; then
    echo "[FAIL] bitwise-and-zero impure-guard sample produced unexpected output"
    echo "$and_zero_impure_output"
    exit 1
fi

if ! grep -q "CALL" <<< "$and_zero_impure_disassembly" || ! grep -q "BITWISE_AND" <<< "$and_zero_impure_disassembly"; then
    echo "[FAIL] bitwise-and-zero impure-guard sample dropped required call or bitwise and opcodes"
    echo "$and_zero_impure_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_bool_equality_type_guard.mog" \
    bool_type_guard_output bool_type_guard_status bool_type_guard_disassembly bool_type_guard_disassembly_status

if [[ $bool_type_guard_status -ne 0 || $bool_type_guard_disassembly_status -ne 0 ]]; then
    echo "[FAIL] bool-equality type-guard sample failed"
    echo "$bool_type_guard_output"
    echo "$bool_type_guard_disassembly"
    exit 1
fi

if [[ "$bool_type_guard_output" != $'true\nfalse' ]]; then
    echo "[FAIL] bool-equality type-guard sample produced unexpected output"
    echo "$bool_type_guard_output"
    exit 1
fi

if ! grep -Eq "EQUAL|NOT_EQUAL" <<< "$bool_type_guard_disassembly"; then
    echo "[FAIL] bool-equality type-guard sample was rewritten despite non-bool operands"
    echo "$bool_type_guard_disassembly"
    exit 1
fi

run_and_capture "$SCRIPT_DIR/sample_ast_opt_bitwise_and_zero_type_guard.mog" \
    and_zero_type_guard_output and_zero_type_guard_status and_zero_type_guard_disassembly and_zero_type_guard_disassembly_status

if [[ $and_zero_type_guard_status -ne 0 || $and_zero_type_guard_disassembly_status -ne 0 ]]; then
    echo "[FAIL] bitwise-and-zero type-guard sample failed"
    echo "$and_zero_type_guard_output"
    echo "$and_zero_type_guard_disassembly"
    exit 1
fi

if [[ "$and_zero_type_guard_output" != "0" ]]; then
    echo "[FAIL] bitwise-and-zero type-guard sample produced unexpected output"
    echo "$and_zero_type_guard_output"
    exit 1
fi

if ! grep -q "BITWISE_AND" <<< "$and_zero_type_guard_disassembly"; then
    echo "[FAIL] bitwise-and-zero type-guard sample was rewritten across type promotion"
    echo "$and_zero_type_guard_disassembly"
    exit 1
fi

echo "[PASS] AST optimizer handles strict typed control-flow and expression rewrites."
