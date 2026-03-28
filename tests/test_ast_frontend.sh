#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REGRESSION_BIN="$PROJECT_ROOT/build/ast_frontend_regression"

if [[ ! -x "$REGRESSION_BIN" ]]; then
    echo "AST frontend regression binary not found at $REGRESSION_BIN"
    echo "Build first with: $PROJECT_ROOT/build.sh"
    exit 1
fi

bash "$SCRIPT_DIR/test_ast_parser.sh" &&
bash "$SCRIPT_DIR/test_typechecker_errors.sh" &&
bash "$SCRIPT_DIR/test_strict_collection_literals.sh" &&
bash "$SCRIPT_DIR/test_ast_compiler_findings.sh" &&
bash "$SCRIPT_DIR/test_ast_optimizer.sh" &&
bash "$SCRIPT_DIR/test_ast_emitter_regression.sh" &&
bash "$SCRIPT_DIR/test_newline_syntax.sh" &&
bash "$SCRIPT_DIR/test_vscode_grammar.sh" &&
bash "$SCRIPT_DIR/test_compile_recovery.sh" &&
bash "$SCRIPT_DIR/test_lsp_navigation.sh" &&
bash "$SCRIPT_DIR/test_frontend_benchmark.sh" &&
"$REGRESSION_BIN"
