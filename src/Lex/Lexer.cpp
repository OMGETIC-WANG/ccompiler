#include "Lexer.h"
#include "../AST/AST.h"
#include "../AST/Context.h"
#include "../AST/Expr.h"
#include "PPEval.h"

#include <string>

RawLexer::RawLexer(SourceHandle source, MacroContext &macro_context,
                   SourceManager &source_manager,
                   CompileContext &compile_context)
    : _source_manager(source_manager), _macro_context(macro_context),
      _compile_context(compile_context) {
  _scanners.push_back(std::make_unique<Scanner>(std::move(source),
                                                compile_context.Diagnostics()));
}

static Token MakeSyntheticToken(SourceManager &source_manager, TokenType type,
                                std::string text,
                                bool has_leading_space = false) {
  return Token{
      type,
      source_manager.AddSynthetic(std::move(text)),
      has_leading_space,
  };
}

static std::string_view ScanIncludeFile(Scanner &scanner,
                                        SourceManager &source_manager,
                                        DiagnosticContext &diagnostics) {
  Token tok = scanner.PeekNextTokenH();
  if (tok.type == TokenType::LiteralString) {
    scanner.ConsumeToken(tok);
    std::string_view text = source_manager.GetText(tok);
    if (text.size() <= 2) {
      diagnostics.Fail(tok.location, "Unexpected empty include file name");
    }
    return text.substr(1, text.size() - 2);
  } else if (tok.type == TokenType::Lt) {
    scanner.ConsumeToken(tok);
    SourceLocation filename_location;
    while (true) {
      tok = scanner.PeekNextTokenH();
      if (tok.type == TokenType::Gt) {
        scanner.ConsumeToken(tok);
        break;
      }
      if (tok.type == TokenType::NextLine) {
        scanner.ConsumeToken(tok);
        diagnostics.Fail(tok.location,
                         "Unexpected end of line while scanning include "
                         "file");
      } else if (tok.type == TokenType::EndOfFile) {
        diagnostics.Fail(tok.location,
                         "Unexpected end of file while scanning include "
                         "file");
      } else {
        if (!filename_location.IsValid()) {
          filename_location = tok.location;
        } else {
          ASSERT(filename_location.source_id == tok.location.source_id,
                 "Include filename spans multiple sources");
          filename_location.end = tok.location.end;
        }
        scanner.ConsumeToken(tok);
      }
    }
    return source_manager.GetText(filename_location);
  } else {
    diagnostics.Fail(tok.location,
                     "Unexpected token while scanning "
                     "include file: ",
                     source_manager.GetText(tok));
  }
}

static MacroDef ScanMacro(Scanner &scanner, SourceManager &source_manager,
                          DiagnosticContext &diagnostics) {
  Token tok = scanner.PeekNextTokenH();
  if (tok.type != TokenType::Identifier) {
    diagnostics.Fail(tok.location,
                     "Unexpected token while scanning "
                     "macro: ",
                     source_manager.GetText(tok));
  }
  MacroDef macro;
  macro.name = source_manager.GetText(tok);
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
        if (tok.type != TokenType::Identifier) {
          diagnostics.Fail(tok.location,
                           "Unexpected token while "
                           "scanning macro: ",
                           source_manager.GetText(tok));
        }
        macro.params.push_back(source_manager.GetText(tok));
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

static std::vector<std::vector<Token>>
ScanMacroArgs(IRawLexer &lexer, size_t arg_count,
              DiagnosticContext &diagnostics) {
  Token lparen = lexer.EmitToken();
  if (lparen.type != TokenType::LParen) {
    diagnostics.Fail(lparen.location, "Expected '(' after macro name");
  }

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
      diagnostics.Fail(tok.location, "Unexpected end of file");
    } else {
      args[arg_idx].push_back(tok);
    }
  }
  return args;
}

class MacroCondLexer : public IRawLexer {
public:
  MacroCondLexer(Scanner *scanner, MacroContext &macro_context,
                 SourceManager &source_manager, DiagnosticContext &diagnostics)
      : _source_manager(source_manager), _macro_context(macro_context),
        _diagnostics(diagnostics) {
    _scanners.emplace_back(scanner, false);
  }

  Token EmitToken() override {
    Token tok = PeekScannerNextToken();
    if (tok.type == TokenType::Sharp) {
      _diagnostics.Fail(tok.location, "Unexpected '#'");
    }

    if (_scanners.size() == 1 && _source_manager.GetText(tok) == "defined") {
      std::string_view target_macro_name;
      Scanner &scanner = *(Scanner *)_scanners.back().get();
      scanner.ConsumeToken(tok);
      tok = scanner.PeekNextTokenH();
      if (tok.type == TokenType::LParen) {
        scanner.ConsumeToken(tok);
        tok = scanner.PeekNextTokenH();
        if (tok.type != TokenType::Identifier) {
          _diagnostics.Fail(tok.location, "Unexpected token after defined: ",
                            _source_manager.GetText(tok));
        }
        scanner.ConsumeToken(tok);
        target_macro_name = _source_manager.GetText(tok);
        tok = scanner.PeekNextTokenH();
        if (tok.type != TokenType::RParen) {
          _diagnostics.Fail(tok.location, "Expected ')' after defined");
        }
        scanner.ConsumeToken(tok);
      } else if (tok.type == TokenType::Identifier) {
        scanner.ConsumeToken(tok);
        target_macro_name = _source_manager.GetText(tok);
      } else {
        _diagnostics.Fail(tok.location, "Unexpected token after defined: ",
                          _source_manager.GetText(tok));
      }
      return MakeSyntheticToken(
          _source_manager, TokenType::LiteralNum,
          _macro_context.FindMacro(target_macro_name) ? "1" : "0");
    }
    if (tok.type == TokenType::Identifier) {
      if (auto *macro =
              _macro_context.FindMacro(_source_manager.GetText(tok))) {
        CurrScanner()->ConsumeToken(tok);
        if (macro->has_arg) {
          _scanners.emplace_back(
              new MacroScanner(
                  *macro,
                  ScanMacroArgs(*this, macro->params.size(), _diagnostics),
                  _source_manager, _diagnostics),
              true);
        } else {
          _scanners.emplace_back(
              new MacroScanner(*macro, std::vector<std::vector<Token>>(),
                               _source_manager, _diagnostics),
              true);
        }
        return EmitToken();
      } else {
        CurrScanner()->ConsumeToken(tok);
        return MakeSyntheticToken(_source_manager, TokenType::LiteralNum, "0");
      }
    }
    if (tok.type == TokenType::Sharp || tok.type == TokenType::SharpSharp) {
      _diagnostics.Fail(tok.location,
                        "Unexpected preprocessor token in macro condition: ",
                        _source_manager.GetText(tok));
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
        return tok;
      }
      tok = _scanners.size() == 1
                ? ((Scanner *)_scanners[0].get())->PeekNextTokenH()
                : _scanners.back()->PeekNextToken();
    }
    if (_scanners.size() == 1) {
      if (tok.type == TokenType::NextLine) {
        return Token{
            TokenType::EndOfFile,
            {
                .source_id = tok.location.source_id,
                .begin = tok.location.begin,
                .end = tok.location.begin,
            },
        };
      }
      return tok;
    } else {
      return tok;
    }
  }

  IScanner *CurrScanner() {
    ASSERT(!_scanners.empty(), "No current scanner");
    return _scanners.back().get();
  }

  std::vector<MayOwnPtr<IScanner>> _scanners;
  SourceManager &_source_manager;
  MacroContext &_macro_context;
  DiagnosticContext &_diagnostics;
};

std::unique_ptr<ASTNode> ScanMacroCond(Scanner *scanner,
                                       MacroContext &macro_context,
                                       SourceManager &source_manager,
                                       CompileContext &compile_context) {
  MacroCondLexer raw_lexer(scanner, macro_context, source_manager,
                           compile_context.Diagnostics());
  Lexer lexer(raw_lexer);
  return ParseExpr(lexer, compile_context);
}

Token RawLexer::PeekScannerNextToken() {
  Token tok = CurrScanner()->PeekNextToken();
  while (tok.type == TokenType::EndOfFile) {
    _scanners.pop_back();
    if (_scanners.empty()) {
      return tok;
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
    if (auto *macro = _macro_context.FindMacro(_source_manager.GetText(tok))) {
      CurrScanner()->ConsumeToken(tok);
      if (macro->has_arg) {
        _scanners.push_back(std::make_unique<MacroScanner>(
            *macro,
            ScanMacroArgs(*this, macro->params.size(),
                          _compile_context.Diagnostics()),
            _source_manager, _compile_context.Diagnostics()));
      } else {
        _scanners.push_back(std::make_unique<MacroScanner>(
            *macro, std::vector<std::vector<Token>>(), _source_manager,
            _compile_context.Diagnostics()));
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
  std::string_view preproc_name = _source_manager.GetText(preproc_tok);
  if (!IsPreprocessor(preproc_name)) {
    _compile_context.Diagnostics().Fail(
        preproc_tok.location, "Unexpected preprocessor name: ", preproc_name);
  }

  if (preproc_name == "include") {
    curr_scanner.ConsumeToken(preproc_tok);
    std::string curr_file(_source_manager.GetFileName(preproc_tok.location));
    std::string_view include_file = ScanIncludeFile(
        curr_scanner, _source_manager, _compile_context.Diagnostics());
    SourceHandle header =
        _source_manager.LoadSourceFrom(curr_file, include_file);
    _scanners.push_back(
        std::make_unique<Scanner>(header, _compile_context.Diagnostics()));
  } else if (preproc_name == "define") {
    curr_scanner.ConsumeToken(preproc_tok);
    _macro_context.AddMacro(ScanMacro(curr_scanner, _source_manager,
                                      _compile_context.Diagnostics()));
  } else if (preproc_name == "if") {
    curr_scanner.ConsumeToken(preproc_tok);
    std::unique_ptr<ASTNode> cond = ScanMacroCond(
        &curr_scanner, _macro_context, _source_manager, _compile_context);
    uint64_t cond_value =
        EvalMacroCond(cond.get(), _compile_context.Diagnostics());
    _pp_cond_stack.push_back(
        {.is_active = cond_value != 0, .branch = PPCondBranch::Then});
  } else if (preproc_name == "else") {
    curr_scanner.ConsumeToken(preproc_tok);
    if (_pp_cond_stack.empty()) {
      _compile_context.Diagnostics().Fail(preproc_tok.location,
                                          "Unexpected #else");
    }
    PPCond &cond = _pp_cond_stack.back();
    cond.branch = PPCondBranch::Else;
  } else if (preproc_name == "endif") {
    curr_scanner.ConsumeToken(preproc_tok);
    if (_pp_cond_stack.empty()) {
      _compile_context.Diagnostics().Fail(preproc_tok.location,
                                          "Unexpected #endif");
    }
    _pp_cond_stack.pop_back();
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
