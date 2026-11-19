#include "Support/LexerTest.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view kAliasError = "unknown TokenType full name";

lexer_test::AliasMap MakeAliases() {
  return lexer_test::ParseAliases(
      {
          "KwInt int",
          "Semicolon ;",
      },
      "<aliases>");
}

template <typename Action>
void ExpectLexerError(std::string_view expected_message, Action action) {
  bool raised = false;
  try {
    action();
  } catch (const std::exception &error) {
    raised = true;
    EXPECT_NE(std::string_view(error.what()).find(expected_message),
              std::string_view::npos)
        << error.what();
  }
  EXPECT_TRUE(raised);
}

} // namespace

TEST(LexerFixture, ParsesAliasesAndTokens) {
  const auto aliases = MakeAliases();
  const auto expected = lexer_test::ParseExpectedTokens(
      {
          "KwInt 123",
          "; ;",
          "Identifier \"hello\\tworld\"",
          "EndOfFile",
          "",
      },
      aliases, "<tokens>");

  ASSERT_EQ(expected.size(), 4);
  EXPECT_EQ(expected[0].type, TokenType::KwInt);
  EXPECT_EQ(expected[0].value, "123");
  EXPECT_EQ(expected[1].type, TokenType::Semicolon);
  EXPECT_EQ(expected[1].value, ";");
  EXPECT_EQ(expected[2].type, TokenType::Identifier);
  EXPECT_EQ(expected[2].value, "hello\tworld");
  EXPECT_EQ(expected[3].type, TokenType::EndOfFile);
  EXPECT_FALSE(expected[3].value.has_value());
}

TEST(LexerFixture, ParsesIncludePaths) {
  const std::vector<std::string_view> args{"-I", "first", "-I", "second"};
  const auto include_paths = lexer_test::ParseIncludePaths(args);
  const std::vector<std::string_view> no_args;

  ASSERT_EQ(include_paths.size(), 2);
  EXPECT_EQ(include_paths[0], "first");
  EXPECT_EQ(include_paths[1], "second");
  EXPECT_TRUE(lexer_test::ParseIncludePaths(no_args).empty());
}

TEST(LexerFixture, RejectsUnknownArgument) {
  ExpectLexerError("unknown lexer test argument", [] {
    const std::vector<std::string_view> args{"--unknown"};
    lexer_test::ParseIncludePaths(args);
  });
}

TEST(LexerFixture, RequiresIncludeDirectory) {
  ExpectLexerError("missing directory after '-I'", [] {
    const std::vector<std::string_view> args{"-I"};
    lexer_test::ParseIncludePaths(args);
  });
}

TEST(LexerFixture, RejectsUnknownTokenName) {
  ExpectLexerError(kAliasError, [] {
    lexer_test::ParseAliases({"Bogus alias"}, "<aliases>");
  });
}

TEST(LexerFixture, RejectsDuplicateAlias) {
  ExpectLexerError("duplicate alias", [] {
    lexer_test::ParseAliases({"KwInt value", "KwChar value"}, "<aliases>");
  });
}

TEST(LexerFixture, RejectsDuplicateAliasForType) {
  ExpectLexerError("duplicate alias for TokenType", [] {
    lexer_test::ParseAliases({"KwInt first", "KwInt second"}, "<aliases>");
  });
}

TEST(LexerFixture, RejectsUnknownExpectedToken) {
  const auto aliases = MakeAliases();
  ExpectLexerError("unknown token type or alias", [&] {
    lexer_test::ParseExpectedTokens({"missing"}, aliases, "<tokens>");
  });
}

TEST(LexerFixture, RequiresEndOfFile) {
  const auto aliases = MakeAliases();
  ExpectLexerError("missing EndOfFile", [&] {
    lexer_test::ParseExpectedTokens({"int 1"}, aliases, "<tokens>");
  });
}

TEST(LexerFixture, RejectsTokensAfterEndOfFile) {
  const auto aliases = MakeAliases();
  ExpectLexerError("EndOfFile must be the last token", [&] {
    lexer_test::ParseExpectedTokens({"EndOfFile", "int 1"}, aliases,
                                    "<tokens>");
  });
}

TEST(LexerFixture, RequiresQuotesForWhitespace) {
  const auto aliases = MakeAliases();
  ExpectLexerError("must be quoted", [&] {
    lexer_test::ParseExpectedTokens({"Identifier hello world", "EndOfFile"},
                                    aliases, "<tokens>");
  });
}

TEST(LexerFixture, RejectsUnterminatedQuotedValue) {
  const auto aliases = MakeAliases();
  ExpectLexerError("unterminated quoted token value", [&] {
    lexer_test::ParseExpectedTokens({"Identifier \"unterminated", "EndOfFile"},
                                    aliases, "<tokens>");
  });
}

TEST(LexerFixture, RejectsUnknownEscape) {
  const auto aliases = MakeAliases();
  ExpectLexerError("unknown escape sequence", [&] {
    lexer_test::ParseExpectedTokens({"Identifier \"bad\\q\"", "EndOfFile"},
                                    aliases, "<tokens>");
  });
}

TEST(LexerFixture, ReportsTypeMismatch) {
  const auto aliases = MakeAliases();
  const auto expected = lexer_test::ParseExpectedTokens({"int 1", "EndOfFile"},
                                                        aliases, "<tokens>");
  ExpectLexerError("type mismatch", [&] {
    lexer_test::VerifyTokens(expected,
                             {
                                 {TokenType::Identifier, "x"},
                                 {TokenType::EndOfFile, ""},
                             },
                             "<tokens>");
  });
}

TEST(LexerFixture, ReportsValueMismatch) {
  const auto aliases = MakeAliases();
  const auto expected = lexer_test::ParseExpectedTokens({"int 1", "EndOfFile"},
                                                        aliases, "<tokens>");
  ExpectLexerError("value mismatch", [&] {
    lexer_test::VerifyTokens(expected,
                             {
                                 {TokenType::KwInt, "2"},
                                 {TokenType::EndOfFile, ""},
                             },
                             "<tokens>");
  });
}

TEST(LexerFixture, ReportsMissingToken) {
  const auto aliases = MakeAliases();
  const auto expected =
      lexer_test::ParseExpectedTokens({"EndOfFile"}, aliases, "<tokens>");
  ExpectLexerError("missing token", [&] {
    lexer_test::VerifyTokens(
        expected, std::vector<std::pair<TokenType, std::string>>{}, "<tokens>");
  });
}

TEST(LexerFixture, ReportsUnexpectedToken) {
  const auto aliases = MakeAliases();
  const auto expected =
      lexer_test::ParseExpectedTokens({"EndOfFile"}, aliases, "<tokens>");
  ExpectLexerError("unexpected token", [&] {
    lexer_test::VerifyTokens(expected,
                             {
                                 {TokenType::EndOfFile, ""},
                                 {TokenType::Identifier, "extra"},
                             },
                             "<tokens>");
  });
}
