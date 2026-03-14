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

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

INVALID_SOURCE_FILE="$TMP_DIR/invalid_source.expr"
printf 'print(1);\n' > "$INVALID_SOURCE_FILE"

set +e
OUTPUT="$($INTERPRETER "$INVALID_SOURCE_FILE" 2>&1)"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "[FAIL] expected non-.mog source file to be rejected"
    exit 1
fi

if ! grep -q "Source files must use the .mog extension" <<< "$OUTPUT"; then
    echo "[FAIL] missing .mog extension error"
    echo "$OUTPUT"
    exit 1
fi

echo "[PASS] CLI rejects non-.mog source files."
exit 0
