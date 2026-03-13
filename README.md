# Programming Language Interpreter

This project is a bytecode-compiled, stack-based interpreter implemented in C++.

## Supported Language Features

- Expressions: numbers, booleans, `null`, strings, grouping
- Arithmetic: `+`, `-`, `*`, `/`
- Comparison/equality: `>`, `>=`, `<`, `<=`, `==`, `!=`
- Logical operators: `!`, `and`, `or` (short-circuiting)
- Bit shifts: `<<`, `>>`
- Variables: explicit typed declarations (with required initializer), `const` immutable bindings, assignment, compound assignment (`+=`, `-=`, `*=`, `/=`, `<<=`, `>>=`), lexical scope
- Update operators: `++`, `--`
- Control flow: `if`/`else`, `while`, `for`, foreach (`for (Type x : collection)`)
- Functions: declarations, parameters, return values, recursion
- Closures: nested functions with captured/upvalue variables
- Classes: class declarations, fields, methods, `this`
- Inheritance: subclassing with `<` and `super.method()` calls
- Modules: `import` / `export` with runtime module cache and circular import detection
- Native packages: runtime-loadable C++ shared libraries imported through `import`

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

Add extra native package roots if needed:

```bash
./build/interpreter --package-path /path/to/packages path/to/program.expr
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
./tests/test_const.sh
./tests/test_typechecker_errors.sh
./tests/test_package_validation.sh
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
export fn add(a, b) { return a + b; }
export const f64 PI = 3.14159;
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

## Native Packages

Source modules and native packages share the same import syntax:

```expr
import nativeMath from "examples:math";
import { addI64, greet } from "examples:math";
```

Namespaced package imports use `namespace:name` and resolve to nested package
directories such as `packages/examples/math/package.so`. Bare package imports
like `"example_math"` still work for legacy packages.

The interpreter searches for native packages in:

- `build/packages` relative to the interpreter binary
- any additional roots passed with `--package-path`
- `packages/` relative to the importing source file or current working directory

Each package is a shared library that exports `exprRegisterPackage()` and
declares its functions/constants using the ABI in `src/NativePackageAPI.hpp`.
This repository includes a namespaced reference package in
`packages/examples/math/` and a legacy compatibility package in
`packages/example_math/`.

Native packages can now return opaque native handles through signatures such as
`fn() -> handle<examples:counter:CounterHandle>`. Handles are GC-managed by the
VM and invoke the package-provided finalizer when released. A reference handle
package lives in `packages/examples/counter/`.

Source files can declare native handles directly:

```expr
import counter from "examples:counter";

const handle<examples:counter:CounterHandle> c = counter.create(10i64);
print(counter.read(c));
```

Official runtime-maintained packages use the reserved `mog:*` namespace. The
first official package is `mog:window`, built only when SDL2 is available at
configure time:

```expr
import window from "mog:window";

const handle<mog:window:WindowHandle> win =
    window.create("Demo", 800i64, 600i64);
const handle<mog:window:EventHandle>? evt = window.pollEvent(win);
window.clear(win);
window.present(win);
window.close(win);
```

If SDL2 is not installed, the interpreter still builds normally and simply
skips the optional `mog:window` package target.

## Package Manifests

Namespaced packages can declare authoring metadata in `package.toml`:

```toml
namespace = "examples"
name = "math"
version = "0.1.0"
abi_version = 3
description = "Reference namespaced math package."
dependencies = []
```

Validate a package directory against its manifest and compiled shared library:

```bash
./build/interpreter --validate-package packages/examples/math
./build/interpreter --validate-package=packages/examples/counter
```

The validator checks package ID syntax, reserved `mog` usage, manifest/ABI
compatibility, registration metadata, and exported native signature parsing.
