# Test suite

The CTest suite is grouped by test kind. `tests/CMakeLists.txt` owns the shared
`ccompiler_tests` executable and adds the top-level suites. Lexer, AST
subgroups, CodeGen, and Diagnostics each register their own tests in a local
`CMakeLists.txt`.

Expected values live beside their fixtures. A `CMakeLists.txt` may name a
fixture, test, label, or expected file, but must not contain diagnostic text,
runtime return values, or other per-case expectations.

Fixtures under `tests/Deferred/` document intended behavior that is not yet
supported. They are not registered with CTest. Known defects are documented in
`tests/KNOWN_ISSUES.md` rather than being encoded as failing tests.

Configuring requires Python 3 with PyYAML because generated diagnostics are
produced at build time. The default test-enabled build also requires `yaml-cpp`.

## Running

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Useful labels are `lexer`, `ast`, `sema`, `schema`, `codegen`, `include`, and
`diagnostics`.
For example:

```bash
ctest --test-dir build -L lexer --output-on-failure
ctest --test-dir build -L ast --output-on-failure
ctest --test-dir build -L sema --output-on-failure
ctest --test-dir build -L diagnostics --output-on-failure
```

Run an AST fixture directly when inspecting a mismatch:

```bash
build/tests/ccompiler_tests tests/AST/Declarations/FunctionSignatures.c
```

## Test data

| Suite | Required fixture files | Optional fixture files |
| --- | --- | --- |
| Lexer | `<case>.c`, `<case>.tokens` | `<case>.args` |
| AST | `<case>.c`, `<case>.ast.yaml` | none |
| AST schema failure | `<case>.ast.yaml`, `<case>.stderr` | none |
| Sema | `<case>.c`, `<case>.ast.yaml` | none |
| CodeGen | `<source>.c`, `<source>.stdout` | `<source>.args` |
| Diagnostics source | `<case>.c`, `<case>.error.yaml` | none |
| Diagnostics driver | `<case>.args`, `<case>.stderr` | `<case>.c` |
| Deferred | source and any eventual sidecars | none |

The exact comparison runner is `tests/cmake/ExpectProcess.cmake`. It checks
the exit status, compares stdout and stderr exactly after stripping leading and
trailing whitespace, and requires an unspecified stream to be empty.

`.args` files contain one raw command-line argument per line. Tests may use
these placeholders:

- `@SOURCE_FILE@`: the test's primary source file.
- `@SOURCE_ROOT@`: the repository source root.
- `@BINARY_ROOT@`: the suite's CMake binary directory.

Structured source diagnostics run through `ccompiler_tests` and compare the
recorded error type, location, and selected fields. Driver diagnostics keep the
exact stderr runner; for driver cases without a `.c` fixture, the `.args` file
is the complete argument list.

## Structured diagnostics

Error types and their formatting are generated from
`src/Diagnostics/Errors.yaml` during the build. Add or update the definition,
then use the generated `errs::<ErrorName>` value at the diagnostic call site.
All Lex, AST, and Sema diagnostics must use a generated error instead of raw
message chunks. The generated header only forward-declares YAML nodes; `Verify`
implementations are compiled into the test target, so the compiler does not
depend on `yaml-cpp`.

Source diagnostic fixtures use this format:

```yaml
diagnostics:
  - error: UnknownVar
    file: "@SOURCE_FILE@"
    location: "1:25-1:32"
    fields:
      var_name: missing
```

`diagnostics` is an ordered sequence with exact length. Each item requires
`error`, `location`, and `fields`; `file` is optional. `location` may be a
half-open line/column range such as `1:25-1:32`, or the exact source text at the
location. Only fields present in the fixture are checked, but unknown field
names fail. A generated field uses an exact scalar comparison. `TypeRef` fields
instead accept either the `ToString()` result or the structured type descriptor
used by AST tests.

## Lexer assertions

Lexer tests run the real lexer and preprocessor directly, then compare the
ordered `RawLexer` token stream. They do not parse the source. Each nonempty
`.tokens` line describes one token as `<token_type> [token_value]`; the final
line must be `EndOfFile`.

Token types accept either the full `TokenType` name or an alias from
`tests/Lexer/token_type_aliases`. The alias file contains one
`<token_type> <alias>` mapping per line. Omit `token_value` for token types
whose source spelling is fixed, including keywords and punctuation. Keep it for
tokens whose spelling varies, such as identifiers, literals, and the
compatibility spellings of `KwRestrict`. When omitted, only the token type is
checked. Unquoted values cannot contain whitespace, quotes, or backslashes.
Quoted values support `\\`, `\"`, `\n`, `\r`, `\t`, `\0`, and `\xNN`.

Token values are compared with `SourceManager::GetText`, so they preserve the
source spelling. A string literal containing `\n` therefore appears as a
quoted `.tokens` value with both escapes doubled:

```text
LiteralString "\"line\\n\""
EndOfFile
```

Add a `.c` and matching `.tokens` file under `tests/Lexer/`. The suite is
auto-discovered as `Lexer.<case>` with the `lexer;frontend` labels.

An optional `.args` file supplies raw Lexer harness arguments, one per line.
The supported argument is repeated `-I <directory>`. Args support the same
`@SOURCE_FILE@`, `@SOURCE_ROOT@`, and `@BINARY_ROOT@` placeholders as process
tests. Include-focused cases also receive the `include` label.

## AST assertions

AST tests parse valid C through the real lexer, macro layer, and parser, then
compare the resulting `TranslationUnit` with a partial YAML assertion. Present
fields must match; omitted fields are ignored. Mapping keys are validated, and
node sequences have exact length and order.

Add `<case>.c` and `<case>.ast.yaml` under an existing `tests/AST/<group>/`
directory. The group CMake file discovers positive cases and creates
`AST.<group>.<case>`. A missing expectation is a configuration error.

```yaml
TranslationUnit:
  decls:
    - FunctionDecl:
        name: main
        function_type:
          FunctionType:
            return_type: {BuiltinType: {type_name: int}}
```

Type references use the same single-node-key descriptor as AST nodes. Add
`tags` when the test depends on type qualifiers. Supported tags are `const`,
`unsigned`, `volatile`, and `restrict`; optional values use YAML `null`.
The `storage_class` field accepts `None`, `Static`, or `Extern`.

```yaml
type:
  PointerType:
    tags: [const]
    pointee_type: {BuiltinType: {type_name: int, tags: [const]}}

init: null
```

Files under `tests/AST/Schema/` are intentionally malformed expectations. Pair
each `<case>.ast.yaml` with an exact `<case>.stderr` file. The shared
`tests/Support/SchemaFixture.c` source is used for these failure tests.

Sema fixtures are parsed and passed through `AnalyzeTranslationUnit` before
the same partial YAML verifier runs. They may assert expression `type`,
`value_category`, `operation_type`, inserted `ImplicitCast` nodes, and the
compound-assignment `compound_op`, `left_value_cast_type`, and
`result_cast_type` annotations.

`Lexer.Format`, `AST.Declarations.BindingIdentity`,
`AST.Schema.VerifierRegistry`, `Diagnostics.Formatting`, and
`Diagnostics.ErrorGenerator` are self-tests. Their expected values remain in
the test implementation because they are not CMake registration data.

## Adding compiler cases

- Lexer cases add `<case>.c` and `<case>.tokens` under `tests/Lexer/`; the
  shared `ccompiler_add_lexer_cases` registration requires both files and
  rejects orphan expectations. Add `<case>.args` when the source needs `-I`
  include paths.
- CodeGen cases add their stem to `CODEGEN_JIT_CASES` in
  `tests/CodeGen/CMakeLists.txt`; configuration fails when a `.c` file in that
  directory is neither registered nor moved to `tests/Deferred/`. The expected
  `.stdout` is derived from the source filename.
- Source diagnostics call `add_frontend_diagnostic_case` with the public test
  name and fixture stem. Driver diagnostics continue to call
  `add_diagnostic_case` with an exact `.stderr` sidecar.
- AST schema failures call the suite-local `add_ast_schema_failure` in
  `tests/AST/Schema/CMakeLists.txt`.
- Sema fixtures call `ccompiler_add_sema_cases` from
  `tests/Sema/CMakeLists.txt`.
- Every new expected file must remain beside the fixture it describes.

See `tests/AST/COVERAGE.md` for the syntax, semantic, lowering, and diagnostic
coverage matrix. It distinguishes `Covered`, `Partial`, `Deferred`,
`Known defect`, and `Internal only` behavior. See `tests/KNOWN_ISSUES.md` for
minimal reproductions of known gaps.
