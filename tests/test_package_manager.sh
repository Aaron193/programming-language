#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MOG="$PROJECT_ROOT/build/interpreter"

if [[ ! -x "$MOG" ]]; then
    echo "Interpreter not found at $MOG"
    echo "Build first with: $PROJECT_ROOT/build.sh"
    exit 1
fi

TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT

ln -s "$PROJECT_ROOT/packages" "$TEMP_DIR/packages"

pushd "$TEMP_DIR" >/dev/null

"$MOG" init pkg-manager-test

if [[ ! -f "$TEMP_DIR/mog.toml" ]]; then
    echo "[FAIL] init did not create mog.toml"
    exit 1
fi

"$MOG" add math

if ! grep -Fq 'math = { path = "' "$TEMP_DIR/mog.toml" || \
   ! grep -Fq 'package = "examples:math"' "$TEMP_DIR/mog.toml"; then
    echo "[FAIL] add did not write the math dependency"
    cat "$TEMP_DIR/mog.toml"
    exit 1
fi

if [[ ! -f "$TEMP_DIR/.mog/install/registry.toml" ]]; then
    echo "[FAIL] add/install did not create .mog/install/registry.toml"
    exit 1
fi

cat > "$TEMP_DIR/app.mog" <<'EOF_APP'
const math = @import("math")
print(math.MEANING_OF_LIFE)
EOF_APP

rm -f "$TEMP_DIR/.mog/install/registry.toml"

RUN_OUTPUT="$("$MOG" run "$TEMP_DIR/app.mog")"
if [[ "$RUN_OUTPUT" != *"42"* ]]; then
    echo "[FAIL] run did not reinstall packages or produce expected output"
    echo "$RUN_OUTPUT"
    exit 1
fi

if [[ ! -f "$TEMP_DIR/.mog/install/registry.toml" ]]; then
    echo "[FAIL] run did not recreate .mog/install/registry.toml"
    exit 1
fi

"$MOG" --validate-package "$PROJECT_ROOT/packages/examples/math" >/dev/null

popd >/dev/null

echo "[PASS] package manager workflow"
