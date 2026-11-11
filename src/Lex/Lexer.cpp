#include "Lexer.h"
#include "../AST/AST.h"
#include "../AST/Context.h"
#include "../AST/Expr.h"
#include "PPEval.h"
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

static std::vector<std::vector<Token>> ScanMacroArgs(IRawLexer &lexer,
                                                     size_t arg_count) {
  Token lparen = lexer.EmitToken();
  ASSERT(lparen.type == TokenType::LParen, "Expected '(' after macro name");

  std::vector<std::vector<Token>> args(arg_count);
  size_t arg_idx = 0;
  int depth = 0;
  while (true) {
    Token tok = lexer.EmitToken();
    if (tok.type == TokenType::RParen) {
      if (depth == 0) {
        break;
      } else {
        depth--;
        args[arg_idx].push_back(tok);
      }
    } else if (tok.type == TokenType::LParen) {
      depth++;
      args[arg_idx].push_back(tok);
    } else if (tok.type == TokenType::Comma && depth == 0) {
      arg_idx++;
      args.emplace_back();
    } else if (tok.type == TokenType::EndOfFile) {
      RAISE_ERROR("Unexpected end of file");
    } else {
      args[arg_idx].push_back(tok);
    }
  }
  return args;
}

class MacroCondLexer : public IRawLexer {
public:
  MacroCondLexer(Scanner *scanner, MacroContext &macro_context,
                 SourceManager &source_manager)
      : _source_manager(source_manager), _macro_context(macro_context) {
    _scanners.emplace_back(scanner, false);
  }

  Token EmitToken() override {
    Token tok = PeekScannerNextToken();
    ASSERT(tok.type != TokenType::Sharp, "Unexpected '#'");

    if (_scanners.size() == 1 && tok.value == "defined") {
      std::string_view target_macro_name;
      Scanner &scanner = *(Scanner *)_scanners.back().get();
      scanner.ConsumeToken(tok);
      tok = scanner.PeekNextTokenH();
      if (tok.type == TokenType::LParen) {
        scanner.ConsumeToken(tok);
        tok = scanner.PeekNextTokenH();
        ASSERT(tok.type == TokenType::Identifier,
               "Unexpected token after defined: ", tok.value);
        scanner.ConsumeToken(tok);
        target_macro_name = tok.value;
        tok = scanner.PeekNextTokenH();
        ASSERT(tok.type == TokenType::RParen, "Expected ')' after defined");
        scanner.ConsumeToken(tok);
      } else if (tok.type == TokenType::Identifier) {
        scanner.ConsumeToken(tok);
        target_macro_name = tok.value;
      } else {
        RAISE_ERROR("Unexpected token after defined: ", tok.value);
      }
      return Token{TokenType::LiteralNum,
                   _macro_context.FindMacro(target_macro_name) ? "1" : "0"};
    }
    if (tok.type == TokenType::Identifier) {
      if (auto *macro = _macro_context.FindMacro(tok.value)) {
        CurrScanner()->ConsumeToken(tok);
        if (macro->has_arg) {
          _scanners.emplace_back(
              new MacroScanner(*macro,
                               ScanMacroArgs(*this, macro->params.size()),
                               _source_manager),
              true);
        } else {
          _scanners.emplace_back(
              new MacroScanner(*macro, std::vector<std::vector<Token>>(),
                               _source_manager),
              true);
        }
        return EmitToken();
      } else {
        CurrScanner()->ConsumeToken(tok);
        return Token{TokenType::LiteralNum, "0"};
      }
    }
    if (tok.type == TokenType::Sharp || tok.type == TokenType::SharpSharp) {
      RAISE_ERROR("Unexpected preprocessor token in macro condition: ",
                  tok.value);
    }
    if (tok.type != TokenType::EndOfFile) {
      CurrScanner()->ConsumeToken(tok);
    }
    return tok;
  }

private:
  Token PeekScannerNextToken() {
    Token tok = _scanners.size() == 1
                    ? ((Scanner *)_scanners[0].get())->PeekNextTokenH()
                    : _scanners.back()->PeekNextToken();
    while (tok.type == TokenType::EndOfFile) {
      _scanners.pop_back();
      if (_scanners.empty()) {
        return Token{TokenType::EndOfFile};
      }
      tok = _scanners.size() == 1
                ? ((Scanner *)_scanners[0].get())->PeekNextTokenH()
                : _scanners.back()->PeekNextToken();
    }
    if (_scanners.size() == 1) {
      return tok.type == TokenType::NextLine ? Token{TokenType::EndOfFile}
                                             : tok;
    } else {
      return tok;
    }
  }

  IScanner *CurrScanner() {
    assert(!_scanners.empty());
    return _scanners.back().get();
  }

  std::vector<MayOwnPtr<IScanner>> _scanners;
  SourceManager &_source_manager;
  MacroContext &_macro_context;
};

std::unique_ptr<ASTNode> ScanMacroCond(Scanner *scanner,
                                       MacroContext &macro_context,
                                       SourceManager &source_manager,
                                       CompileContext &compile_context) {
  MacroCondLexer raw_lexer(scanner, macro_context, source_manager);
  Lexer lexer(raw_lexer);
  return ParseExpr(lexer, compile_context);
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

Token RawLexer::EmitToken() {
  Token tok = EmitTokenImpl();
  while (!IsInValidPPCond() && tok.type != TokenType::EndOfFile) {
    tok = EmitTokenImpl();
  }
  return tok;
}

static bool IsPreprocessor(std::string_view name) {
  return name == "include" || name == "define" || name == "if" ||
         name == "else" || name == "endif";
}

Token RawLexer::EmitTokenImpl() {
  Token tok = PeekScannerNextToken();
  if (tok.type != TokenType::Sharp) {
    if (auto *macro = _macro_context.FindMacro(tok.value)) {
      CurrScanner()->ConsumeToken(tok);
      if (macro->has_arg) {
        _scanners.push_back(std::make_unique<MacroScanner>(
            *macro, ScanMacroArgs(*this, macro->params.size()),
            _source_manager));
      } else {
        _scanners.push_back(std::make_unique<MacroScanner>(
            *macro, std::vector<std::vector<Token>>(), _source_manager));
      }
      return EmitToken();
    }
    if (tok.type != TokenType::EndOfFile) {
      CurrScanner()->ConsumeToken(tok);
    }
    return tok;
  }

  ASSERT(CurrScanner()->Is<Scanner>(), "Unexpected scanner for preprocessor");
  Scanner &curr_scanner = (Scanner &)*CurrScanner();

  curr_scanner.ConsumeToken(tok);
  Token preproc_tok =
      curr_scanner.PeekNextToken(); // preprocessor name must be in the same
                                    // scanner with sharp
  ASSERT(IsPreprocessor(preproc_tok.value),
         "Unexpected preprocessor name: ", tok.value);

  if (preproc_tok.value == "include") {
    curr_scanner.ConsumeToken(preproc_tok);
    std::string_view header =
        _source_manager.LoadSourceFrom(ScanIncludeFile(curr_scanner));
    _scanners.push_back(std::make_unique<Scanner>(header));
  } else if (preproc_tok.value == "define") {
    curr_scanner.ConsumeToken(preproc_tok);
    _macro_context.AddMacro(ScanMacro(curr_scanner));
  } else if (preproc_tok.value == "if") {
    curr_scanner.ConsumeToken(preproc_tok);
    std::unique_ptr<ASTNode> cond = ScanMacroCond(
        &curr_scanner, _macro_context, _source_manager, _compile_context);
    uint64_t cond_value = EvalMacroCond(cond.get());
    _pp_cond_stack.push_back(
        {.is_active = cond_value != 0, .branch = PPCondBranch::Then});
  } else if (preproc_tok.value == "else") {
    curr_scanner.ConsumeToken(preproc_tok);
    ASSERT(!_pp_cond_stack.empty(), "Unexpected #else");
    PPCond &cond = _pp_cond_stack.back();
    cond.branch = PPCondBranch::Else;
  } else if (preproc_tok.value == "endif") {
    curr_scanner.ConsumeToken(preproc_tok);
    ASSERT(!_pp_cond_stack.empty(), "Unexpected #endif");
    _pp_cond_stack.pop_back();
  } else {
    RAISE_ERROR("Unexpected preprocessor name: ", preproc_tok.value);
  }

  return EmitToken();
}

bool RawLexer::IsInValidPPCond() const {
  if (_pp_cond_stack.empty())
    return true;
  PPCond cond = _pp_cond_stack.back();
  if (cond.is_active) {
    return cond.branch == PPCondBranch::Then;
  } else {
    return cond.branch == PPCondBranch::Else;
  }
}
