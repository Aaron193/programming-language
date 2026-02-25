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

FILES=(
    "$SCRIPT_DIR/sample_native_error_len.expr"
    "$SCRIPT_DIR/sample_native_error_sqrt.expr"
    "$SCRIPT_DIR/sample_native_error_num.expr"
)

passed=0
failed=0

for file in "${FILES[@]}"; do
    echo "========================================"
    echo "Running (expect error): $file"
    echo "----------------------------------------"

    if "$INTERPRETER" "$file"; then
        echo "[FAIL] Expected runtime error but execution succeeded: $file"
        failed=$((failed + 1))
    else
        echo "[PASS] Runtime error occurred as expected: $file"
        passed=$((passed + 1))
    fi

    echo
 done

echo "========================================"
echo "Summary: PASS=$passed FAIL=$failed TOTAL=$((passed + failed))"

if [[ $failed -ne 0 ]]; then
    exit 1
fi

exit 0
