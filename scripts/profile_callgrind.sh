#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INTERPRETER="${INTERPRETER:-$ROOT_DIR/build/interpreter}"
OUTPUT_FILE=""
ANNOTATE_ONLY=""
INCLUSIVE=0
TREE_MODE=0
PROGRAM=""

usage() {
  cat <<'EOF'
Usage:
  scripts/profile_callgrind.sh [options] <program.expr>
  scripts/profile_callgrind.sh --annotate-only <callgrind.out>

Options:
  --output FILE          Write Callgrind data to FILE
  --annotate-only FILE   Skip execution and annotate an existing Callgrind file
  --inclusive            Show inclusive costs in the annotation report
  --tree                 Show caller/callee relationships in the annotation report
  --help, -h             Show this help

Environment:
  INTERPRETER            Override the interpreter binary path
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --output)
      OUTPUT_FILE="$2"
      shift 2
      ;;
    --annotate-only)
      ANNOTATE_ONLY="$2"
      shift 2
      ;;
    --inclusive)
      INCLUSIVE=1
      shift
      ;;
    --tree)
      TREE_MODE=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
    *)
      if [[ -n "$PROGRAM" ]]; then
        echo "Expected a single program path." >&2
        usage >&2
        exit 1
      fi
      PROGRAM="$1"
      shift
      ;;
  esac
done

if ! command -v valgrind >/dev/null 2>&1; then
  echo "Missing dependency: valgrind" >&2
  exit 1
fi

if ! command -v callgrind_annotate >/dev/null 2>&1; then
  echo "Missing dependency: callgrind_annotate" >&2
  exit 1
fi

if [[ -n "$ANNOTATE_ONLY" && -n "$PROGRAM" ]]; then
  echo "Do not pass a program when using --annotate-only." >&2
  exit 1
fi

if [[ -n "$ANNOTATE_ONLY" && -n "$OUTPUT_FILE" ]]; then
  echo "--output cannot be combined with --annotate-only." >&2
  exit 1
fi

if [[ -n "$ANNOTATE_ONLY" ]]; then
  PROFILE_PATH="$ANNOTATE_ONLY"
else
  if [[ -z "$PROGRAM" ]]; then
    echo "Missing program path." >&2
    usage >&2
    exit 1
  fi

  if [[ ! -x "$INTERPRETER" ]]; then
    echo "Interpreter not found or not executable at: $INTERPRETER" >&2
    echo "Build it first, for example: ./build.sh --release --profiling" >&2
    exit 1
  fi

  if [[ ! -f "$PROGRAM" ]]; then
    echo "Program not found: $PROGRAM" >&2
    exit 1
  fi

  if [[ -z "$OUTPUT_FILE" ]]; then
    mkdir -p "$ROOT_DIR/build/callgrind"
    stem="$(basename "$PROGRAM")"
    stem="${stem%.*}"
    timestamp="$(date +%Y%m%d-%H%M%S)"
    OUTPUT_FILE="$ROOT_DIR/build/callgrind/${stem}-${timestamp}.out"
  else
    mkdir -p "$(dirname "$OUTPUT_FILE")"
  fi

  PROFILE_PATH="$OUTPUT_FILE"

  echo "Profiling: $PROGRAM"
  echo "Interpreter: $INTERPRETER"
  echo "Callgrind output: $PROFILE_PATH"
  echo

  valgrind --tool=callgrind --callgrind-out-file="$PROFILE_PATH" \
    "$INTERPRETER" "$PROGRAM"
fi

if [[ ! -f "$PROFILE_PATH" ]]; then
  echo "Callgrind output not found: $PROFILE_PATH" >&2
  exit 1
fi

annotate_args=("--auto=no" "--threshold=95")
if [[ $INCLUSIVE -eq 1 ]]; then
  annotate_args+=("--inclusive=yes")
fi
if [[ $TREE_MODE -eq 1 ]]; then
  annotate_args+=("--tree=both")
fi

echo
echo "Annotated hotspots:"
callgrind_annotate "${annotate_args[@]}" "$PROFILE_PATH"
echo
echo "Profile saved to: $PROFILE_PATH"
echo "For source-level annotation, run: callgrind_annotate --auto=yes $PROFILE_PATH"
