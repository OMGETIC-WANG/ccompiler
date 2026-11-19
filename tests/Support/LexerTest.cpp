#include "Support/LexerTest.h"

#include "AST/Context.h"
#include "AST/TypeContext.h"
#include "Lex/Lexer.h"
#include "Lex/Macro.h"
#include "SourceManager.h"
#include "Util/Diagnostic.h"
#include "Util/EnumRefl.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

class LexerTestError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct ExpectedToken {
  TokenType type;
  std::optional<std::string> value;
  std::size_t line;
};

using AliasMap = std::unordered_map<std::string, TokenType>;

std::string_view TrimWhitespace(std::string_view text) {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front()))) {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back()))) {
    text.remove_suffix(1);
  }
  return text;
}

std::vector<std::string> ReadLines(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    throw LexerTestError(
        std::format("Failed to open {}: {}", path.string(), "input file"));
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(std::move(line));
  }
  if (!input.eof()) {
    throw LexerTestError(std::format("Failed to read {} after line {}",
                                     path.string(), lines.size()));
  }
  return lines;
}

std::pair<std::string_view, std::string_view>
SplitFirstField(std::string_view text) {
  text = TrimWhitespace(text);
  const std::size_t separator = text.find_first_of(" \t\v\f\r\n");
  if (separator == std::string_view::npos) {
    return {text, {}};
  }
  return {text.substr(0, separator),
          TrimWhitespace(text.substr(separator + 1))};
}

[[noreturn]] void FailAt(const std::filesystem::path &path, std::size_t line,
                         std::string_view message) {
  throw LexerTestError(std::format("{}:{}: {}", path.string(), line, message));
}

TokenType ResolveTokenType(std::string_view name, const AliasMap &aliases,
                           const std::filesystem::path &path,
                           std::size_t line) {
  if (EnumRefl<TokenType>::IsValidEnumName(name)) {
    return EnumRefl<TokenType>::GetValue(name);
  }
  if (auto found = aliases.find(std::string(name)); found != aliases.end()) {
    return found->second;
  }
  FailAt(path, line, std::format("unknown token type or alias '{}'", name));
}

AliasMap ParseAliases(const std::vector<std::string> &lines,
                      const std::filesystem::path &path) {
  AliasMap aliases;
  std::unordered_set<TokenType> aliased_types;

  for (std::size_t index = 0; index < lines.size(); ++index) {
    const std::size_t line = index + 1;
    const std::string_view trimmed = TrimWhitespace(lines[index]);
    if (trimmed.empty()) {
      continue;
    }

    const auto [type_name, alias] = SplitFirstField(trimmed);
    if (type_name.empty() || alias.empty()) {
      FailAt(path, line, "expected '<token_type> <token_type_alias>'");
    }
    if (alias.find_first_of(" \t\v\f\r\n") != std::string_view::npos) {
      FailAt(path, line, "token type alias cannot contain whitespace");
    }
    if (!EnumRefl<TokenType>::IsValidEnumName(type_name)) {
      FailAt(path, line,
             std::format("unknown TokenType full name '{}'", type_name));
    }

    const TokenType type = EnumRefl<TokenType>::GetValue(type_name);
    if (EnumRefl<TokenType>::IsValidEnumName(alias)) {
      FailAt(path, line,
             std::format("alias '{}' shadows a TokenType full name", alias));
    }
    if (aliases.contains(std::string(alias))) {
      FailAt(path, line, std::format("duplicate alias '{}'", alias));
    }
    if (!aliased_types.insert(type).second) {
      FailAt(path, line,
             std::format("duplicate alias for TokenType '{}'", type_name));
    }
    aliases.emplace(std::string(alias), type);
  }

  return aliases;
}

AliasMap LoadAliases(const std::filesystem::path &path) {
  return ParseAliases(ReadLines(path), path);
}

int HexDigitValue(char digit) {
  if (digit >= '0' && digit <= '9') {
    return digit - '0';
  }
  if (digit >= 'a' && digit <= 'f') {
    return digit - 'a' + 10;
  }
  if (digit >= 'A' && digit <= 'F') {
    return digit - 'A' + 10;
  }
  return -1;
}

std::string ParseQuotedValue(std::string_view text,
                             const std::filesystem::path &path,
                             std::size_t line) {
  if (text.empty() || text.front() != '"') {
    FailAt(path, line, "quoted token value must start with '\"'");
  }

  std::string value;
  for (std::size_t index = 1; index < text.size(); ++index) {
    const char character = text[index];
    if (character == '"') {
      if (!TrimWhitespace(text.substr(index + 1)).empty()) {
        FailAt(path, line, "unexpected text after quoted token value");
      }
      return value;
    }
    if (character != '\\') {
      value.push_back(character);
      continue;
    }

    if (++index == text.size()) {
      FailAt(path, line, "unterminated escape in quoted token value");
    }
    const char escaped = text[index];
    switch (escaped) {
    case '\\':
      value.push_back('\\');
      break;
    case '"':
      value.push_back('"');
      break;
    case 'n':
      value.push_back('\n');
      break;
    case 'r':
      value.push_back('\r');
      break;
    case 't':
      value.push_back('\t');
      break;
    case '0':
      value.push_back('\0');
      break;
    case 'x': {
      if (index + 2 >= text.size()) {
        FailAt(path, line, "expected two hexadecimal digits after '\\x'");
      }
      const int high = HexDigitValue(text[index + 1]);
      const int low = HexDigitValue(text[index + 2]);
      if (high < 0 || low < 0) {
        FailAt(path, line, "expected two hexadecimal digits after '\\x'");
      }
      value.push_back(static_cast<char>((high << 4) | low));
      index += 2;
      break;
    }
    default:
      FailAt(path, line,
             std::format("unknown escape sequence '\\{}'", escaped));
    }
  }

  FailAt(path, line, "unterminated quoted token value");
}

std::optional<std::string> ParseTokenValue(std::string_view text,
                                           const std::filesystem::path &path,
                                           std::size_t line) {
  if (text.empty()) {
    return std::nullopt;
  }
  if (text.front() == '"') {
    return ParseQuotedValue(text, path, line);
  }
  if (text.find_first_of(" \t\v\f\r\n") != std::string_view::npos) {
    FailAt(path, line, "token values containing whitespace must be quoted");
  }
  if (text.find_first_of("\\\"") != std::string_view::npos) {
    FailAt(path, line,
           "token values containing escapes or quotes must be quoted");
  }
  return std::string(text);
}

std::vector<std::filesystem::path>
ParseIncludePaths(std::span<const std::string_view> args) {
  std::vector<std::filesystem::path> include_paths;
  for (std::size_t index = 0; index < args.size(); ++index) {
    if (args[index] != "-I") {
      throw LexerTestError(
          std::format("unknown lexer test argument '{}'", args[index]));
    }
    if (++index == args.size()) {
      throw LexerTestError("missing directory after '-I'");
    }
    include_paths.emplace_back(args[index]);
  }
  return include_paths;
}

std::vector<ExpectedToken>
ParseExpectedTokens(const std::vector<std::string> &lines,
                    const AliasMap &aliases,
                    const std::filesystem::path &path) {
  std::vector<ExpectedToken> expected;
  bool saw_eof = false;

  for (std::size_t index = 0; index < lines.size(); ++index) {
    const std::size_t line = index + 1;
    const std::string_view trimmed = TrimWhitespace(lines[index]);
    if (trimmed.empty()) {
      continue;
    }
    if (saw_eof) {
      FailAt(path, line, "EndOfFile must be the last token");
    }

    const auto [type_name, value_text] = SplitFirstField(trimmed);
    const TokenType type = ResolveTokenType(type_name, aliases, path, line);
    expected.push_back({
        .type = type,
        .value = ParseTokenValue(value_text, path, line),
        .line = line,
    });
    saw_eof = type == TokenType::EndOfFile;
  }

  if (!saw_eof) {
    throw LexerTestError(
        std::format("{}: missing EndOfFile token", path.string()));
  }
  return expected;
}

std::vector<ExpectedToken> LoadExpectedTokens(const std::filesystem::path &path,
                                              const AliasMap &aliases) {
  return ParseExpectedTokens(ReadLines(path), aliases, path);
}

std::string EscapeTokenValue(std::string_view value) {
  std::string escaped;
  escaped.push_back('"');
  for (const char character : value) {
    switch (character) {
    case '\\':
      escaped += "\\\\";
      break;
    case '"':
      escaped += "\\\"";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    case '\0':
      escaped += "\\0";
      break;
    default:
      if (std::isprint(static_cast<unsigned char>(character))) {
        escaped.push_back(character);
      } else {
        escaped +=
            std::format("\\x{:02x}", static_cast<unsigned char>(character));
      }
    }
  }
  escaped.push_back('"');
  return escaped;
}

std::string FormatActualToken(TokenType type, std::string_view value) {
  return std::format("{} {}", EnumRefl<TokenType>::GetName(type),
                     EscapeTokenValue(value));
}

void VerifyTokens(const std::vector<ExpectedToken> &expected,
                  const std::vector<std::pair<TokenType, std::string>> &actual,
                  const std::filesystem::path &expected_path) {
  const std::size_t common =
      std::min(expected.size(), static_cast<std::size_t>(actual.size()));
  for (std::size_t index = 0; index < common; ++index) {
    const ExpectedToken &expected_token = expected[index];
    const auto &[actual_type, actual_value] = actual[index];
    if (expected_token.type != actual_type) {
      FailAt(expected_path, expected_token.line,
             std::format("token {} type mismatch: expected {}, got {}", index,
                         EnumRefl<TokenType>::GetName(expected_token.type),
                         EnumRefl<TokenType>::GetName(actual_type)));
    }
    if (expected_token.value && *expected_token.value != actual_value) {
      FailAt(expected_path, expected_token.line,
             std::format("token {} value mismatch: expected {}, got {}", index,
                         EscapeTokenValue(*expected_token.value),
                         EscapeTokenValue(actual_value)));
    }
  }

  if (actual.size() < expected.size()) {
    const ExpectedToken &missing = expected[actual.size()];
    FailAt(expected_path, missing.line,
           std::format("missing token {}: expected {}", actual.size(),
                       EnumRefl<TokenType>::GetName(missing.type)));
  }
  if (actual.size() > expected.size()) {
    const auto &[type, value] = actual[expected.size()];
    FailAt(expected_path, expected.back().line,
           std::format("unexpected token {} after EndOfFile: {}",
                       expected.size(), FormatActualToken(type, value)));
  }
}

std::vector<std::pair<TokenType, std::string>>
LexSource(const std::filesystem::path &source_path,
          std::span<const std::filesystem::path> include_paths) {
  SourceManager sources;
  for (const std::filesystem::path &include_path : include_paths) {
    sources.AddIncludePath(include_path);
  }
  const SourceHandle source = sources.LoadSourceFrom(source_path.string());
  DiagnosticContext diagnostics(sources);
  MacroContext macros;
  TypeContext types;
  CompileContext context(types, sources, diagnostics);
  RawLexer raw_lexer(source, macros, sources, context);

  std::vector<std::pair<TokenType, std::string>> tokens;
  while (true) {
    const Token token = raw_lexer.EmitToken();
    tokens.emplace_back(token.type, std::string(sources.GetText(token)));
    if (token.type == TokenType::EndOfFile) {
      diagnostics.ThrowIfErrors();
      return tokens;
    }
  }
}

template <typename Action>
bool ExpectLexerTestError(std::string_view expected_message, Action action) {
  try {
    action();
  } catch (const LexerTestError &error) {
    return std::string_view(error.what()).find(expected_message) !=
           std::string_view::npos;
  }
  return false;
}

} // namespace

int RunLexerTest(const std::filesystem::path &source_path,
                 const std::filesystem::path &expected_path,
                 const std::filesystem::path &aliases_path,
                 std::span<const std::string_view> args) {
  const AliasMap aliases = LoadAliases(aliases_path);
  const std::vector<ExpectedToken> expected =
      LoadExpectedTokens(expected_path, aliases);
  const std::vector<std::filesystem::path> include_paths =
      ParseIncludePaths(args);
  const auto actual = LexSource(source_path, include_paths);
  VerifyTokens(expected, actual, expected_path);

  std::cout << "PASS " << source_path << '\n';
  return 0;
}

int RunLexerSelfTest() {
  const std::filesystem::path aliases_path = "<aliases>";
  const std::filesystem::path tokens_path = "<tokens>";

  try {
    const AliasMap aliases = ParseAliases(
        {
            "KwInt int",
            "Semicolon ;",
        },
        aliases_path);
    const std::vector<ExpectedToken> expected = ParseExpectedTokens(
        {
            "KwInt 123",
            "; ;",
            "Identifier \"hello\\tworld\"",
            "EndOfFile",
            "",
        },
        aliases, tokens_path);
    if (expected.size() != 4 || expected[0].type != TokenType::KwInt ||
        expected[0].value != "123" ||
        expected[1].type != TokenType::Semicolon || expected[1].value != ";" ||
        expected[2].type != TokenType::Identifier ||
        expected[2].value != "hello\tworld" ||
        expected[3].type != TokenType::EndOfFile ||
        expected[3].value.has_value()) {
      std::cerr << "Lexer fixture parser did not preserve token descriptions\n";
      return 1;
    }
  } catch (const std::exception &error) {
    std::cerr << "Lexer fixture parser rejected valid input: " << error.what()
              << '\n';
    return 1;
  }

  const AliasMap aliases = ParseAliases(
      {
          "KwInt int",
          "Semicolon ;",
      },
      aliases_path);

  try {
    const std::vector<std::string_view> args{"-I", "first", "-I", "second"};
    const std::vector<std::filesystem::path> include_paths =
        ParseIncludePaths(args);
    const std::vector<std::string_view> no_args;
    if (include_paths.size() != 2 || include_paths[0] != "first" ||
        include_paths[1] != "second" || !ParseIncludePaths(no_args).empty()) {
      std::cerr << "Lexer include argument parsing returned wrong paths\n";
      return 1;
    }
  } catch (const std::exception &error) {
    std::cerr << "Lexer include argument parser rejected valid input: "
              << error.what() << '\n';
    return 1;
  }

  struct ErrorCase {
    std::string_view message;
    bool matches;
  };
  const std::vector<ErrorCase> cases = {
      {
          .message = "unknown lexer test argument",
          .matches = ExpectLexerTestError(
              "unknown lexer test argument",
              [] {
                const std::vector<std::string_view> args{"--unknown"};
                ParseIncludePaths(args);
              }),
      },
      {
          .message = "missing directory after '-I'",
          .matches = ExpectLexerTestError(
              "missing directory after '-I'",
              [] {
                const std::vector<std::string_view> args{"-I"};
                ParseIncludePaths(args);
              }),
      },
      {
          .message = "unknown TokenType full name",
          .matches = ExpectLexerTestError(
              "unknown TokenType full name",
              [&] { ParseAliases({"Bogus alias"}, aliases_path); }),
      },
      {
          .message = "duplicate alias",
          .matches = ExpectLexerTestError("duplicate alias",
                                          [&] {
                                            ParseAliases(
                                                {
                                                    "KwInt value",
                                                    "KwChar value",
                                                },
                                                aliases_path);
                                          }),
      },
      {
          .message = "duplicate alias for TokenType",
          .matches = ExpectLexerTestError("duplicate alias for TokenType",
                                          [&] {
                                            ParseAliases(
                                                {
                                                    "KwInt first",
                                                    "KwInt second",
                                                },
                                                aliases_path);
                                          }),
      },
      {
          .message = "unknown token type or alias",
          .matches = ExpectLexerTestError(
              "unknown token type or alias",
              [&] { ParseExpectedTokens({"missing"}, aliases, tokens_path); }),
      },
      {
          .message = "missing EndOfFile",
          .matches = ExpectLexerTestError("missing EndOfFile",
                                          [&] {
                                            ParseExpectedTokens(
                                                {
                                                    "int 1",
                                                },
                                                aliases, tokens_path);
                                          }),
      },
      {
          .message = "EndOfFile must be the last token",
          .matches = ExpectLexerTestError("EndOfFile must be the last token",
                                          [&] {
                                            ParseExpectedTokens(
                                                {
                                                    "EndOfFile",
                                                    "int 1",
                                                },
                                                aliases, tokens_path);
                                          }),
      },
      {
          .message = "must be quoted",
          .matches = ExpectLexerTestError("must be quoted",
                                          [&] {
                                            ParseExpectedTokens(
                                                {
                                                    "Identifier hello world",
                                                    "EndOfFile",
                                                },
                                                aliases, tokens_path);
                                          }),
      },
      {
          .message = "unterminated quoted token value",
          .matches = ExpectLexerTestError("unterminated quoted token value",
                                          [&] {
                                            ParseExpectedTokens(
                                                {
                                                    "Identifier \"unterminated",
                                                    "EndOfFile",
                                                },
                                                aliases, tokens_path);
                                          }),
      },
      {
          .message = "unknown escape sequence",
          .matches = ExpectLexerTestError("unknown escape sequence",
                                          [&] {
                                            ParseExpectedTokens(
                                                {
                                                    "Identifier \"bad\\q\"",
                                                    "EndOfFile",
                                                },
                                                aliases, tokens_path);
                                          }),
      },
      {
          .message = "type mismatch",
          .matches = ExpectLexerTestError(
              "type mismatch",
              [&] {
                const std::vector<ExpectedToken> expected = ParseExpectedTokens(
                    {
                        "int 1",
                        "EndOfFile",
                    },
                    aliases, tokens_path);
                VerifyTokens(expected,
                             {
                                 {TokenType::Identifier, "x"},
                                 {TokenType::EndOfFile, ""},
                             },
                             tokens_path);
              }),
      },
      {
          .message = "value mismatch",
          .matches = ExpectLexerTestError(
              "value mismatch",
              [&] {
                const std::vector<ExpectedToken> expected = ParseExpectedTokens(
                    {
                        "int 1",
                        "EndOfFile",
                    },
                    aliases, tokens_path);
                VerifyTokens(expected,
                             {
                                 {TokenType::KwInt, "2"},
                                 {TokenType::EndOfFile, ""},
                             },
                             tokens_path);
              }),
      },
      {
          .message = "missing token",
          .matches =
              ExpectLexerTestError("missing token",
                                   [&] {
                                     const std::vector<ExpectedToken> expected =
                                         ParseExpectedTokens(
                                             {
                                                 "EndOfFile",
                                             },
                                             aliases, tokens_path);
                                     VerifyTokens(expected, {}, tokens_path);
                                   }),
      },
      {
          .message = "unexpected token",
          .matches = ExpectLexerTestError(
              "unexpected token",
              [&] {
                const std::vector<ExpectedToken> expected = ParseExpectedTokens(
                    {
                        "EndOfFile",
                    },
                    aliases, tokens_path);
                VerifyTokens(expected,
                             {
                                 {TokenType::EndOfFile, ""},
                                 {TokenType::Identifier, "extra"},
                             },
                             tokens_path);
              }),
      },
  };

  for (const ErrorCase &test_case : cases) {
    if (!test_case.matches) {
      std::cerr << "Lexer fixture parser accepted invalid input or reported "
                   "the wrong error for "
                << test_case.message << '\n';
      return 1;
    }
  }

  std::cout << "PASS lexer fixture parsing\n";
  return 0;
}
