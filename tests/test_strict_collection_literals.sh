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

run_expect_output() {
    local file="$1"
    local expected="$2"

    set +e
    local output
    output="$($INTERPRETER --strict "$file" 2>&1)"
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

failed=0

run_expect_output \
    "$SCRIPT_DIR/sample_collection_print.mog" \
    $'[[1, 2], [3, 4]]\n{"a": 1, "b": 2, "c": 9}\n[a, b, c]\n[1, 2, 9]\nSet(3, 1, 2)\n[5]' || failed=1

run_expect_output "$SCRIPT_DIR/sample_flappy_core.mog" "ok" || failed=1

if [[ $failed -ne 0 ]]; then
    exit 1
fi

echo "[PASS] strict collection literal tests"
exit 0
