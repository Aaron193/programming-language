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

PASS=0
FAIL=0

SUCCESS_FILES=(
    "$SCRIPT_DIR/sample_import_basic.expr"
    "$SCRIPT_DIR/sample_import_named.expr"
    "$SCRIPT_DIR/sample_import_alias.expr"
    "$SCRIPT_DIR/sample_import_class.expr"
    "$SCRIPT_DIR/sample_import_nested.expr"
)

for file in "${SUCCESS_FILES[@]}"; do
    echo "========================================"
    echo "Running: $file"
    echo "----------------------------------------"

    if "$INTERPRETER" "$file"; then
        echo "[PASS] $file"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $file"
        FAIL=$((FAIL + 1))
    fi
    echo
 done

CACHE_FILE="$SCRIPT_DIR/sample_import_cache.expr"
echo "========================================"
echo "Running cache check: $CACHE_FILE"
echo "----------------------------------------"

set +e
CACHE_OUTPUT="$($INTERPRETER "$CACHE_FILE" 2>&1)"
CACHE_STATUS=$?
set -e

if [[ $CACHE_STATUS -ne 0 ]]; then
    echo "[FAIL] cache sample exited with failure"
    echo "$CACHE_OUTPUT"
    FAIL=$((FAIL + 1))
else
    LOAD_COUNT=$(grep -c "cache_target_loaded" <<< "$CACHE_OUTPUT")
    if [[ $LOAD_COUNT -eq 1 ]]; then
        echo "[PASS] module cache executed side-effects once"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] expected cache_target_loaded once, got $LOAD_COUNT"
        echo "$CACHE_OUTPUT"
        FAIL=$((FAIL + 1))
    fi
fi

echo

CYCLE_FILE="$SCRIPT_DIR/sample_import_cycle.expr"
echo "========================================"
echo "Running (expect runtime error): $CYCLE_FILE"
echo "----------------------------------------"

if "$INTERPRETER" "$CYCLE_FILE"; then
    echo "[FAIL] expected runtime error for circular import"
    FAIL=$((FAIL + 1))
else
    echo "[PASS] circular import failed as expected"
    PASS=$((PASS + 1))
fi

echo

SCOPED_EXPORT_FILE="$SCRIPT_DIR/sample_export_scoped_error.expr"
echo "========================================"
echo "Running (expect compile error): $SCOPED_EXPORT_FILE"
echo "----------------------------------------"

if "$INTERPRETER" "$SCOPED_EXPORT_FILE"; then
    echo "[FAIL] expected compile error for scoped export"
    FAIL=$((FAIL + 1))
else
    echo "[PASS] scoped export failed as expected"
    PASS=$((PASS + 1))
fi

echo

echo "========================================"
echo "Summary: PASS=$PASS FAIL=$FAIL TOTAL=$((PASS + FAIL))"

if [[ $FAIL -ne 0 ]]; then
    exit 1
fi

exit 0
