#pragma once

#include "../Exception.h"
#include "../SourceManager.h"
#include "Macro.h"
#include "Scanner.h"

#include <deque>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

class RawLexer {
public:
  RawLexer(std::string_view source, SourceManager &source_manager);

  Token PeekNextToken();

  void ConsumeToken(Token token);

private:
  Token PeekScannerNextToken();

  IScanner *CurrScanner() {
    assert(!_scanners.empty());
    return _scanners.back().get();
  }

  std::vector<std::unique_ptr<IScanner>> _scanners;
  SourceManager &_source_manager;
  MacroContext _macro_context;
};

// A wrapper of RawLexer&, support peeking tokens after current head
class Lexer {
public:
  Lexer(RawLexer &lexer) : _lexer(lexer) {}

  Token PeekNextToken(size_t offset = 0) {
    if (offset < _parsed_tokens.size()) {
      return _parsed_tokens[offset];
    }
    while (offset >= _parsed_tokens.size()) {
      Token tok = _lexer.PeekNextToken();
      _lexer.ConsumeToken(tok);
      _parsed_tokens.push_back(tok);
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

private:
  RawLexer &_lexer;

  std::deque<Token> _parsed_tokens;
};
