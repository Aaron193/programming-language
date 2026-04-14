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
"$MOG" add hello

if ! grep -Fq 'math = { path = "' "$TEMP_DIR/mog.toml" || \
   ! grep -Fq 'package = "examples:math"' "$TEMP_DIR/mog.toml" || \
   ! grep -Fq 'hello = { path = "' "$TEMP_DIR/mog.toml" || \
   ! grep -Fq 'package = "examples:hello"' "$TEMP_DIR/mog.toml"; then
    echo "[FAIL] add did not write the dependency metadata"
    cat "$TEMP_DIR/mog.toml"
    exit 1
fi

if [[ ! -f "$TEMP_DIR/.mog/install/registry.toml" ]]; then
    echo "[FAIL] add/install did not create .mog/install/registry.toml"
    exit 1
fi

cat > "$TEMP_DIR/app.mog" <<'EOF_APP'
const math = @import("math")
const hello = @import("hello")
print(math.MEANING_OF_LIFE)
print(hello.Greet())
EOF_APP

rm -f "$TEMP_DIR/.mog/install/registry.toml"

RUN_OUTPUT="$("$MOG" run "$TEMP_DIR/app.mog")"
if [[ "$RUN_OUTPUT" != *"42"* || "$RUN_OUTPUT" != *"hello from source package"* ]]; then
    echo "[FAIL] run did not reinstall packages or produce expected output"
    echo "$RUN_OUTPUT"
    exit 1
fi

if [[ ! -f "$TEMP_DIR/.mog/install/registry.toml" ]]; then
    echo "[FAIL] run did not recreate .mog/install/registry.toml"
    exit 1
fi

if ! grep -Fq 'schema_version = "lock.v1"' "$TEMP_DIR/mog.lock"; then
    echo "[FAIL] lockfile did not write the new lock schema"
    cat "$TEMP_DIR/mog.lock"
    exit 1
fi

if ! grep -Fq 'schema_version = "install.v1"' "$TEMP_DIR/.mog/install/registry.toml"; then
    echo "[FAIL] install registry did not write the new install schema"
    cat "$TEMP_DIR/.mog/install/registry.toml"
    exit 1
fi

if grep -Fq 'package_dir = "/home/dev/Desktop/projects/programming-language/packages' \
    "$TEMP_DIR/.mog/install/registry.toml"; then
    echo "[FAIL] install registry should point at project-local installs"
    cat "$TEMP_DIR/.mog/install/registry.toml"
    exit 1
fi

if ! grep -Fq 'package_dir = ".mog/install/packages/examples/math"' \
    "$TEMP_DIR/.mog/install/registry.toml" || \
   ! grep -Fq 'package_dir = ".mog/install/packages/examples/hello"' \
    "$TEMP_DIR/.mog/install/registry.toml"; then
    echo "[FAIL] install registry missing project-local package roots"
    cat "$TEMP_DIR/.mog/install/registry.toml"
    exit 1
fi

if ! grep -Fq 'source_path = "' "$TEMP_DIR/mog.lock" || \
   ! grep -Fq 'package_id = "examples:math"' "$TEMP_DIR/mog.lock" || \
   ! grep -Fq 'package_id = "examples:hello"' "$TEMP_DIR/mog.lock"; then
    echo "[FAIL] lockfile missing source metadata"
    cat "$TEMP_DIR/mog.lock"
    exit 1
fi

if [[ ! -f "$TEMP_DIR/.mog/install/packages/examples/math/package.so" ]]; then
    echo "[FAIL] native package was not materialized into the project install store"
    find "$TEMP_DIR/.mog" -maxdepth 5 -type f | sort
    exit 1
fi

if [[ ! -f "$TEMP_DIR/.mog/install/packages/examples/hello/src/main.mog" ]]; then
    echo "[FAIL] source package was not materialized into the project install store"
    find "$TEMP_DIR/.mog" -maxdepth 5 -type f | sort
    exit 1
fi

"$MOG" --validate-package "$PROJECT_ROOT/packages/examples/math" >/dev/null

popd >/dev/null

echo "[PASS] package manager workflow"
