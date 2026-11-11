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
           SourceManager &source_manager, CompileContext &compile_context)
      : _source_manager(source_manager), _macro_context(macro_context),
        _compile_context(compile_context) {
    _scanners.push_back(std::make_unique<Scanner>(std::move(source)));
  }

  Token EmitToken() override;

private:
  Token EmitTokenImpl();

  Token PeekScannerNextToken();

  bool IsInValidPPCond() const;

  IScanner *CurrScanner() {
    assert(!_scanners.empty());
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
  IRawLexer &_lexer;

  std::deque<Token> _parsed_tokens;
};
