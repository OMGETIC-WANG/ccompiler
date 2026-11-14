# AST tests

The test suite parses valid C source through the real lexer, macro layer, and
AST parser, then compares the resulting `TranslationUnit` with a partial YAML
assertion. Code generation and runtime behavior are outside this suite.

## Running

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j2
ctest --test-dir build -L ast --output-on-failure
```

`ctest --test-dir build --output-on-failure` runs the complete registered test
set. Every positive case carries the `ast` and `frontend` labels. Verifier
meta-tests carry `ast` and `schema`.

Run one fixture directly when inspecting a mismatch:

```bash
build/tests/ccompiler_tests tests/AST/Declarations/FunctionSignatures.c
```

## Diagnostic tests

The `diagnostics` label covers user-facing error handling. Source fixtures live
under `tests/Diagnostics/` and are run through the `ccompiler` executable by
`ExpectDiagnostic.cmake`, which checks the expected `file:line:column: error:`
lines, empty stdout, and nonzero exit status. Leading and trailing whitespace
in the captured output and expectations is ignored. Driver cases verify errors
without a source location use the `ccompiler: error:` prefix.

Multi-error recovery cases pair a `.c` fixture with a `.stderr` file. Use
`SOURCE_FILE_PLACEHOLDER` wherever the absolute fixture path should appear.

`Diagnostics.Formatting` directly checks `DiagnosticContext` formatting and the
payload carried by `DiagnosticError`.

```bash
ctest --test-dir build -L diagnostics --output-on-failure
```

The expected YAML path defaults to the source path with `.c` replaced by
`.ast.yaml`.

## Adding a case

Add `<case>.c` and `<case>.ast.yaml` under `tests/AST/<group>/`. CMake discovers
positive cases automatically and creates a test named
`AST.<group>.<case>`. A missing expectation file is a configuration error.

Keep only fields relevant to the behavior under test. A present field must
match; an omitted field is not checked. Mapping keys are validated, so a typo
is an error. Node sequences have exact length and order.

```yaml
TranslationUnit:
  decls:
    - FunctionDecl:
        name: main
        function_type:
          FunctionType:
            return_type: {BuiltinType: {type_name: int}}
```

## Type assertions

Type references use the same single-node-key descriptor as AST nodes. Add
`tags` when the test depends on type qualifiers. Tags are checked as an exact
set and may be omitted when irrelevant.

```yaml
type:
  PointerType:
    tags: [const]
    pointee_type: {BuiltinType: {type_name: int, tags: [const]}}
```

Supported tags are `const` and `unsigned`. Optional values use YAML `null`:

```yaml
type:
  ArrayType:
    size: null
    element_type: {BuiltinType: {type_name: int}}

init: null
```

Aggregate type metadata can be asserted through `fields`:

```yaml
StructType:
  type_name: Point
  is_complete: true
  fields:
    - name: x
      type: {BuiltinType: {type_name: int}}
```

Designated initializers use ordered `field_initializers` entries with `name`
and `value`. Intrinsic calls use `name` and ordered `args` entries wrapped as
`Expr` or `Type`:

```yaml
IntrinsicCall:
  name: sizeof
  args:
    - Type: {BuiltinType: {type_name: int}}
```

## Verifier tests

Files under `tests/AST/Schema/` intentionally fail the YAML comparison and are
run through `ExpectFailure.cmake`. They verify unknown-field handling, wrong
node descriptors, sequence lengths, scalar and literal mismatches, type tags,
and explicit null checks. `AST.Schema.VerifierRegistry` also checks that every
AST node and type node has a verifier.

See `tests/AST/COVERAGE.md` for the current node, type, and operator coverage
matrix and for syntax intentionally deferred from the green suite.
