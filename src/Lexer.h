#pragma once

#include "Exception.h"
#include "Util/EnumRefl.h"
#include <deque>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

// clang-format off
#define PREPROCESSOR_TOKEN_VALUES(x)\
  x(Preprocessor)\
  x(PreprocessorSharp)\
  x(MacroDeclaration)\
  x(MacroContent)\
  x(IncludeFile)

#define TOKEN_TYPE_VALUES(x)\
  x(Unknown)\
  x(RawLLVMIR)\
  x(EndOfFile)\
  x(Identifier)\
  x(Keyword)\
  x(LiteralNum)\
  x(LiteralString)\
  x(LiteralChar)\
  x(Add)\
  x(Sub)\
  x(Div)\
  x(Mod)\
  x(LShift)\
  x(RShift)\
  x(BitAnd)\
  x(BitOr)\
  x(BitXor)\
  x(BitNot)\
  x(And)\
  x(Or)\
  x(Not)\
  x(Eq)\
  x(Ne)\
  x(Lt)\
  x(Gt)\
  x(Le)\
  x(Ge)\
  x(AddAdd)\
  x(SubSub)\
  x(Equal)\
  x(Star)\
  x(Dot)\
  x(Et)\
  x(Comma)\
  x(Semicolon)\
  x(Colon)\
  x(LParen)\
  x(RParen)\
  x(LBracket)\
  x(RBracket)\
  x(LBrace)\
  x(RBrace)\
  PREPROCESSOR_TOKEN_VALUES(x)
// clang-format on

MAKE_ENUM(TokenType, TOKEN_TYPE_VALUES)

REGISTER_ENUM(TokenType, TOKEN_TYPE_VALUES)

#pragma push_macro("__EXTRACT_AS_IF")
#define __EXTRACT_AS_IF(x)                                                     \
  if (type == TokenType::x)                                                    \
    return true;

inline bool IsPreprocessorToken(TokenType type) {
  PREPROCESSOR_TOKEN_VALUES(__EXTRACT_AS_IF)
  return false;
}

#pragma pop_macro("__EXTRACT_AS_IF")

constexpr inline std::string_view begin_raw_ir_label = "__llvm_ir_begin__";
constexpr inline std::string_view end_raw_ir_label = "__llvm_ir_end__";

struct Token {
  TokenType type;
  std::string_view value;

  bool operator==(const Token &) const = default;
};

inline bool IsPreprocessorToken(const Token &token) {
  return IsPreprocessorToken(token.type);
}

enum class MacroArgInsertType { Unknown, Direct, AsString };

struct MacroArgInsertion {
  int arg_index;
  MacroArgInsertType insert_type;
};

class MacroRecord {
public:
  static std::pair<std::string_view, MacroRecord>
  Parse(std::string_view decl, std::string_view content);

  std::string Expand(const std::vector<std::string_view> &args) const;

  size_t arg_count;

  std::vector<std::variant<std::string_view, MacroArgInsertion>> chunks;
};

enum class LexerStage {
  Normal,
  InsideMacro,
  InsideIncludeFile,
  IncludeFileDone
};

class Lexer {
public:
  Lexer(std::string_view source,
        std::shared_ptr<std::unordered_map<std::string_view, MacroRecord>>
            macros = nullptr)
      : _source(source), _macros(macros) {
    if (_macros == nullptr) {
      _macros =
          std::make_shared<std::unordered_map<std::string_view, MacroRecord>>();
    }
  }

  Token PeekNextToken();

  Token ConsumeNextToken();

  void ConsumeToken(Token token);

  std::string_view Rest() const { return _source.substr(_pos); }

private:
  auto &Macros() { return *_macros; }

  void InitMacroExpansionLexer(Token macro_identifier, MacroRecord &macro);

  LexerStage _stage = LexerStage::Normal;

  std::string_view _source;
  size_t _pos = 0;

  Token _prev_token = Token{.type = TokenType::Unknown, .value = {}};

  std::shared_ptr<std::unordered_map<std::string_view, MacroRecord>> _macros;

  std::string _sub_lexer_source;
  std::unique_ptr<Lexer> _sub_lexer;

  std::vector<Token> *_header_tokens = nullptr;
  size_t _header_token_index = 0;
  bool _header_cached = false;
  std::vector<Token> _header_tokens_cache;
  std::string _header_path;

  static std::unordered_map<std::string, std::vector<Token>> &_headers();
};

// A wrapper of Lexer&, fileter out all the preprocessor tokens
class FilteredLexer {
public:
  FilteredLexer(Lexer &lexer) : _lexer(lexer) {}

  Token PeekNextToken(size_t offset = 0) {
    if (offset < _parsed_tokens.size()) {
      return _parsed_tokens[offset];
    }
    while (offset >= _parsed_tokens.size()) {
      while (true) {
        Token tok = _lexer.PeekNextToken();
        _lexer.ConsumeToken(tok);
        if (IsPreprocessorToken(tok)) {
          continue;
        } else {
          _parsed_tokens.push_back(tok);
          break;
        }
      }
    }
    return _parsed_tokens[offset];
  }

  Token ConsumeNextToken() {
    Token tok = PeekNextToken();
    _parsed_tokens.pop_front();
    return tok;
  }

  void ConsumeTokens(size_t count = 1) {
    for (size_t i = 0; i < count; ++i) {
      ConsumeNextToken();
    }
  }

  void ConsumeToken(Token token) {
    ASSERT(token == PeekNextToken(), "Token mismatch while consuming");
    _parsed_tokens.pop_front();
  }

  std::string_view Rest() const { return _lexer.Rest(); }

private:
  Lexer &_lexer;

  std::deque<Token> _parsed_tokens;
};
