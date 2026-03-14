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
    "$SCRIPT_DIR/types/errors/semicolon_statement.mog" \
    "Semicolons are only allowed inside 'for (...)' clauses." || failed=1

if [[ $failed -ne 0 ]]; then
    exit 1
fi

echo "[PASS] syntax validation tests"
exit 0
