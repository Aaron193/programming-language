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
REMOTE_DIR=""
WORKSPACE_DIR=""
trap 'rm -rf "${TEMP_DIR:-}" "${REMOTE_DIR:-}" "${WORKSPACE_DIR:-}"' EXIT

ln -s "$PROJECT_ROOT/packages" "$TEMP_DIR/packages"
export MOG_CACHE_DIR="$TEMP_DIR/cache-root"

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

if ! find "$MOG_CACHE_DIR" -name ".metadata.toml" -print -quit | grep -q .; then
    echo "[FAIL] install did not write durable cache metadata"
    find "$MOG_CACHE_DIR" -maxdepth 6 -print
    exit 1
fi

cp "$TEMP_DIR/mog.lock" "$TEMP_DIR/mog.lock.before"
(cd "$TEMP_DIR" && "$MOG" install --locked >/dev/null)
if ! cmp -s "$TEMP_DIR/mog.lock.before" "$TEMP_DIR/mog.lock"; then
    echo "[FAIL] install --locked should not rewrite mog.lock"
    exit 1
fi

rm -f "$TEMP_DIR/.mog/install/registry.toml"
LOCKED_RUN_OUTPUT="$("$MOG" run --locked "$TEMP_DIR/app.mog")"
if [[ "$LOCKED_RUN_OUTPUT" != *"42"* || "$LOCKED_RUN_OUTPUT" != *"hello from source package"* ]]; then
    echo "[FAIL] run --locked did not reinstall packages or produce expected output"
    echo "$LOCKED_RUN_OUTPUT"
    exit 1
fi

if [[ ! -f "$TEMP_DIR/.mog/install/registry.toml" ]]; then
    echo "[FAIL] run --locked did not recreate .mog/install/registry.toml"
    exit 1
fi

if ! (cd "$TEMP_DIR" && "$MOG" install --offline >/dev/null); then
    echo "[FAIL] install --offline should succeed for local path dependencies"
    exit 1
fi

python3 - "$TEMP_DIR/mog.toml" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
updated = text.replace(
    'hello = { path = "../../home/dev/Desktop/projects/programming-language/packages/examples/hello", package = "examples:hello", version = "0.1.0" }',
    'hello = { path = "../../home/dev/Desktop/projects/programming-language/packages/examples/hello", package = "examples:hello", version = "9.9.9" }',
)
if updated == text:
    raise SystemExit("failed to update hello dependency version")
path.write_text(updated, encoding="utf-8")
PY

if (cd "$TEMP_DIR" && "$MOG" install --locked >/tmp/mog_locked_failure.txt 2>&1); then
    echo "[FAIL] install --locked should reject manifest drift"
    cat /tmp/mog_locked_failure.txt
    exit 1
fi

if ! grep -Eq "out of date|requires version" /tmp/mog_locked_failure.txt; then
    echo "[FAIL] install --locked should explain manifest drift"
    cat /tmp/mog_locked_failure.txt
    exit 1
fi

REMOTE_DIR="$(mktemp -d)"

cat > "$REMOTE_DIR/mog.toml" <<'EOF_REMOTE'
kind = "project"
name = "remote-test"
version = "0.1.0"
description = "remote source test"

[dependencies]
remote = { package = "acme:http", version = "1.0.0" }
EOF_REMOTE

if (cd "$REMOTE_DIR" && "$MOG" install --offline >/tmp/mog_offline_failure.txt 2>&1); then
    echo "[FAIL] install --offline should reject non-local dependencies"
    cat /tmp/mog_offline_failure.txt
    exit 1
fi

if ! grep -Eq "offline|Phase 2|published package resolution" /tmp/mog_offline_failure.txt; then
    echo "[FAIL] install --offline should explain why non-local dependencies are rejected"
    cat /tmp/mog_offline_failure.txt
    exit 1
fi

cat > "$REMOTE_DIR/mog.toml" <<'EOF_GIT'
kind = "project"
name = "git-test"
version = "0.1.0"
description = "git source test"

[dependencies]
remote = { git = "https://example.com/acme/http.git", package = "acme:http", version = "1.0.0" }
EOF_GIT

if (cd "$REMOTE_DIR" && "$MOG" install >/tmp/mog_git_failure.txt 2>&1); then
    echo "[FAIL] install should reject git dependencies until Phase 2"
    cat /tmp/mog_git_failure.txt
    exit 1
fi

if ! grep -Fq "not implemented until Phase 2" /tmp/mog_git_failure.txt; then
    echo "[FAIL] install should explain git dependency support is deferred"
    cat /tmp/mog_git_failure.txt
    exit 1
fi

WORKSPACE_DIR="$(mktemp -d)"
mkdir -p "$WORKSPACE_DIR/apps/demo" "$WORKSPACE_DIR/members/hello-local/src"

cat > "$WORKSPACE_DIR/mog.toml" <<'EOF_WORKSPACE'
kind = "project"
name = "workspace-root"
version = "0.1.0"
description = "workspace root"

[workspace]
members = ["members/hello-local"]

[dependencies]
hello = { workspace = true, package = "examples:hello-local", version = "0.1.0" }
EOF_WORKSPACE

cat > "$WORKSPACE_DIR/members/hello-local/mog.toml" <<'EOF_MEMBER'
kind = "source"
import_name = "hello"
namespace = "examples"
name = "hello-local"
version = "0.1.0"
author = "Mog runtime"
description = "Workspace hello package."
entry = "src/main.mog"
dependencies = []
EOF_MEMBER

cat > "$WORKSPACE_DIR/members/hello-local/src/main.mog" <<'EOF_MEMBER_SRC'
const MESSAGE str = "hello from workspace package"

fn Greet() str {
    return MESSAGE
}
EOF_MEMBER_SRC

cat > "$WORKSPACE_DIR/apps/demo/app.mog" <<'EOF_WORKSPACE_APP'
const hello = @import("hello")
print(hello.Greet())
EOF_WORKSPACE_APP

pushd "$WORKSPACE_DIR/apps/demo" >/dev/null
"$MOG" install >/dev/null
WORKSPACE_RUN_OUTPUT="$("$MOG" run app.mog)"
popd >/dev/null

if [[ "$WORKSPACE_RUN_OUTPUT" != *"hello from workspace package"* ]]; then
    echo "[FAIL] workspace-root install/run did not resolve workspace dependency"
    echo "$WORKSPACE_RUN_OUTPUT"
    exit 1
fi

if [[ ! -f "$WORKSPACE_DIR/.mog/install/registry.toml" || ! -f "$WORKSPACE_DIR/mog.lock" ]]; then
    echo "[FAIL] workspace-root commands should write install metadata at the workspace root"
    find "$WORKSPACE_DIR" -maxdepth 4 -print
    exit 1
fi

if ! grep -Fq 'source_type = "workspace"' "$WORKSPACE_DIR/mog.lock"; then
    echo "[FAIL] workspace dependencies should be recorded as workspace sources"
    cat "$WORKSPACE_DIR/mog.lock"
    exit 1
fi

echo "[PASS] package manager workflow"
