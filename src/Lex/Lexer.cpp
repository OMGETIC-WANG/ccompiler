#include "Lexer.h"
#include "../AST/AST.h"
#include "../AST/Context.h"
#include "../AST/Expr.h"
#include "../Util/Util.h"
#include "Diagnostics/Errors.generated.h"
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

static std::optional<std::string_view>
ScanIncludeFile(Scanner &scanner, SourceManager &source_manager,
                DiagnosticContext &diagnostics) {
  Token next;
  TRY_ASSIGN_NEW(tok, scanner.PeekNextTokenH());
  if (tok.type == TokenType::LiteralString) {
    scanner.ConsumeToken(tok);
    std::string_view text = source_manager.GetText(tok);
    if (text.size() <= 2) {
      diagnostics.Error(tok.location, errs::EmptyIncludeFileName{});
      return std::nullopt;
    }
    return text.substr(1, text.size() - 2);
  } else if (tok.type == TokenType::Lt) {
    scanner.ConsumeToken(tok);
    SourceLocation filename_location;
    while (true) {
      TRY_ASSIGN(tok, scanner.PeekNextTokenH());
      if (tok.type == TokenType::Gt) {
        scanner.ConsumeToken(tok);
        break;
      }
      if (tok.type == TokenType::NextLine) {
        scanner.ConsumeToken(tok);
        diagnostics.Error(tok.location, errs::UnexpectedIncludeEol{});
        return std::nullopt;
      } else if (tok.type == TokenType::EndOfFile) {
        diagnostics.Error(tok.location, errs::UnexpectedIncludeEof{});
        return std::nullopt;
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
    diagnostics.Error(tok.location, errs::UnexpectedIncludeToken{
                                        source_manager.GetText(tok)});
    return std::nullopt;
  }
}

static std::optional<MacroDef> ScanMacro(Scanner &scanner,
                                         SourceManager &source_manager,
                                         DiagnosticContext &diagnostics) {
  Token next;
  TRY_ASSIGN_NEW(tok, scanner.PeekNextTokenH());
  if (!IsIdentifierLike(tok)) {
    diagnostics.Error(tok.location,
                      errs::UnexpectedMacroToken{source_manager.GetText(tok)});
    return std::nullopt;
  }
  MacroDef macro;
  macro.name = source_manager.GetText(tok);
  scanner.ConsumeToken(tok);

  TRY_ASSIGN(tok, scanner.PeekNextTokenH());
  if (tok.type == TokenType::LParen && !tok.has_leading_space) {
    macro.has_arg = true;
    scanner.ConsumeToken(tok);
    while (true) {
      TRY_ASSIGN(tok, scanner.PeekNextTokenH());
      if (tok.type == TokenType::RParen) {
        scanner.ConsumeToken(tok);
        break;
      } else {
        if (tok.type == TokenType::DotDotDot) {
          scanner.ConsumeToken(tok);
          macro.has_va_arg = true;
          macro.params.push_back("__VA_ARGS__");
          TRY_ASSIGN(tok, scanner.PeekNextTokenH());
          continue;
        }
        if (tok.type != TokenType::Identifier) {
          diagnostics.Error(tok.location, errs::UnexpectedMacroToken{
                                              source_manager.GetText(tok)});
          return std::nullopt;
        }
        std::string_view param_name = source_manager.GetText(tok);
        scanner.ConsumeToken(tok);
        TRY_ASSIGN(tok, scanner.PeekNextTokenH());
        if (tok.type == TokenType::DotDotDot) {
          macro.has_va_arg = true;
          macro.params.push_back(param_name);
          scanner.ConsumeToken(tok);
          TRY_ASSIGN(tok, scanner.PeekNextTokenH());
        } else {
          macro.params.push_back(param_name);
        }
        if (tok.type == TokenType::Comma) {
          scanner.ConsumeToken(tok);
        }
      }
    }
  }

  while (true) {
    TRY_ASSIGN(tok, scanner.PeekNextTokenH());
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

static std::optional<std::vector<std::vector<Token>>>
ScanMacroArgs(IScanner &scanner, size_t arg_count, bool has_va_arg,
              DiagnosticContext &diagnostics) {
  Token next;
  TRY_ASSIGN_NEW(lparen, scanner.PeekNextToken());
  if (lparen.type != TokenType::LParen) {
    diagnostics.Error(lparen.location, errs::ExpectedMacroLParen{});
    return std::nullopt;
  }
  scanner.ConsumeToken(lparen);

  std::vector<std::vector<Token>> args(arg_count);
  size_t real_arg_idx = 0;
  size_t arg_idx = 0;

  int depth = 0;
  while (true) {
    TRY_ASSIGN(next, scanner.PeekNextToken());
    Token tok = next;
    if (tok.type == TokenType::RParen) {
      if (depth == 0) {
        scanner.ConsumeToken(tok);
        break;
      } else {
        depth--;
        args[real_arg_idx].push_back(tok);
      }
    } else if (tok.type == TokenType::LParen) {
      depth++;
      args[real_arg_idx].push_back(tok);
    } else if (tok.type == TokenType::Comma && depth == 0) {
      arg_idx++;
      real_arg_idx = std::min(arg_idx, arg_count - 1);

      if (arg_idx >= arg_count && has_va_arg) {
        args[real_arg_idx].push_back(tok);
      }
    } else if (tok.type == TokenType::EndOfFile) {
      diagnostics.Error(tok.location, errs::UnexpectedEnd{});
      return std::nullopt;
    } else {
      args[real_arg_idx].push_back(tok);
    }
    scanner.ConsumeToken(tok);
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

  std::optional<Token> EmitToken() override {
    Token next;
    TRY_ASSIGN_NEW(tok, PeekScannerNextToken());
    if (tok.type == TokenType::Sharp) {
      _diagnostics.Error(tok.location, errs::UnexpectedHash{});
      return std::nullopt;
    }

    if (_scanners.size() == 1 && _source_manager.GetText(tok) == "defined") {
      std::string_view target_macro_name;
      Scanner &scanner = *(Scanner *)_scanners.back().get();
      scanner.ConsumeToken(tok);
      TRY_ASSIGN(tok, scanner.PeekNextTokenH());
      if (tok.type == TokenType::LParen) {
        scanner.ConsumeToken(tok);
        TRY_ASSIGN(tok, scanner.PeekNextTokenH());
        if (tok.type != TokenType::Identifier) {
          _diagnostics.Error(tok.location, errs::UnexpectedTokenAfterDefined{
                                               _source_manager.GetText(tok)});
          return std::nullopt;
        }
        scanner.ConsumeToken(tok);
        target_macro_name = _source_manager.GetText(tok);
        TRY_ASSIGN(tok, scanner.PeekNextTokenH());
        if (tok.type != TokenType::RParen) {
          _diagnostics.Error(tok.location, errs::ExpectedRParenAfterDefined{});
          return std::nullopt;
        }
        scanner.ConsumeToken(tok);
      } else if (tok.type == TokenType::Identifier) {
        scanner.ConsumeToken(tok);
        target_macro_name = _source_manager.GetText(tok);
      } else {
        _diagnostics.Error(tok.location, errs::UnexpectedTokenAfterDefined{
                                             _source_manager.GetText(tok)});
        return std::nullopt;
      }
      return MakeSyntheticToken(
          _source_manager, TokenType::LiteralNum,
          _macro_context.FindMacro(target_macro_name) ? "1" : "0");
    }
    if (IsIdentifierLike(tok)) {
      if (auto *macro_desc =
              _macro_context.FindMacroDesc(_source_manager.GetText(tok))) {
        if (macro_desc->is_expanding) {
          return tok;
        }
        MacroDef *macro = macro_desc->macro.get();
        CurrScanner()->ConsumeToken(tok);
        if (macro->has_arg) {
          TRY_ASSIGN_NEW(args,
                         ScanMacroArgs(*CurrScanner(), macro->params.size(),
                                       macro->has_va_arg, _diagnostics));
          _scanners.emplace_back(new MacroScanner(*macro_desc, std::move(args),
                                                  _source_manager,
                                                  _diagnostics),
                                 true);
        } else {
          _scanners.emplace_back(
              new MacroScanner(*macro_desc, std::vector<std::vector<Token>>(),
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
      _diagnostics.Error(tok.location, errs::UnexpectedTokenInMacroCondition{
                                           _source_manager.GetText(tok)});
      return std::nullopt;
    }
    if (tok.type != TokenType::EndOfFile) {
      CurrScanner()->ConsumeToken(tok);
    }
    return tok;
  }

private:
  std::optional<Token> PeekScannerNextToken() {
    std::optional<Token> tok =
        _scanners.size() == 1
            ? ((Scanner *)_scanners[0].get())->PeekNextTokenH()
            : _scanners.back()->PeekNextToken();
    while (tok && tok->type == TokenType::EndOfFile) {
      _scanners.pop_back();
      if (_scanners.empty()) {
        return tok;
      }
      tok = _scanners.size() == 1
                ? ((Scanner *)_scanners[0].get())->PeekNextTokenH()
                : _scanners.back()->PeekNextToken();
    }
    if (!tok) {
      return std::nullopt;
    }
    if (_scanners.size() == 1) {
      if (tok->type == TokenType::NextLine) {
        return Token{
            TokenType::EndOfFile,
            {
                .source_id = tok->location.source_id,
                .begin = tok->location.begin,
                .end = tok->location.begin,
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

static std::optional<std::unique_ptr<ASTNode>>
ScanMacroCond(Scanner *scanner, MacroContext &macro_context,
              SourceManager &source_manager, CompileContext &compile_context) {
  MacroCondLexer raw_lexer(scanner, macro_context, source_manager,
                           compile_context.Diagnostics());
  Lexer lexer(raw_lexer);
  return ParseExpr(lexer, compile_context);
}

std::optional<Token> RawLexer::PeekScannerNextToken() {
  std::optional<Token> tok = CurrScanner()->PeekNextToken();
  while (tok && tok->type == TokenType::EndOfFile) {
    _scanners.pop_back();
    if (_scanners.empty()) {
      return tok;
    }
    tok = CurrScanner()->PeekNextToken();
  }
  return tok;
}

std::optional<Token> RawLexer::EmitToken() {
  while (true) {
    TRY_ASSIGN_NEW(step, EmitTokenImpl());
    if (auto *tok = std::get_if<Token>(&step)) {
      if (!IsInValidPPCond() && tok->type != TokenType::EndOfFile) {
        continue;
      }
      return std::move(*tok);
    }
  }
}

static bool IsPreprocessor(std::string_view name) {
  return name == "include" || name == "define" || name == "undef" ||
         name == "if" || name == "elif" || name == "ifdef" ||
         name == "ifndef" || name == "else" || name == "endif" ||
         name == "error" || name == "warning";
}

static std::optional<std::monostate> SkipPreprocessorLine(Scanner &scanner) {
  while (true) {
    TRY_ASSIGN_NEW(tok, scanner.PeekNextTokenH());
    if (tok.type == TokenType::EndOfFile) {
      return std::monostate{};
    }
    scanner.ConsumeToken(tok);
    if (tok.type == TokenType::NextLine) {
      return std::monostate{};
    }
  }
}

static std::optional<std::pair<SourceLocation, std::string>>
ScanPreprocessorMessage(Scanner &scanner, SourceManager &source_manager) {
  SourceLocation location;
  std::string message;
  while (true) {
    TRY_ASSIGN_NEW(tok, scanner.PeekNextTokenH());
    if (tok.type == TokenType::EndOfFile) {
      return std::pair<SourceLocation, std::string>{location, message};
    }
    if (tok.type == TokenType::NextLine) {
      scanner.ConsumeToken(tok);
      return std::pair<SourceLocation, std::string>{location, message};
    }
    if (location.IsValid()) {
      message += ' ';
    } else {
      location = tok.location;
    }
    message += source_manager.GetText(tok);
    scanner.ConsumeToken(tok);
  }
}

std::optional<RawLexer::EmitStep> RawLexer::EmitTokenImpl() {
  bool is_active_group = IsInValidPPCond();
  TRY_ASSIGN_NEW(tok, PeekScannerNextToken());
  if (tok.type != TokenType::Sharp) {
    if (!is_active_group) {
      if (tok.type != TokenType::EndOfFile) {
        CurrScanner()->ConsumeToken(tok);
      }
      return EmitStep{std::move(tok)};
    }
    if (auto *macro_desc =
            _macro_context.FindMacroDesc(_source_manager.GetText(tok))) {
      CurrScanner()->ConsumeToken(tok);
      if (macro_desc->is_expanding) {
        return EmitStep{std::move(tok)};
      }
      MacroDef *macro = macro_desc->macro.get();
      if (macro->has_arg) {
        TRY_ASSIGN_NEW(args, ScanMacroArgs(*CurrScanner(), macro->params.size(),
                                           macro->has_va_arg,
                                           _compile_context.Diagnostics()));
        _scanners.push_back(std::make_unique<MacroScanner>(
            *macro_desc, std::move(args), _source_manager,
            _compile_context.Diagnostics()));
      } else {
        _scanners.push_back(std::make_unique<MacroScanner>(
            *macro_desc, std::vector<std::vector<Token>>(), _source_manager,
            _compile_context.Diagnostics()));
      }
      return EmitStep{RetryEmit{}};
    }
    if (tok.type != TokenType::EndOfFile) {
      CurrScanner()->ConsumeToken(tok);
    }
    return EmitStep{std::move(tok)};
  }

  ASSERT(CurrScanner()->Is<Scanner>(), "Unexpected scanner for preprocessor");
  Scanner &curr_scanner = (Scanner &)*CurrScanner();

  curr_scanner.ConsumeToken(tok);
  // The preprocessor name must be in the same scanner as the sharp.
  TRY_ASSIGN_NEW(preproc_tok, curr_scanner.PeekNextToken());
  std::string_view preproc_name = _source_manager.GetText(preproc_tok);
  if (!IsPreprocessor(preproc_name)) {
    _compile_context.Diagnostics().Error(
        preproc_tok.location, errs::UnexpectedPreprocessorName{preproc_name});
    return std::nullopt;
  }

  if (preproc_name == "include") {
    curr_scanner.ConsumeToken(preproc_tok);
    if (!is_active_group) {
      TRY(SkipPreprocessorLine(curr_scanner));
      return EmitStep{RetryEmit{}};
    }
    std::string curr_file(_source_manager.GetFileName(preproc_tok.location));
    TRY_ASSIGN_NEW(include_file,
                   ScanIncludeFile(curr_scanner, _source_manager,
                                   _compile_context.Diagnostics()));
    SourceHandle header =
        _source_manager.LoadSourceFrom(curr_file, include_file);
    _scanners.push_back(
        std::make_unique<Scanner>(header, _compile_context.Diagnostics()));
  } else if (preproc_name == "define") {
    curr_scanner.ConsumeToken(preproc_tok);
    if (is_active_group) {
      TRY_ASSIGN_NEW(macro, ScanMacro(curr_scanner, _source_manager,
                                      _compile_context.Diagnostics()));
      _macro_context.AddMacro(std::move(macro));
    } else {
      TRY(SkipPreprocessorLine(curr_scanner));
    }
  } else if (preproc_name == "undef") {
    curr_scanner.ConsumeToken(preproc_tok);
    TRY_ASSIGN_NEW(macro_tok, curr_scanner.PeekNextTokenH());
    if (macro_tok.type != TokenType::Identifier) {
      _compile_context.Diagnostics().Error(
          macro_tok.location, errs::ExpectedIdentifierAfterUndef{});
      return std::nullopt;
    }
    curr_scanner.ConsumeToken(macro_tok);
    if (is_active_group) {
      _macro_context.RemoveMacro(_source_manager.GetText(macro_tok));
    }
  } else if (preproc_name == "if") {
    curr_scanner.ConsumeToken(preproc_tok);
    if (is_active_group) {
      TRY_ASSIGN_NEW(cond, ScanMacroCond(&curr_scanner, _macro_context,
                                         _source_manager, _compile_context));
      TRY_ASSIGN_NEW(cond_value,
                     EvalMacroCond(cond.get(), _compile_context.Diagnostics()));
      _pp_cond_stack.push_back(
          {.is_active = cond_value != 0, .branch = PPCondBranch::Then});
    } else {
      TRY(SkipPreprocessorLine(curr_scanner));
      _pp_cond_stack.push_back(
          {.is_active = false, .branch = PPCondBranch::Then});
    }
  } else if (preproc_name == "ifdef" || preproc_name == "ifndef") {
    curr_scanner.ConsumeToken(preproc_tok);
    TRY_ASSIGN_NEW(macro_tok, curr_scanner.PeekNextTokenH());
    if (macro_tok.type != TokenType::Identifier) {
      _compile_context.Diagnostics().Error(
          macro_tok.location,
          errs::ExpectedIdentifierAfterDirective{preproc_name});
      return std::nullopt;
    }
    curr_scanner.ConsumeToken(macro_tok);
    bool is_defined =
        _macro_context.FindMacro(_source_manager.GetText(macro_tok)) != nullptr;
    if (preproc_name == "ifndef") {
      is_defined = !is_defined;
    }
    _pp_cond_stack.push_back(
        {.is_active = is_defined, .branch = PPCondBranch::Then});
  } else if (preproc_name == "else") {
    curr_scanner.ConsumeToken(preproc_tok);
    if (_pp_cond_stack.empty()) {
      _compile_context.Diagnostics().Error(preproc_tok.location,
                                           errs::UnexpectedElse{});
      return std::nullopt;
    }
    PPCond &cond = _pp_cond_stack.back();
    cond.branch = PPCondBranch::Else;
  } else if (preproc_name == "elif") {
    curr_scanner.ConsumeToken(preproc_tok);
    TRY_ASSIGN_NEW(cond, ScanMacroCond(&curr_scanner, _macro_context,
                                       _source_manager, _compile_context));
    TRY_ASSIGN_NEW(cond_value,
                   EvalMacroCond(cond.get(), _compile_context.Diagnostics()));
    if (_pp_cond_stack.empty()) {
      _compile_context.Diagnostics().Error(preproc_tok.location,
                                           errs::UnexpectedElif{});
      return std::nullopt;
    }
    _pp_cond_stack.back().is_active = cond_value != 0;
    _pp_cond_stack.back().branch = PPCondBranch::Then;

  } else if (preproc_name == "endif") {
    curr_scanner.ConsumeToken(preproc_tok);
    if (_pp_cond_stack.empty()) {
      _compile_context.Diagnostics().Error(preproc_tok.location,
                                           errs::UnexpectedEndif{});
      return std::nullopt;
    }
    _pp_cond_stack.pop_back();
  } else if (preproc_name == "error") {
    curr_scanner.ConsumeToken(preproc_tok);
    TRY_ASSIGN_NEW(preprocessor_message,
                   ScanPreprocessorMessage(curr_scanner, _source_manager));
    auto [location, message] = std::move(preprocessor_message);
    if (is_active_group) {
      _compile_context.Diagnostics().Error(
          location.IsValid() ? location : preproc_tok.location,
          errs::PreprocessorError{message});
      return std::nullopt;
    }
  } else if (preproc_name == "warning") {
    curr_scanner.ConsumeToken(preproc_tok);
    TRY(SkipPreprocessorLine(curr_scanner));
    if (is_active_group) {
      // TODO: Add warning support to DiagnosticContext
    }
  }

  return EmitStep{RetryEmit{}};
}

bool RawLexer::IsInValidPPCond() const {
  for (const PPCond &cond : _pp_cond_stack) {
    bool is_active = cond.is_active == (cond.branch == PPCondBranch::Then);
    if (!is_active) {
      return false;
    }
  }
  return true;
}
