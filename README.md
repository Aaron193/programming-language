# Programming Language Interpreter

This project is a bytecode-compiled, stack-based interpreter implemented in C++.
The language is strictly typed by default; there is no opt-in non-strict mode.

## Supported Language Features

- Expressions: numbers, booleans, `null`, strings, grouping
- Arithmetic: `+`, `-`, `*`, `/`
- Comparison/equality: `>`, `>=`, `<`, `<=`, `==`, `!=`
- Logical operators: `!`, `&&`, `||` (short-circuiting)
- Bitwise operators: `&`, `|`, `^`, `~`, `<<`, `>>`
- Variables: `var name Type = value`, `var name = value`, `const name Type = value`, `const name = value`, `const name = @import(...)`, assignment, compound assignment (`+=`, `-=`, `*=`, `/=`, `&=`, `|=`, `^=`, `<<=`, `>>=`), lexical scope
- Update operators: `++`, `--`
- Control flow: `if`/`else`, `while`, `for`, foreach (`for (var x Type : collection)`), `break`, `continue`, labeled loops
- Functions: `fn name(param Type, ...) ReturnType { ... }`, omitted `void` on named functions/methods (`fn name(...) { ... }`), block function literals, expression-bodied lambdas, recursion
- Closures: nested functions with captured/upvalue variables
- Types: `type Name struct { ... }`, aliases via `type Alias ExistingType`, fields/methods, `this`
- Inheritance: `type Child struct < Parent { ... }` with `super.method()` calls
- Modules: `@import(...)` bindings with capitalization-based exports
- Operator annotations: `@operator(\"+\")` lowers annotated method calls at compile time
- Native packages: runtime-loadable C++ shared libraries imported through `@import(...)`

Notes:
- Semicolons are not part of normal statement syntax. They are only used as separators inside `for (...)` clauses.
- Local declarations still require explicit types except for `@import(...)` bindings.
- `type ... struct` is currently class-backed syntax, not a separate value-type runtime.
- Supported operator annotations are currently limited to `+`, `-`, `*`, `/`, `==`, `!=`, `<`, `<=`, `>`, `>=`.
- Newline continuation is explicit: `(`, `[`, `.`, `as`, assignment operators, and binary operators must stay on the same line as the expression they continue.
- After the continuation token is consumed, the rest of the call/index/member access may span later lines.
- A newline before `++` or `--` starts a new prefix expression; it does not continue a postfix update.
- Trailing commas in call argument lists are currently not supported.

Examples:

Functions and lambdas:

```expr
fn applyTwice(f fn(i32) i32, value i32) i32 {
  return f(f(value))
}

var addOne fn(i32) i32 = fn(x i32) => x + 1
print(applyTwice(addOne, 40))
```

Closures:

```expr
fn makeAdder(x i32) fn(i32) i32 {
  return fn(y i32) => x + y
}

var addTen fn(i32) i32 = makeAdder(10)
print(addTen(32))
```

Types and fields:

```expr
type Bag struct {
  value i32
  label str
}

var bag Bag = Bag()
bag.value = 42
bag.label = "snacks"
print(bag.label)
```

Collections:

```expr
var scores Dict<str, i32> = {"alice": 7, "bob": 9}
scores["alice"] = scores["alice"] + 1
print(scores.get("alice"))
print(scores.has("bob"))
```

Syntax note: continuation tokens such as `(`, `[`, `.`, `as`, assignment operators, and binary operators must stay on the same line as the expression they continue. For example, `print(1 +` on one line and `2)` on the next is valid, but moving `+` to the next line is rejected.

## Runtime / Engine Features

- Bytecode compiler with Pratt parser
- VM call frame stack (non-recursive interpreter loop)
- Runtime error reporting with source line numbers
- Runtime call stack traces
- Compile error reporting with source line numbers
- Optional diagnostics flags (`--trace`, `--show-return`, `--disassemble`, `--frontend-timings`, `--frontend-timings-json`)
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

Build a debug binary with sanitizers:

```bash
./build.sh --debug --asan
./build.sh --debug --ubsan
./build.sh --debug --asan --ubsan
```

Build a profiler-friendly optimized binary:

```bash
./build.sh --release --profiling
```

Sanitizer builds cannot be combined with `--lto`, `--pgo-generate`, or
`--pgo-use`.

When running shell-based test suites under AddressSanitizer in this environment,
disable leak detection to avoid `LeakSanitizer`/output-capture issues:

```bash
ASAN_OPTIONS=detect_leaks=0 bash tests/test_ast_optimizer.sh
```

Direct-threaded VM dispatch via computed-goto is enabled by default.
GCC or Clang is required to build the project.

## Run

```bash
./build/interpreter path/to/program.mog
```

Add extra native package roots if needed:

```bash
./build/interpreter --package-path /path/to/packages path/to/program.mog
```

Inspect frontend phase timings while compiling:

```bash
./build/interpreter --frontend-timings path/to/program.mog
```

The timing summary reports parse, symbol collection, import resolution, bind,
type-check, HIR lowering, HIR optimization, cache stats, and total frontend
time.

Emit the same frontend metrics as JSON:

```bash
./build/interpreter --frontend-timings-json path/to/program.mog
```

Or start REPL:

```bash
./build/interpreter
```

## Examples

Manual runnable demos live under `examples/`. These are intended for developer
validation and interactive exploration, not for automated CI coverage.

The visible `window` demos require SDL2 to be present so the optional
package is built:

```bash
./build/interpreter examples/window_open.mog
./build/interpreter examples/window_events.mog
```

## Tests

Run expression samples:

```bash
./tests/test_expr.sh
```

Run additional suites:

```bash
./tests/test_ast_frontend.sh
./tests/test_native_errors.sh
./tests/test_compile_recovery.sh
./tests/test_runtime_stacktrace.sh
./tests/test_cli_flags.sh
./tests/test_source_extension.sh
./tests/test_repl.sh
./tests/test_import.sh
./tests/test_const.sh
./tests/test_typechecker_errors.sh
./tests/test_logical_operator_syntax.sh
./tests/test_package_validation.sh
./tests/test_syntax_breakage.sh
./tests/test_newline_syntax.sh
./tests/test_frontend_benchmark.sh
./tests/test_vscode_manifest.sh
```

## VS Code

The VS Code extension lives under `tooling/vscode-mog`.

`.mog` files now declare a Mog language icon through the extension manifest, so
Explorer and tabs can show the bundled icon when the active file icon theme
supports language default icons. Themes such as `Minimal` may still show the
generic file icon because the extension does not override the user's full file
icon theme.

## Benchmarks

Run each benchmark once:

```bash
./benchmarks/run_benchmarks.sh
```

Run the Python equivalents once:

```bash
./benchmarks/run_python_benchmarks.sh
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

Compare Mog benchmarks against the Python equivalents:

```bash
./benchmarks/compare_benchmarks.sh \
  --interpreter-a ./build/interpreter \
  --filter-a 'benchmarks/bench_*.mog' \
  --label-a mog \
  --interpreter-b python3 \
  --filter-b 'benchmarks/python/bench_*.py' \
  --label-b python \
  --iterations 7 \
  --warmup 1
```

Measure frontend compile-time only with the dedicated helper:

```bash
./build/frontend_benchmark tests/sample_var.mog
./build/frontend_benchmark --json tests/sample_var.mog
./benchmarks/compare_frontend_benchmarks.sh --iterations 7 --warmup 1
```

## Profiling

The recommended profiling workflow is Valgrind Callgrind. It gives you
function-level hotspots and callgraph information without adding manual timing
logs to the interpreter.

Before profiling, make sure the benchmark is stable enough to represent normal
runtime work:

```bash
./benchmarks/compare_benchmarks.sh \
  --filter 'benchmarks/bench_feature_mix.mog' \
  --iterations 7 \
  --warmup 1
```

Build an optimized binary with debug symbols and frame pointers:

```bash
./build.sh --release --profiling
```

Profile a program or benchmark:

```bash
./scripts/profile_callgrind.sh benchmarks/bench_feature_mix.mog
./scripts/profile_callgrind.sh --inclusive --tree benchmarks/bench_fibonacci.mog
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

- `benchmarks/bench_feature_mix.mog` as the default general-runtime baseline
- `benchmarks/bench_fibonacci.mog` for call overhead and dispatch-heavy recursion
- `benchmarks/bench_sort.mog` and `benchmarks/bench_matrix.mog` for arithmetic and container traffic
- `benchmarks/bench_class_member_access.mog` when focusing on property/method dispatch

Notes:

- Callgrind is much slower than native execution. Prefer profiling one benchmark at a time.
- The profiling script saves raw profiles under `build/callgrind/`.
- Hotspots in `VirtualMachine::run(...)`, stack push/pop, and `Value` copies are good candidates for further investigation.
- Linux `perf` is lower overhead, but it only works on machines where perf events are enabled. On such a machine, use:

```bash
perf record -g -- ./build/interpreter benchmarks/bench_feature_mix.mog
perf report
```

## Modules

Top-level exports:

```expr
fn Add(a i32, b i32) i32 { return a + b }
const PI f64 = 3.14159
type Vector struct {}
```

Namespace import:

```expr
const math = @import("./math.mog")
print(math.PI)
print(math.Add(1, 2))
```

Labeled loop control:

```expr
outer: while (true) {
    for (var i i32 = 0; i < 10; i++) {
        if (i == 3) continue
        if (i == 7) break outer
    }
}
```

Named import and aliasing:

```expr
const { Add, PI } = @import("./math.mog")
const { Add as sum } = @import("./math.mog")
```

Notes:
- Import paths must be string literals and include full filename.
- Relative paths are resolved from the importing file's directory.
- REPL mode does not allow `@import(...)`.
- Capitalized top-level names are public module exports; lowercase names are private.

## Native Packages

Source modules and native packages share the same import syntax:

```expr
const nativeMath = @import("math")
const { addI64, greet } = @import("math")
```

Path-like imports such as `./math.mog` stay source-module imports. Bare import
names such as `"math"` or `"window"` resolve as package imports.

Package resolution looks for a project root `mog.toml` and `mog.lock` first,
then falls back to scanning package roots. The interpreter searches for native
packages in:

- `build/packages` relative to the interpreter binary
- any additional roots passed with `--package-path`
- `packages/` relative to the importing source file or current working directory

Each package is a shared library that exports `exprRegisterPackage()` and
declares its functions/constants using the ABI in `src/NativePackageAPI.hpp`.
This repository includes a namespaced reference package in
`packages/examples/math/`.

Native packages can now return opaque package types such as
`counter.Counter`. Those values are still GC-managed native handles internally,
and the VM still invokes the package-provided finalizer when they are released.
A reference package lives in `packages/examples/counter/`.

Source files can declare opaque package types directly:

```expr
const counter = @import("counter")

const c counter.Counter = counter.create(10i64)
print(counter.read(c))
```

Official runtime-maintained packages still use the reserved `mog:*` canonical
package ID internally. The first official package is exposed to users as
`window` and is built only when SDL2 is available at configure time:

Headless smoke usage for tests:

```expr
const window = @import("window")

const win window.Window =
    window.create("Demo", 800i64, 600i64)
const evt window.Event? = window.pollEvent(win)
window.clear(win)
window.present(win)
window.close(win)
```

For simple realtime graphics, `window` also exposes:

- `clearRgb(win, r, g, b)`
- `fillRect(win, x, y, width, height, r, g, b)`
- `delay(ms)`
- `KEY_SPACE`
- `KEY_ESCAPE`

See `examples/flappy_bird.mog` for a playable rectangle-rendered demo built
entirely in the language on top of those primitives.

Visible manual demos live under `examples/` and call `window.show(win)` after
creation so the current hidden-by-default behavior remains stable for tests.

If SDL2 is not installed, the interpreter still builds normally and simply
skips the optional `window` package target.

## Package Manifests

Packages can declare authoring metadata in `mog.toml`:

```toml
kind = "native"
import_name = "math"
namespace = "examples"
name = "math"
version = "0.1.0"
abi_version = 3
description = "Reference namespaced math package."
dependencies = []
```

Projects mark their root with `mog.toml` and pin package resolution in
`mog.lock`. Native and source packages can also ship a `package.api.mog` file
for editor navigation, readable package API docs, and public opaque type
declarations.

Validate a package directory against its manifest and compiled shared library:

```bash
./build/interpreter --validate-package packages/examples/math
./build/interpreter --validate-package=packages/examples/counter
```

The validator checks package ID syntax, reserved `mog` usage, manifest/ABI
compatibility, registration metadata, and exported native signature parsing.
