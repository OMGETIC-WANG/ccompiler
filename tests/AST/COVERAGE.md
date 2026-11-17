# Frontend syntax coverage matrix

This document tracks the C subset accepted by the compiler and the tests that
exercise each feature. Coverage is behavior-oriented: a feature is considered
covered only when the relevant layer checks its observable result. Known gaps
are listed in [KNOWN_ISSUES.md](../KNOWN_ISSUES.md).

## Status legend

| Status | Meaning |
|---|---|
| Covered | Registered tests assert the layer named in the column. |
| Partial | Useful assertions exist, but one or more important behaviors are missing. |
| Deferred | The compiler does not currently provide a stable positive behavior. |
| Known defect | A behavior is accepted or intended but currently incorrect. |
| Internal only | The value is compiler-internal and not produced by source syntax. |

## Test layers

| Layer | Purpose | Representative cases |
|---|---|---|
| AST | Final parsed `TranslationUnit`, types, declarations, expression shape, and precedence | `AST.Declarations.FunctionSignatures`, `AST.Types.DeclaratorSpiral`, `AST.Expressions.Precedence` |
| Sema | Types, value categories, implicit casts, constant folding, and call/initializer conversions | `Sema.Core.Expressions`, `Sema.Core.AggregateAccess`, `Sema.Core.FunctionCalls` |
| CodeGen/JIT | LLVM lowering validated by executing `main` | `CodeGen.ControlFlow`, `CodeGen.Operators`, `CodeGen.AggregatesPointers`, `CodeGen.Functions` |
| Diagnostics | Ordered error type, location, and selected fields | `Diagnostics.Sema.*`, `Diagnostics.Parser.*`, `Diagnostics.Preprocessor.*` |
| Include | Quoted/angle includes, nested lookup, and include search order | `Include.LocalRelative`, `Include.SearchOrder` |

## Lexing and literals

| Feature | AST | Sema | CodeGen | Cases | Status |
|---|---|---|---|---|---|
| Decimal, hexadecimal, octal, binary integers and suffixes | Covered | Covered | Covered | `AST.Expressions.IntegerLiterals`, `CodeGen.Operators` | Covered |
| Decimal, exponent, hexadecimal floating literals and suffixes | Covered | Covered | Covered | `AST.Expressions.FloatLiterals`, `CodeGen.LongDouble*` | Covered |
| String literals, escapes, and adjacent concatenation | Covered | Covered | Covered | `AST.Expressions.StringLiterals`, `Sema.Core.Expressions`, `CodeGen.SizeofTypes` | Covered |
| Invalid hexadecimal string escapes | Rejected | N/A | N/A | `Diagnostics.Lex.InvalidHexEscape` | Covered |
| Character literals | Unsupported | N/A | Deferred | `tests/Deferred/CodeGen/CharLiterals` | Deferred |
| Comments and whitespace | Implicit | Implicit | Implicit | All source fixtures | Covered |

## Preprocessor and includes

| Feature | Positive coverage | Negative coverage | Status |
|---|---|---|---|
| Object-like and function-like macros | `AST.Pipeline.Macros` | Existing parser diagnostics | Covered |
| Nested expansion, token paste, single-token stringification, variadic macros | `AST.Pipeline.AdvancedMacros` | `Diagnostics.Parser.*` for malformed declarations | Covered |
| Multi-token stringification | None | None | Known defect |
| `#if`, `#ifdef`, `#ifndef`, nesting, inactive groups | `AST.Pipeline.If`, `AST.Pipeline.Ifdef` | Existing directive diagnostics | Covered |
| `defined`, arithmetic, shift, comparison, logical, conditional conditions | `AST.Pipeline.If` | Existing expression diagnostics | Covered |
| `#elif` and `#else` selection | `AST.Pipeline.If`, `AST.Pipeline.Ifdef` | `Diagnostics.Preprocessor.UnexpectedElse` | Partial |
| `#elif` after an earlier selected branch | None | None | Known defect |
| `#undef` | `AST.Pipeline.Undef` | `Diagnostics.Preprocessor.MissingUndefIdentifier` | Covered |
| `#error` and `#warning` control flow | `AST.Pipeline.Warning`, `AST.Pipeline.If` | `Diagnostics.Preprocessor.ErrorDirective` | Partial: warnings are not emitted |
| Quoted and angle includes, nested relative lookup, search order, include guards | `AST.Pipeline.IncludeQuoted`, `Include.*` | `Diagnostics.Driver.MissingInclude` | Covered |

## Declarations and types

| Feature | Coverage | Cases | Status |
|---|---|---|---|
| Builtin integer/floating types, signedness, `long double` | AST/Sema/CodeGen | `AST.Declarations.BuiltinTypes`, `Sema.Core.LongDouble` | Covered |
| `const`, `volatile`, `restrict` on objects and pointers | AST/Sema/CodeGen | `AST.Declarations.QualifiedDeclarators`, `CodeGen.Qualifiers` | Covered |
| `static` and `extern` linkage, tentative definitions, local externs | AST/Sema/CodeGen | `AST.Declarations.ExternDeclarations`, `Sema.Core.ExternalLinkage`, `CodeGen.QualifiedIR` | Covered |
| `typedef` and typedef names | AST | `AST.Declarations.Typedefs` | Covered |
| Struct/union/enum definitions, forward declarations, fixed enum underlying type | AST | `AST.Declarations.Aggregates`, `AST.Declarations.ForwardTags` | Covered |
| Pointers, arrays, functions, function pointers, pointer-to-array, complex declarators | AST | `AST.Types.FunctionPointers`, `AST.Types.PointersArrays`, `AST.Types.DeclaratorSpiral` | Covered |
| Multiple declarators with distinct pointer/array types | AST | `AST.Declarations.MultipleDeclarators` | Covered |
| Named, anonymous, array, and variadic function parameters | AST/Sema/CodeGen | `AST.Declarations.FunctionSignatures`, `Sema.Core.FunctionCalls`, `CodeGen.Functions` | Covered |
| Initializer referencing an earlier declarator in the same declaration | None | None | Known defect |
| `register` and local static storage | Rejected | `Diagnostics.*` | Intentionally unsupported |

## Initializers

| Form | Coverage | Cases | Status |
|---|---|---|---|
| Scalar initializers and implicit conversions | AST/Sema | `Sema.Core.Expressions`, `Sema.Core.Conversions` | Covered |
| Flat array brace initializers and trailing comma | AST/Sema/CodeGen | `AST.Initializers.AggregateInitializers`, `CodeGen.AggregatesPointers` | Covered |
| Positional struct initializer | AST/Sema/CodeGen | `AST.Initializers.AggregateInitializers`, `Sema.Core.AggregateAccess`, `CodeGen.AggregatesPointers` | Covered |
| Designated struct initializer | AST/Sema/CodeGen | Same cases as positional struct initializers | Covered |
| String initialization and size deduction | AST/Sema/CodeGen | `AST.Expressions.StringLiterals`, `Sema.Core.ArraySize` | Covered |
| Oversized and incomplete-array rejection | Diagnostics | `Diagnostics.Sema.ArrayInitializerBounds`, `Diagnostics.Sema.ArraySizeInitializer` | Covered |
| Nested braces, union brace initialization, compound literals | None | None | Deferred |

## Expressions and operators

| Group | AST | Sema | CodeGen | Cases | Status |
|---|---|---|---|---|---|
| Assignment and all compound assignments | Covered | Covered | Covered | `AST.Expressions.BinaryOperators`, `Sema.Core.Conversions`, `CodeGen.Operators` | Covered |
| Arithmetic, comparison, logical, bitwise, shifts, subscript | Covered | Covered | Covered | `AST.Expressions.BinaryOperators`, `CodeGen.Operators` | Covered |
| Prefix/postfix increments, unary arithmetic, address/deref | Covered | Covered | Covered | `AST.Expressions.UnaryOperators`, `CodeGen.Operators`, `CodeGen.AggregatesPointers` | Covered |
| Calls, nested calls, function-pointer calls | Covered | Covered | Covered | `AST.Expressions.Calls`, `Sema.Core.FunctionCalls`, `CodeGen.Functions` | Covered |
| Field and pointer-field access | Covered | Covered | Covered | `AST.Expressions.PostfixAccess`, `Sema.Core.AggregateAccess`, `CodeGen.AggregatesPointers` | Covered |
| Cast, conditional, and sequence syntax | AST | Cast/conditional Sema | Cast/conditional CodeGen | `AST.Expressions.*`, `Sema.Core.Expressions` | Partial: sequence expressions are rejected after Sema |
| Precedence and associativity across the operator ladder | Covered | N/A | Runtime cases | `AST.Expressions.Precedence` | Covered |
| Lvalue, operand type, subscript, dereference, call arity rejection | N/A | Diagnostics | N/A | `Diagnostics.Sema.*` | Covered |

## Statements and control flow

| Feature | AST | Sema | CodeGen | Cases | Status |
|---|---|---|---|---|---|
| Compound, declaration, expression, empty statements; nested scopes | Covered | Covered | Covered | `AST.Statements.*`, `CodeGen.DeclBindings` | Covered |
| if/else chains | Covered | Covered | Covered | `AST.Statements.Selection`, `CodeGen.ControlFlow` | Covered |
| while, do-while, for, declaration-form for, empty for clauses | Covered | Covered | Covered | `AST.Statements.Loops`, `CodeGen.ControlFlow` | Covered |
| break and continue in loops | Covered | Negative context diagnostics | Covered | `AST.Statements.Loops`, `Diagnostics.Sema.BreakOutside`, `Diagnostics.Sema.ContinueOutside` | Covered |
| switch, grouped case labels, default, break | Covered | Covered | Covered | `AST.Statements.Switch`, `CodeGen.ControlFlow` | Partial |
| Switch body fallthrough between separate case bodies | AST shape only | Accepted | Incorrect execution | None | Known defect |
| return and goto/label | Covered | Covered | Covered | `AST.Statements.Jumps`, `CodeGen.ControlFlow` | Covered |

## Semantic annotations and lowering

| Behavior | Coverage | Cases | Status |
|---|---|---|---|
| Expression type, value category, operation type, and constant folding | Sema | `Sema.Core.Expressions`, `Sema.Core.Constants`, `Sema.Core.Promotions` | Covered |
| Array-to-pointer and function-to-pointer decay | Sema/CodeGen | `Sema.Core.ArrayDecay`, `Sema.Core.FunctionCalls`, `CodeGen.Functions` | Covered |
| Scalar conditions, comparisons, case conversion, and compound assignments | Sema/CodeGen | `Sema.Core.Conversions`, `CodeGen.Conversions`, `CodeGen.Operators` | Covered |
| Function-pointer and variadic call handling | Sema/CodeGen | `Sema.Core.FunctionCalls`, `CodeGen.Functions`, `CodeGen.LongDouble` | Covered |
| Aggregate initializer element conversion and field lvalue types | Sema/CodeGen | `Sema.Core.AggregateAccess`, `CodeGen.AggregatesPointers` | Covered |
| Pointer arithmetic, dereference, and array indexing | Sema/CodeGen | `Sema.Core.Expressions`, `CodeGen.AggregatesPointers` | Covered |

## AST node inventory

Every source-produced AST node has at least one positive AST fixture:

| Node family | Representative cases |
|---|---|
| Translation unit, function, variable, parameter, typedef declarations | `AST.Declarations.*` |
| Struct, union, enum, and enum element declarations | `AST.Declarations.Aggregates`, `AST.Declarations.ForwardTags` |
| Numeric/string literals and variable references | `AST.Expressions.IntegerLiterals`, `AST.Expressions.FloatLiterals`, `AST.Expressions.StringLiterals`, `AST.Expressions.Calls` |
| Binary, unary, cast, implicit cast, field access, calls, sequence, conditional | `AST.Expressions.*`, `Sema.Core.*` |
| Declaration, compound, if, loop, switch/case/default, jump, return, empty statements | `AST.Statements.*` |
| Intrinsic calls | `AST.Intrinsics.*` |

`EmptyStmt` is intentionally filtered from `CompoundStmt`; `AST.Statements.Empty`
asserts that filtering. `ImplicitCast` is introduced by Sema and is covered by
the Sema cases above.

## Type inventory

| Type feature | Cases | Status |
|---|---|---|
| `BuiltinType`, `void` through `long double`, unsigned tags | `AST.Declarations.BuiltinTypes` | Covered |
| `PointerType`, `ArrayType`, `FunctionType`, struct/union/enum types | `AST.Types.*`, `AST.Declarations.Aggregates`, `Sema.Core.FunctionCalls` | Covered |
| `const`, `volatile`, `restrict`, `unsigned` tags | `AST.Declarations.*`, `Sema.Core.QualifierCompatibility` | Covered |
| Internal `i1` | `Sema.Core.Conversions` | Internal only |

## Harness and diagnostics

| Area | Cases | Status |
|---|---|---|
| Partial YAML AST verification and exact sequences | All AST/Sema fixtures | Covered |
| Unknown fields, node/tag mismatches, sequence length, and null/scalar mismatch | `AST.Schema.*` | Covered |
| AST verifier and intrinsic registration completeness | `AST.Schema.VerifierRegistry` | Covered |
| CodeGen source registration and expected-output pairing | CodeGen configure-time orphan check | Covered |
| Key lexer, preprocessor, parser, Sema, initializer, and control-flow rejection paths | `Diagnostics.*` | Covered |
| String field named `field_name` in generated diagnostic verifiers | `Diagnostics.Sema.UnknownField` | Known defect |
