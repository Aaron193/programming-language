#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SMOKE_BIN="$PROJECT_ROOT/build/ast_parser_smoke"

if [[ ! -x "$SMOKE_BIN" ]]; then
    echo "AST parser smoke binary not found at $SMOKE_BIN"
    echo "Build first with: $PROJECT_ROOT/build.sh"
    exit 1
fi

"$SMOKE_BIN" \
    "$SCRIPT_DIR/types/parser/sample_typed_parser.mog" \
    "$SCRIPT_DIR/types/parser/sample_ast_full_parser.mog"
