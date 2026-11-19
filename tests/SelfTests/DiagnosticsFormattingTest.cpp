#include "AST/Context.h"
#include "AST/TypeContext.h"
#include "Diagnostics/Errors.generated.h"
#include "Lex/Lexer.h"
#include "SourceManager.h"
#include "Util/Diagnostic.h"

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

  ASSERT_TRUE(lexer.ConsumeUntil(recovery_stops));
  EXPECT_EQ(lexer.PeekNextToken().type, TokenType::RBrace);
  lexer.ConsumeNextToken();
  ASSERT_TRUE(lexer.ConsumeUntil(recovery_stops));
  EXPECT_EQ(lexer.PeekNextToken().type, TokenType::Semicolon);
  lexer.ConsumeNextToken();

  constexpr std::array eof_stops{TokenType::RBrace};
  EXPECT_FALSE(lexer.ConsumeUntil(eof_stops));
  EXPECT_EQ(lexer.PeekNextToken().type, TokenType::EndOfFile);
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

TEST(DiagnosticsFormatting, ThrowsCollectedDiagnostics) {
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

  bool raised = false;
  try {
    diagnostics.ThrowIfErrors();
  } catch (const DiagnosticError &error) {
    raised = true;
    EXPECT_EQ(TrimWhitespace(diagnostics.Format(error.Messages())),
              "<stdin>:1:2: error: Unknown var: localized\n"
              "<stdin>:1:2: error: Expected ';'");
  }
  EXPECT_TRUE(raised);
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

TEST(DiagnosticsFormatting, FormatsFatalDiagnostics) {
  SourceManager sources;
  SourceHandle source = sources.LoadSource("abc\n", "<stdin>");
  DiagnosticContext diagnostics(sources);
  SourceLocation location{
      .source_id = source.id,
      .begin = 1,
      .end = 2,
  };
  diagnostics.Error(location, errs::UnexpectedToken());

  bool raised = false;
  try {
    diagnostics.Fail(location, errs::ExpectedSemicolon());
  } catch (const DiagnosticError &error) {
    raised = true;
    EXPECT_EQ(error.what(), std::string_view("Expected ';'"));
    EXPECT_EQ(TrimWhitespace(diagnostics.Format(error.Messages())),
              "<stdin>:1:2: error: Unexpected token\n"
              "<stdin>:1:2: error: Expected ';'");
  }
  EXPECT_TRUE(raised);
}
