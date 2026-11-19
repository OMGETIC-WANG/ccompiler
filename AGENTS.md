# Repository Guidelines

## Project Structure & Module Organization

`ccompiler` is a C++23 C frontend and LLVM IR generator. The compilation
pipeline is source loading, lexing/preprocessing, parsing, semantic analysis,
LLVM IR generation, and optional JIT execution. The main CMake targets are:

- `ccompiler_frontend`: the source manager, lexer/preprocessor, parser, AST,
  type system, semantic analysis, and shared utilities.
- `ccompiler_codegen`: LLVM IR generation and builtin intrinsic registration.
- `ccompiler`: the command-line executable with IR output and JIT execution.
- `ccompiler_ast_tests`, `ccompiler_sema_tests`, `ccompiler_lexer_tests`, and
  `ccompiler_diagnostics_tests`: test-type-specific fixture drivers.
- `ccompiler_self_tests`: Google Test executable for unit-level self-tests.

Important locations:

- `src/Main.cpp`: command-line entry point and top-level compilation flow.
- `src/SourceManager.*`: source loading, buffers, and text locations.
- `src/Lex/`: scanning, tokens, macros, and preprocessing.
- `src/AST/`: parsing, declarations, expressions, types, and AST printing.
- `src/Sema/`: semantic analysis, type checking, conversions, and AST
  annotations consumed by code generation.
- `src/CodeGen/LLVMIR/`: LLVM IR generation.
- `src/JITRunner/`: native execution of generated modules.
- `src/Util/`: command-line parsing, diagnostics, and shared utilities.
- `tests/`: fixture-driven Lexer, AST, Sema, CodeGen, and diagnostic tests,
  plus Google Test self-tests under `tests/SelfTests/`.
- `examples/brainfuck/`: end-to-end compiler integration example and fixtures.
- `musl_pre_include/`: compatibility headers that must precede system headers.
- `gdb_util/`: debugging helpers.

Keep related `.h`/`.cpp` files together and register new source files in the
nearest `CMakeLists.txt`. Test-driver targets and shared fixture support are
defined in `tests/CMakeLists.txt`; suite-local fixtures and registration belong
in the corresponding `tests/<suite>/CMakeLists.txt`.

## Build, Test, and Development Commands

Requirements are CMake 3.28+, a C++23 compiler, LLVM 23.1.3 exactly, and
Python 3 with PyYAML for generated diagnostics. The default test-enabled build
also requires `yaml-cpp` and Google Test with a CMake package installed under
`CMAKE_INSTALL_PREFIX` or `CMAKE_PREFIX_PATH`. Set the `LLVM_DIR` CMake cache
variable or environment variable if LLVM is not discoverable through
`llvm-config`. The Brainfuck integration workflow also requires `llvm-as`,
`llc`, and a host C compiler.

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure
ctest --test-dir build -L lexer --output-on-failure
ctest --test-dir build -L ast --output-on-failure
ctest --test-dir build -L sema --output-on-failure
ctest --test-dir build -L codegen --output-on-failure
ctest --test-dir build -L diagnostics --output-on-failure
ctest --test-dir build -L selftest --output-on-failure
build/ccompiler examples/brainfuck/main.c -o build/main.ll
python3 examples/brainfuck/test.py
```

The first two commands configure and build all compiler and test targets. The
full CTest command runs every registered suite. Useful labels are `lexer`,
`ast`, `sema`, `schema`, `codegen`, `include`, `diagnostics`, and `selftest`.

A source argument is required. Pass `-` as the source file to read stdin, and
pass `-o -` to write LLVM IR to stdout:

```bash
build/ccompiler source.c -o -
build/ccompiler - -o build/source.ll < source.c
```

Use `--show-ast`, `--show-tokens`, `--lex-only`, `--no-ir`, or `-jit-run` when
debugging. AST color output is controlled by `--color` and honors a nonempty
`NO_COLOR` environment variable.

`examples/brainfuck/test.py` builds and runs the end-to-end Brainfuck example.
`examples/brainfuck/build.py` honors `CCOMPILER` and `HOST_CC`, so it can test
the compiler built outside the default `build/` path. Note: it is only required
to run when `src/` was changed.

## Coding Style & Naming Conventions

Use two-space indentation and format changed C++ files with
`clang-format -i <files>`; `.clang-format` extends the LLVM style. Prefer
`#pragma once`, explicit types, and focused RAII ownership. Types and
functions use `PascalCase`, local variables and members use `snake_case`, and
macros/constants use `UPPER_SNAKE_CASE`. Match names already used by adjacent
modules.

Format changed `CMakeLists.txt` files with `cmake-format -i <files>`.

Format changed Python files with `ruff format <files>`

## Testing Guidelines

The CTest suite is grouped by test kind. `tests/README.md` documents the fixture
table and comparison runner; follow it when adding cases. In general:

- AST cases pair `<case>.c` with `<case>.ast.yaml` and compare a partial
  `TranslationUnit` assertion. They are auto-discovered under
  `tests/AST/<group>/` and registered as `AST.<group>.<case>`.
- Sema cases pair `<case>.c` with `<case>.ast.yaml`, run
  `AnalyzeTranslationUnit`, and then use the same partial YAML verifier. Add
  them under `tests/Sema/` and register the group with
  `ccompiler_add_sema_cases`.
- Lexer cases pair `<case>.c` with `<case>.tokens` and assert the ordered
  post-preprocessing `RawLexer` output directly. They live under
  `tests/Lexer/` and are auto-registered with `ccompiler_add_lexer_cases`. An
  optional `.args` file supplies `-I` include paths.
- CodeGen cases are JIT-run end-to-end cases with a source file and an exact
  `.stdout` expectation. An optional `.args` file supplies one raw
  command-line argument per line.
- Structured source diagnostics pair a `.c` fixture with `.error.yaml`.
  Driver-only cases continue to use an exact `.stderr` sidecar and may contain
  only `.args`.

Present YAML fields are checked while omitted fields are ignored. Mapping keys
are validated, and node sequences have exact length and order. Keep assertions
focused on the behavior under test.

Files under `tests/AST/Schema/` intentionally exercise verifier failures and
pair each malformed expectation with an exact `.stderr` sidecar. They are
registered in `tests/AST/Schema/CMakeLists.txt`; add a new verifier meta-test
through that file when covering another failure mode. Keep per-case diagnostic
text, process expectations, and runtime values in sidecar files rather than
CMake registration code. Update `tests/AST/COVERAGE.md` when adding syntax
coverage or changing a documented limitation.

End-to-end integration cases live under `examples/brainfuck/tests/`. Name
programs `<case>.bf`, inputs `<case>.in`, and expected output
`<case>.expected`; add assertions to `examples/brainfuck/test.py`.

Run the full CTest suite and Brainfuck integration test before submitting
changes; the Brainfuck test is required whenever files under `src/` change.
Add a Lexer fixture for lexical or preprocessing output, an AST or Sema fixture
for frontend semantic behavior, and a CodeGen JIT case or representative
IR/self-test for lowering behavior. Add a diagnostics case when changing a
diagnostic or rejection path. When adding AST/type nodes or printer behavior,
keep verifier registration, AST output, and schema descriptors in sync; the
Google Test self-tests and `AST.PrintingCLI` cover those paths. Add unit-level
regressions as focused `TEST`/`TEST_F` cases under `tests/SelfTests/`; no
coverage threshold is currently configured.

## Commit & Pull Request Guidelines

Use Conventional Commit-style subjects such as `feat: add switch support`,
`fix: correct float literal parsing`, or `refactor: split scanner layers`.
Keep each commit focused.

There is no repository-specific PR template or CI configuration. PRs should
explain the behavior change, list validation commands, link relevant issues,
and include sample IR or before/after output when diagnostics or generated
code changes.
