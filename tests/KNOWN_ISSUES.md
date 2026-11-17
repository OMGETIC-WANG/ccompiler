# Known issues and deferred behavior

This file records gaps found while auditing syntax coverage. Per the current
testing policy, known defects are not registered as failing or `WILL_FAIL`
tests. A case moves into the active suites when the compiler behavior is fixed
and has a normal positive or negative assertion.

## Deferred syntax

### Character literals

`tests/Deferred/CodeGen/CharLiterals` demonstrates the intended behavior.
Character tokens are scanned, but `ParsePrimary` has no character-literal AST
node or value conversion path.

### Parenthesis-free `sizeof`

`sizeof` currently requires an opening parenthesis:

```c
int value;
int size = sizeof value; /* rejected */
```

### Nested aggregate and compound initializers

The parser supports flat array and struct initializers. Nested brace
initializers, union brace initializers, and compound literals do not have a
stable positive AST path:

```c
int matrix[2][2] = {{1, 2}, {3, 4}};
union Value value = {1};
int number = (int){1};
```

### Sequence expressions after Sema

Parenthesized comma expressions are represented as `SequenceEval` in the AST,
but Sema rejects them with `UnsupportedSequenceEval`, so they have no Sema or
CodeGen coverage.

## Incorrect supported behavior

### Switch case bodies do not fall through

Case labels sharing a body are supported, but execution of one case body does
not continue into the next case body. For example, a `switch (1)` using:

```c
case 1:
  value = 1;
case 2:
  value += 2;
```

returns with `value == 1` instead of `value == 3`.

### Multi-token macro stringification loses spaces

`#value` works for a single token, but `STRINGIFY(hello world)` produces
`"helloworld"`.

### `#elif` can be selected after an earlier branch

When an earlier `#if` or `#elif` branch is active, a true later `#elif` can
incorrectly become active as well.

### Same-declaration initializers cannot reference earlier declarators

This is rejected even though the earlier declarator is already in scope:

```c
int first = 1, second = first + 1;
```

### Enum constants fail numeric operators

Enum constants work in returns, initializers, and switch labels, but arithmetic
and comparison operators currently reject enum operands instead of applying
their integer representation.

## Test infrastructure

### Generated diagnostic verifiers shadow `field_name`

When a generated diagnostic has a field named `field_name`, its `Verify`
implementation compares against the local YAML key name rather than the stored
member. `Diagnostics.Sema.UnknownField` therefore checks the error type and
location but leaves `fields` empty until the generator is fixed.
