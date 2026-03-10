#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCH_DIR="$ROOT_DIR/benchmarks"

INTERPRETER_A="$ROOT_DIR/build/interpreter"
INTERPRETER_B=""
LABEL_A="A"
LABEL_B="B"
FILTER="$BENCH_DIR/bench_*.expr"
ITERATIONS=7
WARMUP=1
QUIET=0

usage() {
  cat <<'EOF'
Usage:
  benchmarks/compare_benchmarks.sh [options]

Options:
  --interpreter-a PATH   Interpreter for side A (default: build/interpreter)
  --interpreter-b PATH   Interpreter for side B (optional)
  --label-a NAME         Label for side A (default: A)
  --label-b NAME         Label for side B (default: B)
  --filter GLOB          Benchmark file glob (default: benchmarks/bench_*.expr)
  --iterations N         Timed runs per benchmark (default: 7)
  --warmup N             Warmup runs per benchmark (default: 1)
  --quiet                Suppress progress logs
  --help                 Show this help

Notes:
  - Benchmarks are expected to print elapsed time on the last line.
  - If --interpreter-b is omitted, only side A statistics are reported.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --interpreter-a)
      INTERPRETER_A="$2"
      shift 2
      ;;
    --interpreter-b)
      INTERPRETER_B="$2"
      shift 2
      ;;
    --label-a)
      LABEL_A="$2"
      shift 2
      ;;
    --label-b)
      LABEL_B="$2"
      shift 2
      ;;
    --filter)
      FILTER="$2"
      shift 2
      ;;
    --iterations)
      ITERATIONS="$2"
      shift 2
      ;;
    --warmup)
      WARMUP="$2"
      shift 2
      ;;
    --quiet)
      QUIET=1
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

if [[ ! -x "$INTERPRETER_A" ]]; then
  echo "Interpreter A not found/executable: $INTERPRETER_A" >&2
  exit 1
fi

if [[ -n "$INTERPRETER_B" && ! -x "$INTERPRETER_B" ]]; then
  echo "Interpreter B not found/executable: $INTERPRETER_B" >&2
  exit 1
fi

if ! [[ "$ITERATIONS" =~ ^[0-9]+$ ]] || [[ "$ITERATIONS" -lt 1 ]]; then
  echo "--iterations must be an integer >= 1" >&2
  exit 1
fi

if ! [[ "$WARMUP" =~ ^[0-9]+$ ]]; then
  echo "--warmup must be an integer >= 0" >&2
  exit 1
fi

mapfile -t BENCH_FILES < <(compgen -G "$FILTER" | sort)
if [[ ${#BENCH_FILES[@]} -eq 0 ]]; then
  echo "No benchmark files matched: $FILTER" >&2
  exit 1
fi

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

sanitize_name() {
  local value="$1"
  value="${value//\//_}"
  value="${value// /_}"
  value="${value//[^A-Za-z0-9_.-]/_}"
  printf '%s' "$value"
}

run_once() {
  local interpreter="$1"
  local bench="$2"

  local output
  if ! output="$("$interpreter" "$bench" 2>&1)"; then
    echo "Benchmark failed: $bench" >&2
    echo "$output" >&2
    return 1
  fi

  local elapsed
  elapsed="$(printf '%s\n' "$output" | awk 'NF { line = $0 } END { print line }')"
  elapsed="${elapsed//$'\r'/}"

  if ! [[ "$elapsed" =~ ^[-+]?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$ ]]; then
    echo "Failed to parse elapsed time from benchmark output: $bench" >&2
    echo "$output" >&2
    return 1
  fi

  printf '%s\n' "$elapsed"
}

collect_samples() {
  local label="$1"
  local interpreter="$2"
  local total="${#BENCH_FILES[@]}"
  local index=0

  for bench in "${BENCH_FILES[@]}"; do
    index=$((index + 1))
    local bench_name
    bench_name="$(basename "$bench")"
    local sample_file="$TMP_DIR/$(sanitize_name "$label")__$(sanitize_name "$bench_name").samples"
    local bench_start
    bench_start="$(date +%s)"

    if [[ "$QUIET" -eq 0 ]]; then
      echo "[bench][$label][$index/$total] $bench_name (warmup=$WARMUP, runs=$ITERATIONS)" >&2
    fi

    for ((i = 0; i < WARMUP; i++)); do
      run_once "$interpreter" "$bench" > /dev/null
    done

    : > "$sample_file"
    for ((i = 0; i < ITERATIONS; i++)); do
      run_once "$interpreter" "$bench" >> "$sample_file"
    done

    if [[ "$QUIET" -eq 0 ]]; then
      local bench_end
      bench_end="$(date +%s)"
      local bench_elapsed=$((bench_end - bench_start))
      echo "[bench][$label][$index/$total] done in ${bench_elapsed}s" >&2
    fi
  done
}

compute_stats() {
  local file="$1"
  local n
  n="$(wc -l < "$file" | tr -d ' ')"

  local mean stddev min max
  read -r mean stddev min max < <(
    awk '
      NR == 1 { min = $1; max = $1 }
      { sum += $1; sumsq += ($1 * $1); if ($1 < min) min = $1; if ($1 > max) max = $1 }
      END {
        if (NR == 0) exit 1;
        mean = sum / NR;
        variance = (sumsq / NR) - (mean * mean);
        if (variance < 0) variance = 0;
        stddev = sqrt(variance);
        printf "%.9f %.9f %.9f %.9f\n", mean, stddev, min, max;
      }
    ' "$file"
  )

  local median
  median="$(
    sort -n "$file" | awk '
      { values[NR] = $1 }
      END {
        if (NR == 0) exit 1;
        if (NR % 2 == 1) {
          printf "%.9f\n", values[(NR + 1) / 2];
        } else {
          printf "%.9f\n", (values[NR / 2] + values[(NR / 2) + 1]) / 2.0;
        }
      }
    '
  )"

  printf '%s %s %s %s %s %s\n' "$n" "$mean" "$median" "$stddev" "$min" "$max"
}

if [[ "$QUIET" -eq 0 ]]; then
  runs_per_side=$((WARMUP + ITERATIONS))
  side_count=1
  if [[ -n "$INTERPRETER_B" ]]; then
    side_count=2
  fi
  total_runs=$((runs_per_side * ${#BENCH_FILES[@]} * side_count))
  echo "[bench] starting: benchmarks=${#BENCH_FILES[@]} warmup=$WARMUP runs=$ITERATIONS sides=$side_count total_program_runs=$total_runs" >&2
fi

collect_samples "$LABEL_A" "$INTERPRETER_A"
if [[ -n "$INTERPRETER_B" ]]; then
  collect_samples "$LABEL_B" "$INTERPRETER_B"
fi

if [[ -n "$INTERPRETER_B" ]]; then
  printf "%-32s %12s %12s %12s %12s %12s %12s %10s\n" \
    "benchmark" "${LABEL_A}_mean" "${LABEL_A}_med" "${LABEL_A}_std" \
    "${LABEL_B}_mean" "${LABEL_B}_med" "${LABEL_B}_std" "A/B"
  for bench in "${BENCH_FILES[@]}"; do
    bench_name="$(basename "$bench")"
    file_a="$TMP_DIR/$(sanitize_name "$LABEL_A")__$(sanitize_name "$bench_name").samples"
    file_b="$TMP_DIR/$(sanitize_name "$LABEL_B")__$(sanitize_name "$bench_name").samples"

    read -r _ a_mean a_median a_stddev _ _ < <(compute_stats "$file_a")
    read -r _ b_mean b_median b_stddev _ _ < <(compute_stats "$file_b")
    speedup="$(awk -v a="$a_mean" -v b="$b_mean" 'BEGIN { if (b == 0) { print "inf" } else { printf "%.3fx", a / b } }')"

    printf "%-32s %12.6f %12.6f %12.6f %12.6f %12.6f %12.6f %10s\n" \
      "$bench_name" "$a_mean" "$a_median" "$a_stddev" \
      "$b_mean" "$b_median" "$b_stddev" "$speedup"
  done
else
  printf "%-32s %12s %12s %12s %12s %12s\n" \
    "benchmark" "${LABEL_A}_mean" "${LABEL_A}_med" "${LABEL_A}_std" \
    "${LABEL_A}_min" "${LABEL_A}_max"
  for bench in "${BENCH_FILES[@]}"; do
    bench_name="$(basename "$bench")"
    file_a="$TMP_DIR/$(sanitize_name "$LABEL_A")__$(sanitize_name "$bench_name").samples"
    read -r _ a_mean a_median a_stddev a_min a_max < <(compute_stats "$file_a")

    printf "%-32s %12.6f %12.6f %12.6f %12.6f %12.6f\n" \
      "$bench_name" "$a_mean" "$a_median" "$a_stddev" "$a_min" "$a_max"
  done
fi
