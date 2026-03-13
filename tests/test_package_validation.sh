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

run_expect_success() {
    local label="$1"
    shift

    if "$@"; then
        echo "[PASS] $label"
        return 0
    fi

    echo "[FAIL] $label"
    return 1
}

run_expect_failure() {
    local label="$1"
    local expected="$2"
    shift 2

    set +e
    local output
    output="$("$@" 2>&1)"
    local status=$?
    set -e

    if [[ $status -eq 0 ]]; then
        echo "[FAIL] $label"
        echo "$output"
        return 1
    fi

    if ! grep -Fq "$expected" <<< "$output"; then
        echo "[FAIL] $label"
        echo "Expected: $expected"
        echo "Actual output:"
        echo "$output"
        return 1
    fi

    echo "[PASS] $label"
    return 0
}

failed=0

run_expect_success \
    "validate examples:math" \
    "$INTERPRETER" --validate-package "$PROJECT_ROOT/packages/examples/math" ||
    failed=1

run_expect_success \
    "validate examples:counter" \
    "$INTERPRETER" --validate-package="$PROJECT_ROOT/packages/examples/counter" ||
    failed=1

run_expect_failure \
    "reject registration mismatch" \
    "Manifest declares 'examples:mismatch' but library registers 'examples:declared_math'." \
    "$INTERPRETER" --validate-package "$PROJECT_ROOT/packages/examples/mismatch" ||
    failed=1

TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT
mkdir -p "$TEMP_DIR/mog/fake"
cat <<'EOF_MANIFEST' > "$TEMP_DIR/mog/fake/package.toml"
namespace = "mog"
name = "fake"
version = "0.1.0"
abi_version = 3
description = "fake"
dependencies = []
EOF_MANIFEST

run_expect_failure \
    "reject reserved mog namespace outside repo package roots" \
    "Namespace 'mog' is reserved for runtime-maintained packages." \
    "$INTERPRETER" --validate-package "$TEMP_DIR/mog/fake" ||
    failed=1

if [[ $failed -ne 0 ]]; then
    exit 1
fi

echo "[PASS] package validation tests"
exit 0
