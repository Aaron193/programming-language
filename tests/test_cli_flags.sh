#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INTERPRETER="$PROJECT_ROOT/build/interpreter"
TARGET="$SCRIPT_DIR/sample_var.mog"

if [[ ! -x "$INTERPRETER" ]]; then
    echo "Interpreter not found at $INTERPRETER"
    echo "Build first with: $PROJECT_ROOT/build.sh"
    exit 1
fi

set +e
TRACE_OUTPUT="$($INTERPRETER --trace "$TARGET" 2>&1)"
TRACE_STATUS=$?
SHOW_RETURN_OUTPUT="$($INTERPRETER --show-return "$TARGET" 2>&1)"
SHOW_RETURN_STATUS=$?
DISASSEMBLE_OUTPUT="$($INTERPRETER --disassemble "$TARGET" 2>&1)"
DISASSEMBLE_STATUS=$?
FRONTEND_TIMINGS_OUTPUT="$($INTERPRETER --frontend-timings "$TARGET" 2>&1)"
FRONTEND_TIMINGS_STATUS=$?
FRONTEND_TIMINGS_JSON_OUTPUT="$($INTERPRETER --frontend-timings-json "$TARGET" 2>&1)"
FRONTEND_TIMINGS_JSON_STATUS=$?
STRICT_FLAG_OUTPUT="$($INTERPRETER --strict "$TARGET" 2>&1)"
STRICT_FLAG_STATUS=$?
set -e

if [[ $TRACE_STATUS -ne 0 ]]; then
    echo "[FAIL] --trace execution failed"
    echo "$TRACE_OUTPUT"
    exit 1
fi
if ! grep -q "LINE:" <<< "$TRACE_OUTPUT"; then
    echo "[FAIL] --trace output missing instruction trace"
    echo "$TRACE_OUTPUT"
    exit 1
fi

if [[ $SHOW_RETURN_STATUS -ne 0 ]]; then
    echo "[FAIL] --show-return execution failed"
    echo "$SHOW_RETURN_OUTPUT"
    exit 1
fi
if ! grep -q "Return constant:" <<< "$SHOW_RETURN_OUTPUT"; then
    echo "[FAIL] --show-return output missing return value"
    echo "$SHOW_RETURN_OUTPUT"
    exit 1
fi

if [[ $DISASSEMBLE_STATUS -ne 0 ]]; then
    echo "[FAIL] --disassemble execution failed"
    echo "$DISASSEMBLE_OUTPUT"
    exit 1
fi
if ! grep -q "== disassembly ==" <<< "$DISASSEMBLE_OUTPUT"; then
    echo "[FAIL] --disassemble output missing header"
    echo "$DISASSEMBLE_OUTPUT"
    exit 1
fi

if [[ $FRONTEND_TIMINGS_STATUS -ne 0 ]]; then
    echo "[FAIL] --frontend-timings execution failed"
    echo "$FRONTEND_TIMINGS_OUTPUT"
    exit 1
fi
if ! grep -q "\\[frontend\\] parse=.* bind=.* check=.* hir=.* hir-optimize=.* total=" <<< "$FRONTEND_TIMINGS_OUTPUT"; then
    echo "[FAIL] --frontend-timings output missing timing summary"
    echo "$FRONTEND_TIMINGS_OUTPUT"
    exit 1
fi

if [[ $FRONTEND_TIMINGS_JSON_STATUS -ne 0 ]]; then
    echo "[FAIL] --frontend-timings-json execution failed"
    echo "$FRONTEND_TIMINGS_JSON_OUTPUT"
    exit 1
fi
if ! grep -q '"parseMicros":[0-9][0-9]*' <<< "$FRONTEND_TIMINGS_JSON_OUTPUT" || \
   ! grep -q '"moduleCacheHits":[0-9][0-9]*' <<< "$FRONTEND_TIMINGS_JSON_OUTPUT" || \
   ! grep -q '"totalMicros":[0-9][0-9]*' <<< "$FRONTEND_TIMINGS_JSON_OUTPUT"; then
    echo "[FAIL] --frontend-timings-json output missing JSON timing payload"
    echo "$FRONTEND_TIMINGS_JSON_OUTPUT"
    exit 1
fi

if ! grep -q "42" <<< "$TRACE_OUTPUT" || ! grep -q "42" <<< "$SHOW_RETURN_OUTPUT" || ! grep -q "42" <<< "$DISASSEMBLE_OUTPUT" || ! grep -q "42" <<< "$FRONTEND_TIMINGS_OUTPUT" || ! grep -q "42" <<< "$FRONTEND_TIMINGS_JSON_OUTPUT"; then
    echo "[FAIL] expected program output missing for one or more flag runs"
    exit 1
fi

if [[ $STRICT_FLAG_STATUS -eq 0 ]]; then
    echo "[FAIL] expected --strict to be rejected"
    exit 1
fi
if ! grep -q "Unknown option: --strict" <<< "$STRICT_FLAG_OUTPUT"; then
    echo "[FAIL] expected unknown-option output for --strict"
    echo "$STRICT_FLAG_OUTPUT"
    exit 1
fi

echo "[PASS] CLI flags work and --strict is rejected."
exit 0
