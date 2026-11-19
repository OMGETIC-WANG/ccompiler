#pragma once

#include "Lex/Lexer.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lexer_test {

struct ExpectedToken {
  TokenType type;
  std::optional<std::string> value;
  std::size_t line;
};

using AliasMap = std::unordered_map<std::string, TokenType>;

AliasMap ParseAliases(const std::vector<std::string> &lines,
                      const std::filesystem::path &path);

std::vector<ExpectedToken>
ParseExpectedTokens(const std::vector<std::string> &lines,
                    const AliasMap &aliases, const std::filesystem::path &path);

std::vector<std::filesystem::path>
ParseIncludePaths(std::span<const std::string_view> args);

void VerifyTokens(const std::vector<ExpectedToken> &expected,
                  const std::vector<std::pair<TokenType, std::string>> &actual,
                  const std::filesystem::path &expected_path);

} // namespace lexer_test

int RunLexerTest(const std::filesystem::path &source_path,
                 const std::filesystem::path &expected_path,
                 const std::filesystem::path &aliases_path,
                 std::span<const std::string_view> args);
