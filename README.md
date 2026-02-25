# Programming Language Interpreter

This project is a bytecode-compiled, stack-based interpreter implemented in C++.

## Supported Language Features

- Expressions: numbers, booleans, `null`, strings, grouping
- Arithmetic: `+`, `-`, `*`, `/`
- Comparison/equality: `>`, `>=`, `<`, `<=`, `==`, `!=`
- Logical operators: `!`, `and`, `or` (short-circuiting)
- Bit shifts: `<<`, `>>`
- Variables: `var` declarations, assignment, compound assignment (`+=`, `-=`, `*=`, `/=`, `<<=`, `>>=`), lexical scope
- Update operators: `++`, `--`
- Control flow: `if`/`else`, `while`, `for`, foreach (`for (var x : collection)`)
- Functions: declarations, parameters, return values, recursion
- Closures: nested functions with captured/upvalue variables
- Classes: class declarations, fields, methods, `this`
- Inheritance: subclassing with `<` and `super.method()` calls

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
```
