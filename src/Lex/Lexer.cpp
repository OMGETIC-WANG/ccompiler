#include "Lexer.h"
#include <cassert>

static std::string_view ScanIncludeFile(Scanner &scanner) {
  Token tok = scanner.PeekNextTokenH();
  if (tok.type == TokenType::LiteralString) {
    scanner.ConsumeToken(tok);
    return tok.value;
  } else if (tok.type == TokenType::Lt) {
    scanner.ConsumeToken(tok);
    std::string_view filename;
    while (true) {
      tok = scanner.PeekNextTokenH();
      if (tok.type == TokenType::Gt) {
        scanner.ConsumeToken(tok);
        break;
      }
      if (tok.type == TokenType::NextLine) {
        scanner.ConsumeToken(tok);
        RAISE_ERROR("Unexpected end of line while scanning include file");
      } else {
        if (filename.empty()) {
          filename = tok.value;
        } else {
          filename = std::string_view(filename.data(),
                                      tok.value.data() + tok.value.size());
        }
      }
    }
    return filename;
  } else {
    RAISE_ERROR("Unexpected token while scanning include file: ", tok.value);
  }
}

static MacroDef ScanMacro(Scanner &scanner) {
  Token tok = scanner.PeekNextTokenH();
  ASSERT(tok.type == TokenType::Identifier,
         "Unexpected token while scanning macro: ", tok.value);
  MacroDef macro;
  macro.name = tok.value;
  scanner.ConsumeToken(tok);

  tok = scanner.PeekNextTokenH();
  if (tok.type == TokenType::LParen && tok.has_leading_space) {
    macro.has_arg = true;
    scanner.ConsumeToken(tok);
    while (true) {
      tok = scanner.PeekNextTokenH();
      if (tok.type == TokenType::RParen) {
        scanner.ConsumeToken(tok);
        break;
      } else {
        ASSERT(tok.type == TokenType::Identifier,
               "Unexpected token while scanning macro: ", tok.value);
        macro.params.push_back(tok.value);
        scanner.ConsumeToken(tok);
        tok = scanner.PeekNextTokenH();
        if (tok.type == TokenType::Comma) {
          scanner.ConsumeToken(tok);
        }
      }
    }
  }

  while (true) {
    tok = scanner.PeekNextTokenH();
    if (tok.type == TokenType::NextLine) {
      scanner.ConsumeToken(tok);
      break;
    } else {
      macro.body.push_back(tok);
      scanner.ConsumeToken(tok);
    }
  }

  return macro;
}

static std::vector<std::vector<Token>> ScanMacroArgs(RawLexer &lexer,
                                                     size_t arg_count) {
  Token lparen = lexer.PeekNextToken();
  ASSERT(lparen.type == TokenType::LParen, "Expected '(' after macro name");
  lexer.ConsumeToken(lparen);

  std::vector<std::vector<Token>> args(arg_count);
  size_t arg_idx = 0;
  int depth = 0;
  while (true) {
    Token tok = lexer.PeekNextToken();
    if (tok.type == TokenType::RParen) {
      if (depth == 0) {
        lexer.ConsumeToken(tok);
        break;
      } else {
        lexer.ConsumeToken(tok);
        depth--;
        args[arg_idx].push_back(tok);
      }
    } else if (tok.type == TokenType::LParen) {
      lexer.ConsumeToken(tok);
      depth++;
      args[arg_idx].push_back(tok);
    } else if (tok.type == TokenType::Comma && depth == 0) {
      lexer.ConsumeToken(tok);
      arg_idx++;
      args.emplace_back();
    } else if (tok.type == TokenType::EndOfFile) {
      RAISE_ERROR("Unexpected end of file");
    } else {
      lexer.ConsumeToken(tok);
      args[arg_idx].push_back(tok);
    }
  }
  return args;
}

RawLexer::RawLexer(std::string_view source, SourceManager &source_manager)
    : _source_manager(source_manager) {
  _scanners.push_back(std::make_unique<Scanner>(source));
}

Token RawLexer::PeekScannerNextToken() {
  Token tok = CurrScanner()->PeekNextToken();
  while (tok.type == TokenType::EndOfFile) {
    _scanners.pop_back();
    if (_scanners.empty()) {
      return Token{TokenType::EndOfFile};
    }
    tok = CurrScanner()->PeekNextToken();
  }
  return tok;
}

Token RawLexer::PeekNextToken() {
  Token tok = PeekScannerNextToken();
  if (tok.type != TokenType::Sharp) {
    if (auto *macro = _macro_context.FindMacro(tok.value)) {
      ConsumeToken(tok);
      if (macro->has_arg) {
        _scanners.push_back(std::make_unique<MacroScanner>(
            *macro, ScanMacroArgs(*this, macro->params.size()),
            _source_manager));
      } else {
        _scanners.push_back(std::make_unique<MacroScanner>(
            *macro, std::vector<std::vector<Token>>(), _source_manager));
      }
      return PeekNextToken();
    }
    return tok;
  }

  ASSERT(CurrScanner()->Is<Scanner>(), "Unexpected scanner for preprocessor");
  Scanner &curr_scanner = (Scanner &)*CurrScanner();

  curr_scanner.ConsumeToken(tok);
  Token preproc_tok =
      curr_scanner.PeekNextToken(); // preprocessor name must be in the same
                                    // scanner with sharp
  ASSERT(preproc_tok.type == TokenType::Identifier,
         "Unexpected preprocessor name: ", tok.value);

  if (preproc_tok.value == "include") {
    curr_scanner.ConsumeToken(preproc_tok);
    std::string_view header =
        _source_manager.LoadSourceFrom(ScanIncludeFile(curr_scanner));
    _scanners.push_back(std::make_unique<Scanner>(header));
  } else if (preproc_tok.value == "define") {
    curr_scanner.ConsumeToken(preproc_tok);
    _macro_context.AddMacro(ScanMacro(curr_scanner));
  }

  return PeekNextToken();
}

void RawLexer::ConsumeToken(Token token) {
  if (token.type != TokenType::EndOfFile) {
    CurrScanner()->ConsumeToken(token);
  }
}
