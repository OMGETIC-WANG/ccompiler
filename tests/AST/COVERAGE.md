# AST coverage matrix

This matrix tracks behavior that is observable in the final `TranslationUnit`,
its non-owning `TypeRef` values, or its declaration references. A deferred
entry has no CTest case until the parser behavior can satisfy a stable positive
assertion.

## AST nodes

| Node | Status | Primary fixture |
|---|---|---|
| `TranslationUnit` | Covered | All fixtures |
| `FunctionDecl` | Covered | `Declarations/FunctionSignatures` |
| `StructDecl` | Covered | `Declarations/Aggregates`, `Declarations/ForwardTags` |
| `UnionDecl` | Covered | `Declarations/Aggregates` |
| `LiteralString` | Covered | `Expressions/StringLiterals` |
| `LiteralNum` | Covered | `Expressions/IntegerLiterals`, `Expressions/FloatLiterals` |
| `BinaryExpr` | Covered | `Expressions/BinaryOperators`, `Expressions/Precedence` |
| `UnaryExpr` | Covered | `Expressions/UnaryOperators` |
| `DeclStmt` | Covered | `Declarations/MultipleDeclarators`, `Statements/Declarations` |
| `VarDecl` | Covered | `Declarations/BuiltinTypes` |
| `ParamDecl` | Covered | `Declarations/FunctionSignatures`, `Declarations.BindingIdentity` |
| `TypedefDecl` | Covered | `Declarations/Typedefs` |
| `VarRef` | Covered | `Expressions/Calls`, `Statements/Declarations` |
| `FunctionInvocation` | Covered | `Expressions/Calls` |
| `SequenceEval` | Covered | `Expressions/Sequence` |
| `CompoundStmt` | Covered | All function fixtures |
| `IfStmt` | Covered | `Statements/Selection` |
| `ReturnStmt` | Covered | `Statements/Declarations` |
| `WhileStmt` | Covered | `Statements/Loops` |
| `DoWhileStmt` | Covered | `Statements/Loops` |
| `ForStmt` | Covered | `Statements/Loops` |
| `SwitchStmt` | Covered | `Statements/Switch` |
| `CaseStmt` | Covered | `Statements/Switch` |
| `DefaultStmt` | Covered | `Statements/Switch` |
| `BreakStmt` | Covered | `Statements/Loops`, `Statements/Switch` |
| `ContinueStmt` | Covered | `Statements/Loops` |
| `EnumDecl` | Covered | `Declarations/Aggregates`, `Declarations/ForwardTags` |
| `EnumElemDecl` | Covered | `Declarations/Aggregates` |
| `LabelStmt` | Covered | `Statements/Jumps` |
| `GotoStmt` | Covered | `Statements/Jumps` |
| `TypeCast` | Covered | `Expressions/UnaryOperators` |
| `ImplicitCast` | Covered after Sema | `Sema.Core.ArrayDecay`, `Sema.Core.Promotions`, `Sema.Core.Conversions` |
| `GetField` | Covered | `Expressions/PostfixAccess` |
| `PtrGetField` | Covered | `Expressions/PostfixAccess` |
| `IntrinsicCall` | Covered | `Intrinsics/Sizeof`, `Intrinsics/VaBuiltins` |
| `EmptyStmt` | Runtime filtered | `Statements/Empty` verifies filtering from `CompoundStmt` |
| `ConditionalExpr` | Covered | `Expressions/Conditional` |

## Types

| Type node or variant | Status | Primary fixture |
|---|---|---|
| `BuiltinType` | Covered | `Declarations/BuiltinTypes` |
| `PointerType` | Covered | `Types/PointersArrays` |
| `ArrayType` | Covered | `Types/PointersArrays` |
| `FunctionType` | Covered | `Declarations/FunctionSignatures` |
| `StructType` | Covered | `Declarations/Aggregates` |
| `UnionType` | Covered | `Declarations/Aggregates` |
| `EnumType` | Covered | `Declarations/Aggregates` |
| `void`, `char`, `short`, `int`, `long`, `long long` | Covered | `Declarations/BuiltinTypes` |
| `float`, `double`, `long double` literal type | Covered | `Expressions/FloatLiterals` |
| `I1` | Internal only | Not produced by source declarations |
| `const` tag | Covered | `Declarations/StorageAndQualifiers` |
| `unsigned` tag | Covered | `Declarations/BuiltinTypes` |

## Initializers

| Form | Status | Primary fixture |
|---|---|---|
| Scalar value initializer | Covered | `Expressions/IntegerLiterals` |
| Array brace initializer | Covered | `Initializers/AggregateInitializers` |
| Designated struct initializer | Covered | `Initializers/AggregateInitializers` |
| String initializer and adjacent strings | Covered | `Expressions/StringLiterals` |
| Multiple declarators with mixed initialization | Covered | `Declarations/MultipleDeclarators` |

## Operators

| Group | Status | Primary fixture |
|---|---|---|
| Assignment and all compound assignments | Covered | `Expressions/BinaryOperators` |
| Arithmetic, comparison, logical, bitwise, shift, subscript | Covered | `Expressions/BinaryOperators` |
| Prefix/postfix increment and decrement | Covered | `Expressions/UnaryOperators`, `Statements/Loops` |
| Unary plus/minus, logical and bitwise not | Covered | `Expressions/UnaryOperators` |
| Address and dereference | Covered | `Expressions/UnaryOperators` |
| Member access and pointer member access | Covered | `Expressions/PostfixAccess` |
| Cast, conditional, sequence, call | Covered | `Expressions/UnaryOperators`, `Expressions/Conditional`, `Expressions/Sequence`, `Expressions/Calls` |
| Precedence and associativity | Covered | `Expressions/Precedence` |

## Semantic annotations

| Behavior | Status | Primary fixture |
|---|---|---|
| Expression type and value category | Covered | `Sema.Core.Promotions`, `Sema.Core.ArrayDecay` |
| Array-to-pointer decay | Covered | `Sema.Core.ArrayDecay` |
| Incomplete array size deduction | Covered | `Sema.Core.ArraySize` |
| Implicit cast insertion | Covered | `Sema.Core.Promotions`, `Sema.Core.ArrayDecay`, `Sema.Core.Conversions` |
| Boolean, comparison, case, and compound-assignment conversion metadata | Covered | `Sema.Core.Conversions` |
| Folded numeric and enum constants | Covered | `Sema.Core.Constants` |

## Pipeline and meta-tests

| Area | Status | Primary fixture or test |
|---|---|---|
| Object-like macro expansion | Covered | `Pipeline/Macros` |
| Function-like macro expansion | Covered | `Pipeline/Macros` |
| `#if` numeric and nested conditional inclusion | Covered | `Pipeline/If` |
| `#ifdef`/`#ifndef` conditional inclusion | Covered | `Pipeline/Ifdef` |
| Macro removal with `#undef` | Covered | `Pipeline/Undef` |
| `#warning` non-fatal directive handling | Covered | `Pipeline/Warning` |
| Active and inactive `#error` handling | Covered | `Preprocessor.ErrorDirective`, `Pipeline/If` |
| Quoted and nested relative `#include` | Covered | `Pipeline/IncludeQuoted`, `Include.LocalRelative` |
| Include guards and repeated headers | Covered | `Pipeline/IncludeQuoted` |
| Include search paths and search order | Covered | `Include.SearchOrder` |
| Intrinsic registration shared with compiler | Covered | `AST.Schema.VerifierRegistry`, `Intrinsics/*` |
| Verifier registration completeness | Covered | `AST.Schema.VerifierRegistry` |
| Unknown fields and malformed descriptors | Covered | `AST.Schema.*` |
| Sequence, scalar, literal, tag, and null mismatches | Covered | `AST.Schema.*` |

## Deferred parser behavior

These cases are known from source inspection or focused probes but are not in
the green suite because they currently reject, abort, or produce an unstable
AST:

| Behavior | Current status |
|---|---|
| Character literals | Lexed but no primary-expression AST path |
| Variadic parameter lists | Current parse probe aborts on type specifiers |
| `sizeof` applied without parentheses | Intrinsic parser currently requires `(` |
| `long double` declarations | Explicitly rejected; `L` literal type is covered |
| `volatile`, `extern`, and `register` declarations | Explicitly rejected or unsupported |
| Nested brace initializers and compound literals | No stable supported AST path |
| Intermediate `Declaration`/`Declarator` trees | Out of scope; final resolved types are tested |
| Source locations and invalid-program diagnostics | Out of scope for this AST suite |
