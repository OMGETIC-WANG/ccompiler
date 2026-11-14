#pragma once

#include "../Exception.h"
#include "../SourceManager.h"
#include "Macro.h"
#include "Scanner.h"

#include <deque>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

struct IRawLexer {
  virtual ~IRawLexer() = default;

  virtual Token EmitToken() = 0;
};

enum class PPCondBranch { Then, Else };

struct PPCond {
  bool is_active;
  PPCondBranch branch;
};

class CompileContext;

class RawLexer : public IRawLexer {
public:
  RawLexer(SourceHandle source, MacroContext &macro_context,
           SourceManager &source_manager, CompileContext &compile_context);

  Token EmitToken() override;

private:
  Token EmitTokenImpl();

  Token PeekScannerNextToken();

  bool IsInValidPPCond() const;

  IScanner *CurrScanner() {
    ASSERT(!_scanners.empty(), "No current scanner");
    return _scanners.back().get();
  }

  std::vector<std::unique_ptr<IScanner>> _scanners;
  SourceManager &_source_manager;
  MacroContext &_macro_context;
  CompileContext &_compile_context;
  std::vector<PPCond> _pp_cond_stack;
};

class FixedLexer : public IRawLexer {
public:
  FixedLexer(std::vector<Token> tokens)
      : _tokens(std::move(tokens)), _index(0) {}

  Token EmitToken() override {
    if (_index >= _tokens.size()) {
      return Token{TokenType::EndOfFile};
    } else {
      return _tokens[_index++];
    }
  }

private:
  std::vector<Token> _tokens;
  size_t _index;
};

// A wrapper of RawLexer&, support peeking tokens after current head
class Lexer {
public:
  Lexer(IRawLexer &lexer) : _lexer(lexer) {}

  Token PeekNextToken(size_t offset = 0) {
    if (offset < _parsed_tokens.size()) {
      return _parsed_tokens[offset];
    }
    while (offset >= _parsed_tokens.size()) {
      _parsed_tokens.push_back(_lexer.EmitToken());
    }
    return _parsed_tokens[offset];
  }

  Token ConsumeNextToken() {
    Token tok = PeekNextToken();
    _parsed_tokens.pop_front();
    _last_consumed_location = tok.location;
    ++_consumed_token_count;
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
    _last_consumed_location = token.location;
    ++_consumed_token_count;
  }

  SourceLocation GetLastConsumedLocation() const {
    return _last_consumed_location;
  }

  size_t GetConsumedTokenCount() const { return _consumed_token_count; }

  // Consume until any of <types>
  bool ConsumeUntil(std::span<const TokenType> types) {
    while (true) {
      Token tok = PeekNextToken();
      if (tok.type == TokenType::EndOfFile) {
        return false;
      }
      for (TokenType type : types) {
        if (tok.type == type) {
          return true;
        }
      }
      ConsumeToken(tok);
    }
  }

  bool ConsumeUntil(TokenType type) {
    return ConsumeUntil(std::span<const TokenType>(&type, 1));
  }

private:
  IRawLexer &_lexer;

  std::deque<Token> _parsed_tokens;
  SourceLocation _last_consumed_location;
  size_t _consumed_token_count = 0;
};
