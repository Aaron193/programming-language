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

PASS_FILE="$SCRIPT_DIR/sample_inherit_field.expr"

if ! "$INTERPRETER" "$PASS_FILE" >/tmp/property_access_pass.out 2>/tmp/property_access_pass.err; then
    echo "[FAIL] Expected inherited field access to succeed: $PASS_FILE"
    cat /tmp/property_access_pass.err
    exit 1
fi

FAIL_FILE="$(mktemp)"
trap 'rm -f "$FAIL_FILE"' EXIT

cat <<'EOF' >"$FAIL_FILE"
class Box {
  i32 value;
}

auto box = Box();
print(box.value);
EOF

set +e
FAIL_OUTPUT="$($INTERPRETER "$FAIL_FILE" 2>&1)"
FAIL_STATUS=$?
set -e

if [[ $FAIL_STATUS -eq 0 ]]; then
    echo "[FAIL] Expected runtime failure for uninitialized field access."
    exit 1
fi

if ! grep -Fq "Undefined property 'value'." <<< "$FAIL_OUTPUT"; then
    echo "[FAIL] Expected undefined property message for uninitialized field access."
    echo "$FAIL_OUTPUT"
    exit 1
fi

echo "[PASS] property access runtime checks"
exit 0
