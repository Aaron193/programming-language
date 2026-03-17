#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REGRESSION_BIN="$PROJECT_ROOT/build/ast_emitter_regression"

if [[ ! -x "$REGRESSION_BIN" ]]; then
    echo "AST emitter regression binary not found at $REGRESSION_BIN"
    echo "Build first with: $PROJECT_ROOT/build.sh"
    exit 1
fi

"$REGRESSION_BIN"
