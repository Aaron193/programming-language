#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MOG="${MOG:-$PROJECT_ROOT/build/interpreter}"
PACKAGE_DIR="$PROJECT_ROOT/packages/mog/window"
WINDOW_SO="$PROJECT_ROOT/build/packages/mog/window/package.so"
WINDOW_DYLIB="$PROJECT_ROOT/build/packages/mog/window/package.dylib"

usage() {
    cat <<'EOF'
Usage:
  scripts/publish_official_window.sh --registry-path <dir>
  scripts/publish_official_window.sh --registry <alias> [--workspace <dir>]

Options:
  --registry-path <dir>  Publish using a temporary workspace rooted at <dir>.
  --registry <alias>     Publish through an existing workspace registry alias.
  --workspace <dir>      Workspace directory to use with --registry. Defaults to $PWD.
EOF
}

registry_path=""
registry_alias=""
workspace_dir="$PWD"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --registry-path)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --registry-path." >&2
                usage >&2
                exit 1
            fi
            registry_path="$2"
            shift 2
            ;;
        --registry-path=*)
            registry_path="${1#*=}"
            shift
            ;;
        --registry)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --registry." >&2
                usage >&2
                exit 1
            fi
            registry_alias="$2"
            shift 2
            ;;
        --registry=*)
            registry_alias="${1#*=}"
            shift
            ;;
        --workspace)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --workspace." >&2
                usage >&2
                exit 1
            fi
            workspace_dir="$2"
            shift 2
            ;;
        --workspace=*)
            workspace_dir="${1#*=}"
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -n "$registry_path" && -n "$registry_alias" ]]; then
    echo "Choose either --registry-path or --registry, not both." >&2
    exit 1
fi

if [[ -z "$registry_path" && -z "$registry_alias" ]]; then
    echo "Publishing requires --registry-path <dir> or --registry <alias>." >&2
    exit 1
fi

if [[ ! -x "$MOG" ]]; then
    echo "Interpreter not found at $MOG" >&2
    echo "Build first with: $PROJECT_ROOT/build.sh" >&2
    exit 1
fi

if [[ ! -d "$PACKAGE_DIR" ]]; then
    echo "Official package directory not found at $PACKAGE_DIR" >&2
    exit 1
fi

window_library=""
if [[ -f "$WINDOW_SO" ]]; then
    window_library="$WINDOW_SO"
elif [[ -f "$WINDOW_DYLIB" ]]; then
    window_library="$WINDOW_DYLIB"
fi

if [[ -z "$window_library" ]]; then
    echo "Built mog:window library not found under $PROJECT_ROOT/build/packages/mog/window." >&2
    echo "Build the repo with SDL2 available before publishing the official window package." >&2
    exit 1
fi

publish_workspace="$workspace_dir"
temp_workspace=""
cleanup() {
    if [[ -n "$temp_workspace" ]]; then
        rm -rf "$temp_workspace"
    fi
}
trap cleanup EXIT

publish_args=()

if [[ -n "$registry_path" ]]; then
    mkdir -p "$registry_path"
    temp_workspace="$(mktemp -d)"
    publish_workspace="$temp_workspace"
    cat > "$publish_workspace/mog.toml" <<EOF
kind = "project"
name = "window-publish-root"
version = "0.1.0"
description = "Temporary workspace for publishing mog:window."

[registries.default]
index = "$registry_path"
EOF
else
    if [[ ! -f "$publish_workspace/mog.toml" ]]; then
        echo "Workspace manifest not found at $publish_workspace/mog.toml" >&2
        exit 1
    fi
    publish_args=(--registry "$registry_alias")
fi

(
    cd "$publish_workspace"
    "$MOG" publish "${publish_args[@]}" "$PACKAGE_DIR"
)
