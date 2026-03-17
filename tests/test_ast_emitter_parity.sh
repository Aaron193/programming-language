#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PARITY_BIN="$PROJECT_ROOT/build/ast_emitter_parity"

if [[ ! -x "$PARITY_BIN" ]]; then
    echo "AST emitter parity binary not found at $PARITY_BIN"
    echo "Build first with: $PROJECT_ROOT/build.sh"
    exit 1
fi

"$PARITY_BIN"
