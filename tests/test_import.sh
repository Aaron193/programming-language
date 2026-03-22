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
    "$SCRIPT_DIR/sample_import_basic.mog"
    "$SCRIPT_DIR/sample_import_named.mog"
    "$SCRIPT_DIR/sample_import_alias.mog"
    "$SCRIPT_DIR/sample_import_class.mog"
    "$SCRIPT_DIR/sample_import_nested.mog"
    "$SCRIPT_DIR/sample_import_native_package.mog"
    "$SCRIPT_DIR/sample_import_native_named.mog"
    "$SCRIPT_DIR/sample_import_native_handle.mog"
    "$SCRIPT_DIR/sample_import_native_handle_typed.mog"
    "$SCRIPT_DIR/sample_import_native_handle_frontend_typed.mog"
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

HANDLE_FILE="$SCRIPT_DIR/sample_import_native_handle.mog"
echo "========================================"
echo "Running handle finalizer check: $HANDLE_FILE"
echo "----------------------------------------"

set +e
HANDLE_OUTPUT="$($INTERPRETER "$HANDLE_FILE" 2>&1)"
HANDLE_STATUS=$?
set -e

if [[ $HANDLE_STATUS -ne 0 ]]; then
    echo "[FAIL] handle sample exited with failure"
    echo "$HANDLE_OUTPUT"
    FAIL=$((FAIL + 1))
else
    RELEASE_COUNT=$(grep -c "counter_handle_released" <<< "$HANDLE_OUTPUT")
    if [[ $RELEASE_COUNT -eq 2 ]]; then
        echo "[PASS] native handle finalizers executed twice"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] expected native handle finalizer twice, got $RELEASE_COUNT"
        echo "$HANDLE_OUTPUT"
        FAIL=$((FAIL + 1))
    fi
fi

CACHE_FILE="$SCRIPT_DIR/sample_import_cache.mog"
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

CYCLE_FILE="$SCRIPT_DIR/sample_import_cycle.mog"
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

SCOPED_EXPORT_FILE="$SCRIPT_DIR/sample_export_scoped_error.mog"
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

MISSING_NATIVE_FILE="$SCRIPT_DIR/sample_import_native_missing.mog"
echo "========================================"
echo "Running (expect compile error): $MISSING_NATIVE_FILE"
echo "----------------------------------------"

if "$INTERPRETER" "$MISSING_NATIVE_FILE"; then
    echo "[FAIL] expected compile error for missing native package"
    FAIL=$((FAIL + 1))
else
    echo "[PASS] missing native package failed as expected"
    PASS=$((PASS + 1))
fi

echo

TYPE_MISMATCH_FILE="$SCRIPT_DIR/sample_import_native_type_mismatch.mog"
echo "========================================"
echo "Running (expect compile error): $TYPE_MISMATCH_FILE"
echo "----------------------------------------"

if "$INTERPRETER" "$TYPE_MISMATCH_FILE"; then
    echo "[FAIL] expected compile error for native package type mismatch"
    FAIL=$((FAIL + 1))
else
    echo "[PASS] native package type mismatch failed as expected"
    PASS=$((PASS + 1))
fi

echo

INVALID_ID_FILE="$SCRIPT_DIR/sample_import_native_invalid_id.mog"
echo "========================================"
echo "Running (expect compile error): $INVALID_ID_FILE"
echo "----------------------------------------"

if "$INTERPRETER" "$INVALID_ID_FILE"; then
    echo "[FAIL] expected compile error for invalid native package ID"
    FAIL=$((FAIL + 1))
else
    echo "[PASS] invalid native package ID failed as expected"
    PASS=$((PASS + 1))
fi

echo

BARE_ID_FILE="$SCRIPT_DIR/sample_import_native_bare_invalid.mog"
echo "========================================"
echo "Running (expect compile error): $BARE_ID_FILE"
echo "----------------------------------------"

if "$INTERPRETER" "$BARE_ID_FILE"; then
    echo "[FAIL] expected compile error for bare native package ID"
    FAIL=$((FAIL + 1))
else
    echo "[PASS] bare native package ID failed as expected"
    PASS=$((PASS + 1))
fi

echo

MISMATCH_FILE="$SCRIPT_DIR/sample_import_native_metadata_mismatch.mog"
echo "========================================"
echo "Running (expect compile error): $MISMATCH_FILE"
echo "----------------------------------------"

if "$INTERPRETER" "$MISMATCH_FILE"; then
    echo "[FAIL] expected compile error for native package metadata mismatch"
    FAIL=$((FAIL + 1))
else
    echo "[PASS] native package metadata mismatch failed as expected"
    PASS=$((PASS + 1))
fi

echo

FOREIGN_HANDLE_FILE="$SCRIPT_DIR/fail_import_native_handle_foreign.mog"
echo "========================================"
echo "Running (expect runtime error): $FOREIGN_HANDLE_FILE"
echo "----------------------------------------"

if "$INTERPRETER" "$FOREIGN_HANDLE_FILE"; then
    echo "[FAIL] expected runtime error for foreign native handle"
    FAIL=$((FAIL + 1))
else
    echo "[PASS] foreign native handle failed as expected"
    PASS=$((PASS + 1))
fi

echo

INVALID_SOURCE_EXTENSION_FILE="$SCRIPT_DIR/fail_import_expr_extension.mog"
echo "========================================"
echo "Running (expect compile error): $INVALID_SOURCE_EXTENSION_FILE"
echo "----------------------------------------"

set +e
INVALID_SOURCE_EXTENSION_OUTPUT="$($INTERPRETER "$INVALID_SOURCE_EXTENSION_FILE" 2>&1)"
INVALID_SOURCE_EXTENSION_STATUS=$?
set -e

if [[ $INVALID_SOURCE_EXTENSION_STATUS -eq 0 ]]; then
    echo "[FAIL] expected compile error for .expr source-module import"
    FAIL=$((FAIL + 1))
elif grep -q "Source module imports must use the .mog extension" <<< "$INVALID_SOURCE_EXTENSION_OUTPUT"; then
    echo "[PASS] .expr source-module import rejected with extension error"
    PASS=$((PASS + 1))
else
    echo "[FAIL] missing .mog extension error for .expr source-module import"
    echo "$INVALID_SOURCE_EXTENSION_OUTPUT"
    FAIL=$((FAIL + 1))
fi

echo

WINDOW_PACKAGE_SO="$PROJECT_ROOT/build/packages/mog/window/package.so"
WINDOW_PACKAGE_DYLIB="$PROJECT_ROOT/build/packages/mog/window/package.dylib"
if [[ -f "$WINDOW_PACKAGE_SO" || -f "$WINDOW_PACKAGE_DYLIB" ]]; then
    WINDOW_FILE="$SCRIPT_DIR/sample_mog_window.mog"
    echo "========================================"
    echo "Running SDL window smoke test: $WINDOW_FILE"
    echo "----------------------------------------"

    if SDL_VIDEODRIVER=dummy "$INTERPRETER" "$WINDOW_FILE"; then
        echo "[PASS] SDL window smoke test"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] SDL window smoke test"
        FAIL=$((FAIL + 1))
    fi
    echo

    WINDOW_RENDER_FILE="$SCRIPT_DIR/sample_mog_window_render.mog"
    echo "========================================"
    echo "Running SDL render smoke test: $WINDOW_RENDER_FILE"
    echo "----------------------------------------"

    if SDL_VIDEODRIVER=dummy "$INTERPRETER" "$WINDOW_RENDER_FILE"; then
        echo "[PASS] SDL render smoke test"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] SDL render smoke test"
        FAIL=$((FAIL + 1))
    fi
    echo
else
    echo "[SKIP] SDL2 not available; mog:window package not built"
    echo
fi

echo "========================================"
echo "Summary: PASS=$PASS FAIL=$FAIL TOTAL=$((PASS + FAIL))"

if [[ $FAIL -ne 0 ]]; then
    exit 1
fi

exit 0
