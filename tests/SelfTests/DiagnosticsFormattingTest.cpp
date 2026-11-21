#include "AST/Context.h"
#include "AST/TranslationUnit.h"
#include "AST/TypeContext.h"
#include "Diagnostics/Errors.generated.h"
#include "Lex/Lexer.h"
#include "Lex/Macro.h"
#include "Sema/SemanticAnalysis.h"
#include "SourceManager.h"
#include "Util/Diagnostic.h"
#include "Util/Util.h"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <yaml-cpp/yaml.h>

namespace {

std::string TrimWhitespace(std::string_view value) {
  constexpr std::string_view whitespace = " \t\n\r\f\v";
  const std::size_t first = value.find_first_not_of(whitespace);
  if (first == std::string_view::npos) {
    return {};
  }
  const std::size_t last = value.find_last_not_of(whitespace);
  return std::string(value.substr(first, last - first + 1));
}

std::optional<int> OptionalValue(bool fail) {
  if (fail) {
    return std::nullopt;
  }
  return 7;
}

std::optional<int> TryAssignValue(bool fail) {
  int value = 0;
  TRY_ASSIGN(value, OptionalValue(fail));
  return value;
}

std::optional<int> TryAssignNewValue(bool fail) {
  TRY_ASSIGN_NEW(value, OptionalValue(fail));
  return value;
}

OptUnique<int> OptionalPointer(bool fail) {
  if (fail) {
    return std::nullopt;
  }
  return std::make_unique<int>(9);
}

std::optional<int> TryAssignNewMoveOnly(bool fail) {
  TRY_ASSIGN_NEW(value, OptionalPointer(fail));
  return *value;
}

std::optional<std::monostate> TryValue(bool fail) {
  TRY(OptionalValue(fail));
  return Succeeded();
}

std::optional<int> CountedValue(int &evaluation_count, bool fail = false) {
  ++evaluation_count;
  if (fail) {
    return std::nullopt;
  }
  return 11;
}

std::optional<int> TryAssignCounted(int &evaluation_count, bool fail = false) {
  int value = 0;
  TRY_ASSIGN(value, CountedValue(evaluation_count, fail));
  return value;
}

std::optional<int> TryAssignNewCounted(int &evaluation_count,
                                       bool fail = false) {
  TRY_ASSIGN_NEW(value, CountedValue(evaluation_count, fail));
  return value;
}

std::optional<std::monostate> TryCounted(int &evaluation_count,
                                         bool fail = false) {
  TRY(CountedValue(evaluation_count, fail));
  return Succeeded();
}

std::optional<std::pair<int, int>> TryAssignNewTwice(int &evaluation_count) {
  TRY_ASSIGN_NEW(first, CountedValue(evaluation_count));
  TRY_ASSIGN_NEW(second, CountedValue(evaluation_count));
  return std::pair{first, second};
}

} // namespace

TEST(DiagnosticsFormatting, ConsumeUntilRetainsStopTokens) {
  FixedLexer recovery_lexer({
      Token(TokenType::Identifier),
      Token(TokenType::RBrace),
      Token(TokenType::Semicolon),
      Token(TokenType::Identifier),
  });
  Lexer lexer(recovery_lexer);
  constexpr std::array recovery_stops{
      TokenType::Semicolon,
      TokenType::RBrace,
  };

  std::optional<bool> consumed = lexer.ConsumeUntil(recovery_stops);
  ASSERT_TRUE(consumed.has_value());
  ASSERT_TRUE(*consumed);
  EXPECT_EQ(lexer.PeekNextToken()->type, TokenType::RBrace);
  lexer.ConsumeNextToken();
  consumed = lexer.ConsumeUntil(recovery_stops);
  ASSERT_TRUE(consumed.has_value());
  ASSERT_TRUE(*consumed);
  EXPECT_EQ(lexer.PeekNextToken()->type, TokenType::Semicolon);
  lexer.ConsumeNextToken();

  constexpr std::array eof_stops{TokenType::RBrace};
  consumed = lexer.ConsumeUntil(eof_stops);
  ASSERT_TRUE(consumed.has_value());
  EXPECT_FALSE(*consumed);
  EXPECT_EQ(lexer.PeekNextToken()->type, TokenType::EndOfFile);
}

TEST(DiagnosticsFormatting, VerifiesGeneratedFields) {
  const errs::UnknownVar unknown_var("localized");
  EXPECT_TRUE(unknown_var.Verify(YAML::Load("{}")));
  EXPECT_TRUE(unknown_var.Verify(YAML::Load("var_name: localized")));
  EXPECT_FALSE(unknown_var.Verify(YAML::Load("var_name: different")));
  EXPECT_FALSE(unknown_var.Verify(YAML::Load("unknown: localized")));

  EXPECT_TRUE(errs::ArgumentCountMismatch(2).Verify(
      YAML::Load("expected_arguments: 2")));
  EXPECT_TRUE(errs::UnsupportedExpressionASTNode(ASTNodeType::VarRef)
                  .Verify(YAML::Load("node_type: VarRef")));
}

TEST(DiagnosticsFormatting, FormatsTypeReferences) {
  TypeContext types;
  const errs::TypeMismatch mismatch(types.Pointer(types.getInt()),
                                    types.getInt());

  EXPECT_EQ(mismatch.ToString(), "Type mismatch: expected Ptr<int>, got int");
  EXPECT_TRUE(mismatch.Verify(YAML::Load("expected_type: Ptr<int>")));
  EXPECT_FALSE(mismatch.Verify(YAML::Load("expected_type: int")));
}

TEST(DiagnosticsFormatting, FormatsLocalDiagnostics) {
  SourceManager sources;
  SourceHandle source = sources.LoadSource("abc\n", "<stdin>");
  DiagnosticContext diagnostics(sources);
  SourceLocation location{
      .source_id = source.id,
      .begin = 1,
      .end = 2,
  };

  diagnostics.Error(location, errs::UnknownVar("localized"));
  const std::string formatted = diagnostics.Format(diagnostics.Messages());

  EXPECT_EQ(TrimWhitespace(formatted),
            "<stdin>:1:2: error: Unknown var: localized");
  EXPECT_TRUE(diagnostics.HasErrors());
  EXPECT_EQ(diagnostics.Messages().size(), 1);
}

TEST(DiagnosticsFormatting, PreservesCollectedDiagnostics) {
  SourceManager sources;
  SourceHandle source = sources.LoadSource("abc\n", "<stdin>");
  DiagnosticContext diagnostics(sources);
  SourceLocation location{
      .source_id = source.id,
      .begin = 1,
      .end = 2,
  };
  diagnostics.Error(location, errs::UnknownVar("localized"));
  diagnostics.Error(location, errs::ExpectedSemicolon());

  EXPECT_EQ(TrimWhitespace(diagnostics.Format(diagnostics.Messages())),
            "<stdin>:1:2: error: Unknown var: localized\n"
            "<stdin>:1:2: error: Expected ';'");
}

TEST(DiagnosticsFormatting, FormatsGlobalDiagnostics) {
  SourceManager sources;
  DiagnosticContext diagnostics(sources);
  DiagnosticMessage message{
      .error = std::make_shared<errs::UnexpectedToken>(),
      .formatted_message = "Unexpected token",
  };

  EXPECT_EQ(TrimWhitespace(diagnostics.Format(message)),
            "ccompiler: error: Unexpected token");
}

TEST(DiagnosticsFormatting, PropagatesOptionalFailures) {
  EXPECT_EQ(TryAssignValue(false), 7);
  EXPECT_FALSE(TryAssignValue(true).has_value());
  EXPECT_EQ(TryAssignNewValue(false), 7);
  EXPECT_FALSE(TryAssignNewValue(true).has_value());
  EXPECT_EQ(TryAssignNewMoveOnly(false), 9);
  EXPECT_FALSE(TryAssignNewMoveOnly(true).has_value());
  EXPECT_TRUE(TryValue(false).has_value());
  EXPECT_FALSE(TryValue(true).has_value());

  int evaluation_count = 0;
  std::optional<std::pair<int, int>> values =
      TryAssignNewTwice(evaluation_count);
  ASSERT_TRUE(values.has_value());
  EXPECT_EQ(values->first, 11);
  EXPECT_EQ(values->second, 11);
  EXPECT_EQ(evaluation_count, 2);

  evaluation_count = 0;
  EXPECT_EQ(TryAssignCounted(evaluation_count), 11);
  EXPECT_EQ(evaluation_count, 1);

  evaluation_count = 0;
  EXPECT_TRUE(TryCounted(evaluation_count).has_value());
  EXPECT_EQ(evaluation_count, 1);

  evaluation_count = 0;
  EXPECT_FALSE(TryAssignCounted(evaluation_count, true).has_value());
  EXPECT_EQ(evaluation_count, 1);

  evaluation_count = 0;
  EXPECT_FALSE(TryCounted(evaluation_count, true).has_value());
  EXPECT_EQ(evaluation_count, 1);

  evaluation_count = 0;
  EXPECT_EQ(TryAssignNewCounted(evaluation_count), 11);
  EXPECT_EQ(evaluation_count, 1);

  evaluation_count = 0;
  EXPECT_FALSE(TryAssignNewCounted(evaluation_count, true).has_value());
  EXPECT_EQ(evaluation_count, 1);
}

TEST(DiagnosticsFormatting, LexerFailureStopsTranslationUnit) {
  SourceManager sources;
  SourceHandle source = sources.LoadSource("int value = @;\n", "<lexer-error>");
  DiagnosticContext diagnostics(sources);
  MacroContext macros;
  TypeContext types;
  CompileContext context(types, sources, diagnostics);
  RawLexer raw_lexer(source, macros, sources, context);
  Lexer lexer(raw_lexer);

  OptUnique<TranslationUnit> translation_unit =
      ParseTranslationUnit(lexer, types, sources, diagnostics);

  EXPECT_FALSE(translation_unit.has_value());
  EXPECT_TRUE(diagnostics.HasErrors());
}

TEST(DiagnosticsFormatting, PreprocessorFailureStopsTranslationUnit) {
  SourceManager sources;
  SourceHandle source = sources.LoadSource("#error stop\n", "<pp-error>");
  DiagnosticContext diagnostics(sources);
  MacroContext macros;
  TypeContext types;
  CompileContext context(types, sources, diagnostics);
  RawLexer raw_lexer(source, macros, sources, context);
  Lexer lexer(raw_lexer);

  OptUnique<TranslationUnit> translation_unit =
      ParseTranslationUnit(lexer, types, sources, diagnostics);

  EXPECT_FALSE(translation_unit.has_value());
  EXPECT_TRUE(diagnostics.HasErrors());
}

TEST(DiagnosticsFormatting, ParserRecoveryReturnsPartialTranslationUnit) {
  SourceManager sources;
  SourceHandle source =
      sources.LoadSource("int first = ;\nint second = ;\n", "<recovery>");
  DiagnosticContext diagnostics(sources);
  MacroContext macros;
  TypeContext types;
  CompileContext context(types, sources, diagnostics);
  RawLexer raw_lexer(source, macros, sources, context);
  Lexer lexer(raw_lexer);

  OptUnique<TranslationUnit> translation_unit =
      ParseTranslationUnit(lexer, types, sources, diagnostics);

  ASSERT_TRUE(translation_unit.has_value());
  EXPECT_NE(*translation_unit, nullptr);
  EXPECT_EQ(diagnostics.Messages().size(), 2);
}

TEST(DiagnosticsFormatting, SemaFailureLeavesTranslationUnitUnanalyzed) {
  SourceManager sources;
  SourceHandle source = sources.LoadSource(
      "int main(void) { return \"invalid\"; }\n", "<sema-error>");
  DiagnosticContext diagnostics(sources);
  MacroContext macros;
  TypeContext types;
  CompileContext context(types, sources, diagnostics);
  RawLexer raw_lexer(source, macros, sources, context);
  Lexer lexer(raw_lexer);

  OptUnique<TranslationUnit> translation_unit =
      ParseTranslationUnit(lexer, types, sources, diagnostics);
  ASSERT_TRUE(translation_unit.has_value());
  ASSERT_NE(*translation_unit, nullptr);
  EXPECT_FALSE(diagnostics.HasErrors());

  std::optional<std::monostate> result =
      AnalyzeTranslationUnit(**translation_unit, types, diagnostics);

  EXPECT_FALSE(result.has_value());
  EXPECT_TRUE(diagnostics.HasErrors());
  EXPECT_FALSE((*translation_unit)->is_semantically_analyzed);
}
