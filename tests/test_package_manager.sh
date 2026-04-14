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

write_registry_index() {
    local registry_dir="$1"
    local mode="${2:-correct}"
    python3 - "$registry_dir" "$mode" <<'PY'
from pathlib import Path
import sys

registry_dir = Path(sys.argv[1])
mode = sys.argv[2]

def digest_directory(root: Path) -> str:
    files = sorted(path for path in root.rglob("*") if path.is_file())
    seed = []
    for path in files:
        seed.append(path.relative_to(root).as_posix())
        seed.append("\n")
        seed.append(path.read_text(encoding="utf-8"))
        seed.append("\n")
    text = "".join(seed).encode("utf-8")
    value = 1469598103934665603
    for byte in text:
        value ^= byte
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"

util_digest = digest_directory(registry_dir / "packages" / "util")
http_digest = digest_directory(registry_dir / "packages" / "http")
native_digest = digest_directory(registry_dir / "packages" / "native-demo")
if mode == "digest-mismatch":
    http_digest = "0000000000000000"

index = f'''schema_version = "registry.v1"

[[package]]
package_id = "acme:util"
version = "1.0.0"
artifact_path = "packages/util"
artifact_digest = "{util_digest}"
dependencies = []

[[package]]
package_id = "acme:http"
version = "1.0.0"
artifact_path = "packages/http"
artifact_digest = "{http_digest}"
dependencies = ["acme:util@1.0.0"]

[[package]]
package_id = "acme:native-demo"
version = "1.0.0"
artifact_path = "packages/native-demo"
artifact_digest = "{native_digest}"
dependencies = []
'''

(registry_dir / "index.toml").write_text(index, encoding="utf-8")
PY
}

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

if ! grep -Fq 'schema_version = "lock.v2"' "$TEMP_DIR/mog.lock"; then
    echo "[FAIL] lockfile did not write the new lock schema"
    cat "$TEMP_DIR/mog.lock"
    exit 1
fi

if ! grep -Fq 'schema_version = "install.v2"' "$TEMP_DIR/.mog/install/registry.toml"; then
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
REGISTRY_DIR="$REMOTE_DIR/registry"
mkdir -p "$REGISTRY_DIR/packages/util/src" \
         "$REGISTRY_DIR/packages/http/src" \
         "$REGISTRY_DIR/packages/native-demo"

cat > "$REGISTRY_DIR/packages/util/mog.toml" <<'EOF_REGISTRY_UTIL_MANIFEST'
kind = "source"
import_name = "util"
namespace = "acme"
name = "util"
version = "1.0.0"
author = "Registry test"
description = "Published utility package."
entry = "src/main.mog"
dependencies = []
EOF_REGISTRY_UTIL_MANIFEST

cat > "$REGISTRY_DIR/packages/util/src/main.mog" <<'EOF_REGISTRY_UTIL_SRC'
const MESSAGE str = "utility from registry"

fn Name() str {
    return MESSAGE
}
EOF_REGISTRY_UTIL_SRC

cat > "$REGISTRY_DIR/packages/http/mog.toml" <<'EOF_REGISTRY_HTTP_MANIFEST'
kind = "source"
import_name = "http"
namespace = "acme"
name = "http"
version = "1.0.0"
author = "Registry test"
description = "Published http package."
entry = "src/main.mog"
dependencies = ["acme:util"]
EOF_REGISTRY_HTTP_MANIFEST

cat > "$REGISTRY_DIR/packages/http/src/main.mog" <<'EOF_REGISTRY_HTTP_SRC'
const util = @import("util")

fn Fetch() str {
    return util.Name()
}
EOF_REGISTRY_HTTP_SRC

cat > "$REGISTRY_DIR/packages/native-demo/mog.toml" <<'EOF_REGISTRY_NATIVE_MANIFEST'
kind = "native"
import_name = "native-demo"
namespace = "acme"
name = "native-demo"
version = "1.0.0"
abi_version = 1
author = "Registry test"
description = "Published native package placeholder."
dependencies = []
EOF_REGISTRY_NATIVE_MANIFEST

write_registry_index "$REGISTRY_DIR"

cat > "$REMOTE_DIR/mog.toml" <<EOF_REMOTE
kind = "project"
name = "remote-test"
version = "0.1.0"
description = "remote source test"

[registries.default]
index = "$REGISTRY_DIR"

[dependencies]
http = { package = "acme:http", version = "1.0.0" }
EOF_REMOTE

if ! (cd "$REMOTE_DIR" && "$MOG" install >/dev/null); then
    echo "[FAIL] install should resolve published source packages from the registry"
    exit 1
fi

if ! grep -Fq 'source_type = "registry"' "$REMOTE_DIR/mog.lock" || \
   ! grep -Fq 'registry = "default"' "$REMOTE_DIR/mog.lock" || \
   ! grep -Fq 'artifact_digest = "' "$REMOTE_DIR/mog.lock"; then
    echo "[FAIL] remote lockfile should record registry source metadata"
    cat "$REMOTE_DIR/mog.lock"
    exit 1
fi

cat > "$REMOTE_DIR/app.mog" <<'EOF_REMOTE_APP'
const http = @import("http")
print(http.Fetch())
EOF_REMOTE_APP

REMOTE_RUN_OUTPUT="$("$MOG" run "$REMOTE_DIR/app.mog")"
if [[ "$REMOTE_RUN_OUTPUT" != *"utility from registry"* ]]; then
    echo "[FAIL] run should execute registry-installed source packages with transitive dependencies"
    echo "$REMOTE_RUN_OUTPUT"
    exit 1
fi

rm -f "$REMOTE_DIR/.mog/install/registry.toml"
if ! (cd "$REMOTE_DIR" && "$MOG" install --offline >/dev/null); then
    echo "[FAIL] install --offline should succeed after a registry package has been cached"
    exit 1
fi

EMPTY_CACHE_DIR="$(mktemp -d)"
if (cd "$REMOTE_DIR" && MOG_CACHE_DIR="$EMPTY_CACHE_DIR" "$MOG" install --offline >/tmp/mog_registry_offline_failure.txt 2>&1); then
    echo "[FAIL] install --offline should reject uncached registry dependencies"
    cat /tmp/mog_registry_offline_failure.txt
    exit 1
fi
rm -rf "$EMPTY_CACHE_DIR"

if ! grep -Eq "offline|cached locally" /tmp/mog_registry_offline_failure.txt; then
    echo "[FAIL] install --offline should explain the missing registry cache"
    cat /tmp/mog_registry_offline_failure.txt
    exit 1
fi

cat > "$REGISTRY_DIR/packages/util/src/main.mog" <<'EOF_REGISTRY_UTIL_SRC_UPDATED'
const MESSAGE str = "utility from registry v2"

fn Name() str {
    return MESSAGE
}
EOF_REGISTRY_UTIL_SRC_UPDATED
write_registry_index "$REGISTRY_DIR"

if (cd "$REMOTE_DIR" && "$MOG" install --locked >/tmp/mog_registry_locked_failure.txt 2>&1); then
    echo "[FAIL] install --locked should reject registry artifact drift"
    cat /tmp/mog_registry_locked_failure.txt
    exit 1
fi

if ! grep -Eq "out of date|refresh" /tmp/mog_registry_locked_failure.txt; then
    echo "[FAIL] install --locked should explain registry artifact drift"
    cat /tmp/mog_registry_locked_failure.txt
    exit 1
fi

cat > "$REMOTE_DIR/mog.toml" <<EOF_RANGE
kind = "project"
name = "remote-test"
version = "0.1.0"
description = "remote source test"

[registries.default]
index = "$REGISTRY_DIR"

[dependencies]
http = { package = "acme:http", version = "^1.0.0" }
EOF_RANGE

if (cd "$REMOTE_DIR" && "$MOG" install >/tmp/mog_registry_range_failure.txt 2>&1); then
    echo "[FAIL] install should reject published dependency version ranges in the MVP"
    cat /tmp/mog_registry_range_failure.txt
    exit 1
fi

if ! grep -Eq "exact x.y.z|exact" /tmp/mog_registry_range_failure.txt; then
    echo "[FAIL] install should explain exact published versions are required"
    cat /tmp/mog_registry_range_failure.txt
    exit 1
fi

cat > "$REMOTE_DIR/mog.toml" <<EOF_UNKNOWN_REGISTRY
kind = "project"
name = "remote-test"
version = "0.1.0"
description = "remote source test"

[registries.default]
index = "$REGISTRY_DIR"

[dependencies]
http = { package = "acme:http", version = "1.0.0", registry = "missing" }
EOF_UNKNOWN_REGISTRY

if (cd "$REMOTE_DIR" && "$MOG" install >/tmp/mog_registry_missing_failure.txt 2>&1); then
    echo "[FAIL] install should reject unknown registry aliases"
    cat /tmp/mog_registry_missing_failure.txt
    exit 1
fi

if ! grep -Fq "unknown registry" /tmp/mog_registry_missing_failure.txt; then
    echo "[FAIL] install should explain missing registry aliases"
    cat /tmp/mog_registry_missing_failure.txt
    exit 1
fi

cat > "$REMOTE_DIR/mog.toml" <<EOF_NATIVE_REMOTE
kind = "project"
name = "remote-test"
version = "0.1.0"
description = "remote source test"

[registries.default]
index = "$REGISTRY_DIR"

[dependencies]
native = { package = "acme:native-demo", version = "1.0.0" }
EOF_NATIVE_REMOTE

if (cd "$REMOTE_DIR" && "$MOG" install >/tmp/mog_registry_native_failure.txt 2>&1); then
    echo "[FAIL] install should reject published native packages in Phase 2A"
    cat /tmp/mog_registry_native_failure.txt
    exit 1
fi

if ! grep -Eq "Phase 3|Published native packages" /tmp/mog_registry_native_failure.txt; then
    echo "[FAIL] install should explain published native packages are deferred"
    cat /tmp/mog_registry_native_failure.txt
    exit 1
fi

write_registry_index "$REGISTRY_DIR" digest-mismatch
cat > "$REMOTE_DIR/mog.toml" <<EOF_BAD_DIGEST
kind = "project"
name = "remote-test"
version = "0.1.0"
description = "remote source test"

[registries.default]
index = "$REGISTRY_DIR"

[dependencies]
http = { package = "acme:http", version = "1.0.0" }
EOF_BAD_DIGEST

if (cd "$REMOTE_DIR" && "$MOG" install >/tmp/mog_registry_digest_failure.txt 2>&1); then
    echo "[FAIL] install should reject registry artifact digest mismatches"
    cat /tmp/mog_registry_digest_failure.txt
    exit 1
fi

if ! grep -Fq "digest mismatch" /tmp/mog_registry_digest_failure.txt; then
    echo "[FAIL] install should explain registry artifact digest mismatches"
    cat /tmp/mog_registry_digest_failure.txt
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
    echo "[FAIL] install should reject git dependencies until a later Phase 2 slice"
    cat /tmp/mog_git_failure.txt
    exit 1
fi

if ! grep -Fq "not implemented until Phase 2" /tmp/mog_git_failure.txt; then
    echo "[FAIL] install should explain git dependency support is still deferred"
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
