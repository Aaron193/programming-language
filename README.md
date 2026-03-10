# Programming Language Interpreter

This project is a bytecode-compiled, stack-based interpreter implemented in C++.

## Supported Language Features

- Expressions: numbers, booleans, `null`, strings, grouping
- Arithmetic: `+`, `-`, `*`, `/`
- Comparison/equality: `>`, `>=`, `<`, `<=`, `==`, `!=`
- Logical operators: `!`, `and`, `or` (short-circuiting)
- Bit shifts: `<<`, `>>`
- Variables: `auto` declarations (with required initializer), assignment, compound assignment (`+=`, `-=`, `*=`, `/=`, `<<=`, `>>=`), lexical scope
- Update operators: `++`, `--`
- Control flow: `if`/`else`, `while`, `for`, foreach (`for (auto x : collection)`)
- Functions: declarations, parameters, return values, recursion
- Closures: nested functions with captured/upvalue variables
- Classes: class declarations, fields, methods, `this`
- Inheritance: subclassing with `<` and `super.method()` calls
- Modules: `import` / `export` with runtime module cache and circular import detection

## Runtime / Engine Features

- Bytecode compiler with Pratt parser
- VM call frame stack (non-recursive interpreter loop)
- Runtime error reporting with source line numbers
- Runtime call stack traces
- Compile error recovery with panic-mode synchronization
- Optional diagnostics flags (`--trace`, `--show-return`, `--disassemble`)
- Interactive REPL when no source file is provided

## Built-in Native Functions

- `clock()`
- `sqrt(x)`
- `len(x)`
- `type(x)`
- `str(x)`
- `num(x)`

## Build

```bash
./build.sh
```

Build a profiler-friendly optimized binary:

```bash
./build.sh --release --profiling
```

Direct-threaded VM dispatch via computed-goto is enabled by default.
GCC or Clang is required to build the project.

## Run

```bash
./build/interpreter path/to/program.expr
```

Or start REPL:

```bash
./build/interpreter
```

## Tests

Run expression samples:

```bash
./tests/test_expr.sh
```

Run additional suites:

```bash
./tests/test_native_errors.sh
./tests/test_compile_recovery.sh
./tests/test_runtime_stacktrace.sh
./tests/test_cli_flags.sh
./tests/test_repl.sh
./tests/test_import.sh
```

## Benchmarks

Run each benchmark once:

```bash
./benchmarks/run_benchmarks.sh
```

Run repeated statistical benchmarking (mean/median/stddev):

```bash
./benchmarks/compare_benchmarks.sh --iterations 7 --warmup 1
```

Compare two interpreter binaries (A/B):

```bash
./benchmarks/compare_benchmarks.sh \
  --interpreter-a /path/to/interpreter_a \
  --interpreter-b /path/to/interpreter_b \
  --label-a baseline \
  --label-b candidate \
  --iterations 7 \
  --warmup 1
```

## Profiling

The recommended profiling workflow is Valgrind Callgrind. It gives you
function-level hotspots and callgraph information without adding manual timing
logs to the interpreter.

Build an optimized binary with debug symbols and frame pointers:

```bash
./build.sh --release --profiling
```

Profile a program or benchmark:

```bash
./scripts/profile_callgrind.sh benchmarks/bench_feature_mix.expr
./scripts/profile_callgrind.sh --inclusive --tree benchmarks/bench_fibonacci.expr
```

Re-annotate an existing Callgrind output without rerunning the program:

```bash
./scripts/profile_callgrind.sh --annotate-only build/callgrind/bench_feature_mix-20260310-120000.out
```

For source-level expansion of a saved profile:

```bash
callgrind_annotate --auto=yes build/callgrind/<profile>.out
```

Recommended profiling targets:

- `benchmarks/bench_fibonacci.expr` for call overhead and dispatch-heavy recursion
- `benchmarks/bench_sort.expr` and `benchmarks/bench_matrix.expr` for arithmetic and container traffic
- `benchmarks/bench_feature_mix.expr` for a broader mixed workload

Notes:

- Callgrind is much slower than native execution. Prefer profiling one benchmark at a time.
- Hotspots in `VirtualMachine::run(...)`, stack push/pop, and `Value` copies are good candidates for further investigation.
- Linux `perf` is lower overhead, but it only works on machines where perf events are enabled. On such a machine, use:

```bash
perf record -g -- ./build/interpreter benchmarks/bench_feature_mix.expr
perf report
```

## Modules

Top-level exports:

```expr
export function add(a, b) { return a + b; }
export auto PI = 3.14159;
export class Vector {}
```

Namespace import:

```expr
import math from "./math.expr";
print(math.PI);
print(math.add(1, 2));
```

Named import and aliasing:

```expr
import { add, PI } from "./math.expr";
import { add as sum } from "./math.expr";
```

Notes:
- Import paths must be string literals and include full filename.
- Relative paths are resolved from the importing file's directory.
- REPL mode does not allow `import` statements.
