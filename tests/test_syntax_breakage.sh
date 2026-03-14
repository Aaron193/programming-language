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

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/legacy_class_keyword.mog" \
    "Keyword 'class' was removed; use 'type Name struct { ... }'." || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/legacy_import_statement.mog" \
    "Statement 'import ... from' was removed; use '@import(...)' in a binding." || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/legacy_function_literal_keyword.mog" \
    "Keyword 'function' was removed; use 'fn'." || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/legacy_typed_class_member.mog" \
    "Legacy typed class members were removed; use 'name Type' fields and 'fn Name(...) Ret'." || failed=1

run_expect_compile_error \
    "$SCRIPT_DIR/types/errors/legacy_method_missing_fn.mog" \
    "Methods must start with 'fn'." || failed=1

if [[ $failed -ne 0 ]]; then
    exit 1
fi

echo "[PASS] syntax breakage tests"
exit 0
