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

echo "[PASS] AST optimizer folds pure constants and preserves side-effectful expressions."
