# Repository Guidelines

## Project Structure & Module Organization

`ccompiler` is a C++23 C frontend and LLVM IR generator. The main CMake
targets are:

- `ccompiler_frontend`: the source manager, lexer/preprocessor, parser, AST,
  type system, and shared utilities.
- `ccompiler_codegen`: LLVM IR generation and builtin intrinsic registration.
- `ccompiler`: the command-line executable with IR output and JIT execution.

Important locations:

- `src/Main.cpp`: command-line entry point and top-level compilation flow.
- `src/SourceManager.*`: source loading, buffers, and text locations.
- `src/Lex/`: scanning, tokens, macros, and preprocessing.
- `src/AST/`: parsing, declarations, expressions, and type handling.
- `src/CodeGen/LLVMIR/`: LLVM IR generation.
- `src/JITRunner/`: native execution of generated modules.
- `src/Util/`: shared utilities.
- `tests/`: CTest-based AST and frontend verifier tests.
- `examples/brainfuck/`: end-to-end compiler integration example and fixtures.
- `docs/TODO.md`: C language feature roadmap and known limitations.
- `gdb_util/`: debugging helpers.

Keep related `.h`/`.cpp` files together and register new source files in the
nearest `CMakeLists.txt`.

## Build, Test, and Development Commands

Requirements are CMake 3.28+, a C++23 compiler, LLVM 23.1.3 exactly, and
`yaml-cpp` for the default test-enabled build. Set the `LLVM_DIR` CMake cache
variable or environment variable if LLVM is not discoverable through
`llvm-config`. The Brainfuck integration workflow also requires Python 3,
`llvm-as`, `llc`, and a host C compiler.

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
ctest --test-dir build -L ast --output-on-failure
build/ccompiler examples/brainfuck/main.c -o build/main.ll
python3 examples/brainfuck/test.py
```

The first two commands configure and build all compiler and test targets. The
full CTest command runs the AST and schema suites; `-L ast` selects the AST
suite. The `ccompiler` command emits LLVM IR; omit `-o` to print it. Use
`--show-ast`, `--show-tokens`, `--lex-only`, or `-jit-run` when debugging.

`examples/brainfuck/test.py` builds and runs the end-to-end Brainfuck example.
`examples/brainfuck/build.py` honors `CCOMPILER` and `HOST_CC`, so it can test
the compiler built outside the default `build/` path.

## Coding Style & Naming Conventions

Use two-space indentation and format changed C++ files with
`clang-format -i <files>`; `.clang-format` extends the LLVM style. Prefer
`#pragma once`, explicit types, and focused RAII ownership. Types and
functions use `PascalCase`, local variables and members use `snake_case`, and
macros/constants use `UPPER_SNAKE_CASE`. Match names already used by adjacent
modules.

Format changed `CMakeLists.txt` files with `cmake-format -i <files>`.

## Testing Guidelines

The CTest suite parses valid C through the real lexer, macro layer, and parser,
then compares the resulting `TranslationUnit` against a partial YAML assertion.
Add positive cases under `tests/AST/<group>/` as a matching `<case>.c` and
`<case>.ast.yaml`; CMake discovers them automatically and registers
`AST.<group>.<case>`. Keep YAML assertions focused on the behavior under test,
and remember that present fields are checked while omitted fields are ignored.

Files under `tests/AST/Schema/` intentionally exercise verifier failures and
are registered explicitly in `tests/CMakeLists.txt`. Add a new verifier
meta-test through that file when covering another failure mode. Update
`tests/AST/COVERAGE.md` when adding syntax coverage or changing a documented
limitation.

End-to-end integration cases live under `examples/brainfuck/tests/`. Name
programs `<case>.bf`, inputs `<case>.in`, and expected output
`<case>.expected`; add assertions to `examples/brainfuck/test.py`.

Run the full CTest suite and Brainfuck integration test before submitting
changes. Include an AST fixture or representative LLVM IR when changing parser
or code-generation behavior, and add a negative test when changing diagnostics
or rejection behavior. No coverage threshold or general-purpose unit-test
framework is currently configured.

## Commit & Pull Request Guidelines

Use Conventional Commit-style subjects such as `feat: add switch support`,
`fix: correct float literal parsing`, or `refactor: split scanner layers`.
Keep each commit focused.

There is no repository-specific PR template or CI configuration. PRs should
explain the behavior change, list validation commands, link relevant issues,
and include sample IR or before/after output when diagnostics or generated
code changes.
