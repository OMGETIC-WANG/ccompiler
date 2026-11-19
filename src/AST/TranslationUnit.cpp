#include "TranslationUnit.h"
#include "../Exception.h"
#include "Context.h"
#include "Declaration.h"
#include "Diagnostics/Errors.generated.h"
#include "Expr.h"
#include "ParserError.h"

static bool IsStatementStart(Token tok, CompileContext &context) {
  if (IsDeclarationSpecifier(tok, context)) {
    return true;
  }

  switch (tok.type) {
  case TokenType::KwIf:
  case TokenType::KwWhile:
  case TokenType::KwDo:
  case TokenType::KwFor:
  case TokenType::KwSwitch:
  case TokenType::KwReturn:
  case TokenType::KwGoto:
  case TokenType::KwBreak:
  case TokenType::KwContinue:
  case TokenType::Semicolon:
  case TokenType::LBrace:
  case TokenType::Identifier:
  case TokenType::LiteralNum:
  case TokenType::LiteralString:
  case TokenType::LiteralChar:
  case TokenType::LParen:
  case TokenType::Add:
  case TokenType::Sub:
  case TokenType::Star:
  case TokenType::Et:
  case TokenType::BitNot:
  case TokenType::Not:
  case TokenType::AddAdd:
  case TokenType::SubSub:
    return true;
  default:
    return false;
  }
}

// Skip a malformed construct while keeping nested delimiters from exposing
// their interior separators as statement boundaries.
static void SynchronizeToStatementBoundary(Lexer &lexer,
                                           CompileContext &context,
                                           bool in_compound_stmt) {
  size_t paren_depth = 0;
  size_t bracket_depth = 0;
  size_t brace_depth = 0;

  while (true) {
    Token tok = lexer.PeekNextToken();
    if (tok.type == TokenType::EndOfFile) {
      return;
    }

    const bool at_top_level =
        paren_depth == 0 && bracket_depth == 0 && brace_depth == 0;
    if (at_top_level) {
      if (tok.type == TokenType::RBrace) {
        if (in_compound_stmt) {
          return;
        }
        context.Diagnostics().Error(tok.location,
                                    errs::UnexpectedClosingBrace{});
        lexer.ConsumeToken(tok);
        return;
      }
      if (tok.type == TokenType::Semicolon) {
        lexer.ConsumeToken(tok);
        return;
      }
      if (IsStatementStart(tok, context)) {
        return;
      }
    }

    switch (tok.type) {
    case TokenType::LParen:
      ++paren_depth;
      break;
    case TokenType::RParen:
      if (paren_depth > 0) {
        --paren_depth;
      }
      break;
    case TokenType::LBracket:
      ++bracket_depth;
      break;
    case TokenType::RBracket:
      if (bracket_depth > 0) {
        --bracket_depth;
      }
      break;
    case TokenType::LBrace:
      ++brace_depth;
      break;
    case TokenType::RBrace:
      if (brace_depth > 0) {
        --brace_depth;
      }
      break;
    default:
      break;
    }

    lexer.ConsumeToken(tok);
  }
}

static std::unique_ptr<ASTNode> ParseControlCondExpr(Lexer &lexer,
                                                     CompileContext &context) {
  Token tok = lexer.PeekNextToken();
  if (tok.type != TokenType::LParen) {
    RaiseParserError(context, tok.location, errs::ExpectedLParen{});
  }
  lexer.ConsumeToken(tok);
  auto expr = ParseExpr(lexer, context);
  tok = lexer.PeekNextToken();
  if (tok.type != TokenType::RParen) {
    RaiseParserError(context, tok.location, errs::ExpectedRParen{});
  }
  lexer.ConsumeToken(tok);
  return expr;
}

static std::unique_ptr<ASTNode> ParseIfStmt(Lexer &lexer,
                                            CompileContext &context);

static std::unique_ptr<WhileStmt> ParseWhileStmt(Lexer &lexer,
                                                 CompileContext &context);

static std::unique_ptr<DoWhileStmt> ParseDoWhileStmt(Lexer &lexer,
                                                     CompileContext &context);

static std::unique_ptr<ForStmt> ParseForStmt(Lexer &lexer,
                                             CompileContext &context);

static std::unique_ptr<SwitchStmt> ParseSwitchStmt(Lexer &lexer,
                                                   CompileContext &context);

static std::unique_ptr<ASTNode> ParseReturnStmt(Lexer &lexer,
                                                CompileContext &context);

static std::unique_ptr<ASTNode> ParseGotoStmt(Lexer &lexer,
                                              CompileContext &context);

static std::unique_ptr<ASTNode> ParseStmtOnly(Lexer &lexer,
                                              CompileContext &context) {
  std::unique_ptr<ASTNode> stmt;

  Token tok = lexer.PeekNextToken();
  if (IsDeclarationSpecifier(tok, context)) {
    stmt = ParseDeclaration(lexer, context);
  } else {
    switch (tok.type) {
    case TokenType::KwIf:
      stmt = ParseIfStmt(lexer, context);
      break;
    case TokenType::KwWhile:
      stmt = ParseWhileStmt(lexer, context);
      break;
    case TokenType::KwDo:
      stmt = ParseDoWhileStmt(lexer, context);
      break;
    case TokenType::KwFor:
      stmt = ParseForStmt(lexer, context);
      break;
    case TokenType::KwSwitch:
      stmt = ParseSwitchStmt(lexer, context);
      break;
    case TokenType::KwReturn:
      stmt = ParseReturnStmt(lexer, context);
      break;
    case TokenType::KwGoto:
      stmt = ParseGotoStmt(lexer, context);
      break;
    case TokenType::KwBreak:
      lexer.ConsumeToken(tok);
      stmt = std::make_unique<BreakStmt>();
      stmt->location = tok.location;
      break;
    case TokenType::KwContinue:
      lexer.ConsumeToken(tok);
      stmt = std::make_unique<ContinueStmt>();
      stmt->location = tok.location;
      break;
    default: {
      Token next_tok = lexer.PeekNextToken(1);
      if (next_tok.type == TokenType::Colon) {
        lexer.ConsumeToken(tok);
        Token colon = lexer.PeekNextToken();
        if (colon.type != TokenType::Colon) {
          RaiseParserError(context, colon.location, errs::ExpectedLabelColon{});
        }
        lexer.ConsumeToken(colon);
        auto label_stmt = std::make_unique<LabelStmt>();
        label_stmt->location = CombineLocation(tok.location, colon.location);
        label_stmt->name = context.Sources().GetText(tok);
        context.AddLabel(label_stmt->name);
        stmt = std::move(label_stmt);
      } else {
        stmt = ParseExpr(lexer, context);
      }
      break;
    }
    }
  }

  return stmt;
}

static bool StmtNeedsSemicolon(const ASTNode *stmt) {
  if (!stmt) {
    return true;
  }
  switch (stmt->GetType()) {
  case ASTNodeType::CompoundStmt:
  case ASTNodeType::IfStmt:
  case ASTNodeType::WhileStmt:
  case ASTNodeType::ForStmt:
  case ASTNodeType::SwitchStmt:
  case ASTNodeType::LabelStmt:
    return false;
  case ASTNodeType::FunctionDecl:
    return !stmt->As<FunctionDecl>()->body;
  case ASTNodeType::DeclStmt: {
    const auto &decls = stmt->As<DeclStmt>()->decls;
    return decls.empty() || StmtNeedsSemicolon(decls.back().get());
  }
  default:
    return true;
  }
}

// Parse one statement or a compound statement.
static std::unique_ptr<ASTNode> ParseStmt(Lexer &lexer,
                                          CompileContext &context) {
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::LBrace) {
    return ParseCompoundStmt(lexer, context);
  }
  if (tok.type == TokenType::RBrace) {
    return nullptr;
  }
  if (tok.type == TokenType::EndOfFile) {
    return nullptr;
  }
  if (tok.type == TokenType::Semicolon) {
    lexer.ConsumeToken(tok);
    auto empty_stmt = std::make_unique<EmptyStmt>();
    empty_stmt->location = tok.location;
    return empty_stmt;
  }

  std::unique_ptr<ASTNode> stmt = ParseStmtOnly(lexer, context);

  if (StmtNeedsSemicolon(stmt.get())) {
    Token next = lexer.PeekNextToken();
    if (next.type == TokenType::Semicolon) {
      lexer.ConsumeToken(next);
    } else {
      context.Diagnostics().Error(next.location,
                                  errs::ExpectedSemicolonAfterStatement{});
      if (next.type != TokenType::EndOfFile && next.type != TokenType::RBrace &&
          !IsStatementStart(next, context)) {
        throw ParserRecovery();
      }
    }
  }

  return stmt;
}

std::unique_ptr<CompoundStmt> ParseCompoundStmt(Lexer &lexer,
                                                CompileContext &context) {
  auto tok = lexer.PeekNextToken();
  if (tok.type != TokenType::LBrace) {
    RaiseParserError(context, tok.location, errs::ExpectedLBrace{});
  }
  lexer.ConsumeToken(tok);
  auto CS = std::make_unique<CompoundStmt>();
  SourceLocation start_location = tok.location;
  CompileContextLayerGuard layer_guard(context);
  while (true) {
    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::EndOfFile) {
      context.Diagnostics().Error(tok.location, errs::ExpectedClosingBrace{});
      CS->location =
          CombineLocation(start_location, lexer.GetLastConsumedLocation());
      break;
    }
    if (tok.type == TokenType::RBrace) {
      lexer.ConsumeToken(tok);
      CS->location = CombineLocation(start_location, tok.location);
      break;
    }
    size_t previous_consumed_count = lexer.GetConsumedTokenCount();
    try {
      CS->AddStmt(ParseStmt(lexer, context));
    } catch (const ParserRecovery &) {
      SynchronizeToStatementBoundary(lexer, context, true);
      Token next = lexer.PeekNextToken();
      // A boundary token may also be the token that triggered the error.
      if (lexer.GetConsumedTokenCount() == previous_consumed_count &&
          next.type != TokenType::RBrace && next.type != TokenType::EndOfFile) {
        lexer.ConsumeToken(next);
      }
    }
  }
  return CS;
}

static std::unique_ptr<ASTNode> ParseIfStmt(Lexer &lexer,
                                            CompileContext &context) {
  Token start_token = lexer.PeekNextToken();
  auto if_stmt = std::make_unique<IfStmt>();
  lexer.ConsumeNextToken(); // consume "if"
  if_stmt->condition = ParseControlCondExpr(lexer, context);
  {
    CompileContextLayerGuard guard(context);
    if_stmt->then_branch = ParseStmt(lexer, context);
  }
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::KwElse) {
    lexer.ConsumeToken(tok);
    CompileContextLayerGuard guard(context);
    if_stmt->else_branch = ParseStmt(lexer, context);
  }
  SourceLocation end_location =
      if_stmt->else_branch
          ? if_stmt->else_branch->location
          : (if_stmt->then_branch ? if_stmt->then_branch->location
                                  : lexer.GetLastConsumedLocation());
  if_stmt->location = CombineLocation(start_token.location, end_location);
  return if_stmt;
}

static std::unique_ptr<WhileStmt> ParseWhileStmt(Lexer &lexer,
                                                 CompileContext &context) {
  Token start_token = lexer.PeekNextToken();
  auto while_stmt = std::make_unique<WhileStmt>();
  lexer.ConsumeNextToken(); // consume "while"
  while_stmt->condition = ParseControlCondExpr(lexer, context);
  {
    CompileContextLayerGuard guard(context);
    while_stmt->body = ParseStmt(lexer, context);
  }
  SourceLocation end_location = while_stmt->body
                                    ? while_stmt->body->location
                                    : lexer.GetLastConsumedLocation();
  while_stmt->location = CombineLocation(start_token.location, end_location);
  return while_stmt;
}

static std::unique_ptr<DoWhileStmt> ParseDoWhileStmt(Lexer &lexer,
                                                     CompileContext &context) {
  Token start_token = lexer.PeekNextToken();
  auto do_while_stmt = std::make_unique<DoWhileStmt>();
  lexer.ConsumeNextToken(); // consume "do"
  {
    CompileContextLayerGuard guard(context);
    do_while_stmt->body = ParseStmt(lexer, context);
  }
  Token while_token = lexer.PeekNextToken();
  if (while_token.type != TokenType::KwWhile) {
    RaiseParserError(context, while_token.location,
                     errs::ExpectedWhileAfterDo{});
  }
  lexer.ConsumeToken(while_token);
  do_while_stmt->condition = ParseControlCondExpr(lexer, context);
  do_while_stmt->location =
      CombineLocation(start_token.location, lexer.GetLastConsumedLocation());
  return do_while_stmt;
}

static std::unique_ptr<ForStmt> ParseForStmt(Lexer &lexer,
                                             CompileContext &context) {
  Token start_token = lexer.PeekNextToken();
  auto for_stmt = std::make_unique<ForStmt>();
  lexer.ConsumeNextToken();
  Token lparen = lexer.PeekNextToken();
  if (lparen.type != TokenType::LParen) {
    RaiseParserError(context, lparen.location, errs::ExpectedLParen{});
  }
  lexer.ConsumeToken(lparen);
  CompileContextLayerGuard guard(context);

  if (lexer.PeekNextToken().type == TokenType::Semicolon) {
    for_stmt->init = nullptr;
  } else {
    for_stmt->init = ParseStmtOnly(lexer, context);
  }
  Token semicolon = lexer.PeekNextToken();
  if (semicolon.type != TokenType::Semicolon) {
    RaiseParserError(context, semicolon.location, errs::ExpectedSemicolon{});
  }
  lexer.ConsumeToken(semicolon);

  if (lexer.PeekNextToken().type == TokenType::Semicolon) {
    for_stmt->condition = nullptr;
  } else {
    for_stmt->condition = ParseStmtOnly(lexer, context);
  }
  semicolon = lexer.PeekNextToken();
  if (semicolon.type != TokenType::Semicolon) {
    RaiseParserError(context, semicolon.location, errs::ExpectedSemicolon{});
  }
  lexer.ConsumeToken(semicolon);

  if (lexer.PeekNextToken().type == TokenType::RParen) {
    for_stmt->increment = nullptr;
  } else {
    for_stmt->increment = ParseStmtOnly(lexer, context);
  }

  Token rparen = lexer.PeekNextToken();
  if (rparen.type != TokenType::RParen) {
    RaiseParserError(context, rparen.location, errs::ExpectedRParen{});
  }
  lexer.ConsumeToken(rparen);
  {
    CompileContextLayerGuard body_guard(context);
    for_stmt->body = ParseStmt(lexer, context);
  }
  SourceLocation end_location = for_stmt->body
                                    ? for_stmt->body->location
                                    : lexer.GetLastConsumedLocation();
  for_stmt->location = CombineLocation(start_token.location, end_location);
  return for_stmt;
}

static bool IsCaseEnd(TokenType type) {
  return type == TokenType::KwCase || type == TokenType::KwDefault ||
         type == TokenType::RBrace;
}

static std::unique_ptr<SwitchStmt> ParseSwitchStmt(Lexer &lexer,
                                                   CompileContext &context) {
  Token start_token = lexer.PeekNextToken();
  auto switch_stmt = std::make_unique<SwitchStmt>();
  lexer.ConsumeNextToken(); // consume "switch"
  switch_stmt->value = ParseControlCondExpr(lexer, context);
  if (lexer.PeekNextToken().type == TokenType::Semicolon) {
    switch_stmt->location =
        CombineLocation(start_token.location, lexer.GetLastConsumedLocation());
    return switch_stmt;
  } else {
    Token lbrace = lexer.PeekNextToken();
    if (lbrace.type != TokenType::LBrace) {
      RaiseParserError(context, lbrace.location, errs::ExpectedLBrace{});
    }
    lexer.ConsumeToken(lbrace);
    CompileContextLayerGuard guard(context);
    while (true) {
      Token tok = lexer.PeekNextToken();
      switch (tok.type) {
      case TokenType::KwCase: {
        Token case_start = tok;
        SourceLocation case_end = tok.location;
        auto case_stmt = std::make_unique<CaseStmt>();
        while (lexer.PeekNextToken().type == TokenType::KwCase) {
          lexer.ConsumeNextToken();
          case_stmt->case_vals.push_back(ParseExpr(lexer, context));
          case_end = case_stmt->case_vals.back()->location;
          Token colon = lexer.PeekNextToken();
          if (colon.type != TokenType::Colon) {
            RaiseParserError(context, colon.location, errs::ExpectedColon{});
          }
          case_end = colon.location;
          lexer.ConsumeToken(colon);
        }
        do {
          case_stmt->body.push_back(ParseStmt(lexer, context));
        } while (!IsCaseEnd(lexer.PeekNextToken().type));
        if (!case_stmt->body.empty() && case_stmt->body.back()) {
          case_end = case_stmt->body.back()->location;
        }
        case_stmt->location = CombineLocation(case_start.location, case_end);
        switch_stmt->cases.push_back(std::move(case_stmt));
        break;
      }
      case TokenType::KwDefault: {
        lexer.ConsumeToken(tok);
        SourceLocation default_end = tok.location;
        auto default_stmt = std::make_unique<DefaultStmt>();
        Token colon = lexer.PeekNextToken();
        if (colon.type != TokenType::Colon) {
          RaiseParserError(context, colon.location, errs::ExpectedColon{});
        }
        default_end = colon.location;
        lexer.ConsumeToken(colon);
        do {
          default_stmt->body.push_back(ParseStmt(lexer, context));
        } while (!IsCaseEnd(lexer.PeekNextToken().type));
        if (!default_stmt->body.empty() && default_stmt->body.back()) {
          default_end = default_stmt->body.back()->location;
        }
        default_stmt->location = CombineLocation(tok.location, default_end);
        switch_stmt->cases.push_back(std::move(default_stmt));
        break;
      }
      case TokenType::RBrace:
        lexer.ConsumeToken(tok);
        switch_stmt->location =
            CombineLocation(start_token.location, tok.location);
        return switch_stmt;
      default:
        RaiseParserError(context, tok.location,
                         errs::UnexpectedSwitchToken{tok.type});
      }
    }
  }
}

static std::unique_ptr<ASTNode> ParseReturnStmt(Lexer &lexer,
                                                CompileContext &context) {
  Token start_token = lexer.PeekNextToken();
  auto ret_stmt = std::make_unique<ReturnStmt>();
  lexer.ConsumeNextToken(); // consume "return"
  ret_stmt->ret_value = ParseExpr(lexer, context);
  ret_stmt->location = CombineLocation(
      start_token.location, ret_stmt->ret_value ? ret_stmt->ret_value->location
                                                : start_token.location);
  return ret_stmt;
}

static std::unique_ptr<ASTNode> ParseGotoStmt(Lexer &lexer,
                                              CompileContext &context) {
  Token start_token = lexer.PeekNextToken();
  auto goto_stmt = std::make_unique<GotoStmt>();
  lexer.ConsumeNextToken(); // consume "goto"
  Token tok = lexer.PeekNextToken();
  if (tok.type != TokenType::Identifier) {
    RaiseParserError(context, tok.location, errs::ExpectedGotoIdentifier{});
  }
  lexer.ConsumeToken(tok);
  goto_stmt->target = context.Sources().GetText(tok);
  goto_stmt->location = CombineLocation(start_token.location, tok.location);
  return goto_stmt;
}

static void AddVaList(CompileContext &context) {
  auto *va_list_type = context.Types().GetOrCreateStruct(
      context.CurrentScopeId(), "__builtin_va_list_type");
  // x86_64 SysV va_list tag layout.
  StructField fields[] = {
      {"", context.Types().getInt(), 0},
      {"", context.Types().getInt(), 1},
      {"", context.Types().Pointer(context.Types().getVoid()), 2},
      {"", context.Types().Pointer(context.Types().getVoid()), 3}};
  context.Types().CompleteStruct(va_list_type, fields);

  // musl suspects that __builtin_va_list is a typedef
  context.AddSyntheticTypedef("__builtin_va_list",
                              context.Types().Array(va_list_type, 1));
}

std::unique_ptr<TranslationUnit>
ParseTranslationUnit(Lexer &lexer, TypeContext &types, SourceManager &sources,
                     DiagnosticContext &diagnostics) {
  std::unique_ptr<TranslationUnit> TU = std::make_unique<TranslationUnit>();
  Token first_token = lexer.PeekNextToken();
  SourceLocation translation_unit_location = first_token.location;
  bool has_decls = false;
  CompileContext context(types, sources, diagnostics);
  context.PushLayer();
  AddVaList(context);
  while (true) {
    Token tok = lexer.PeekNextToken();
    if (tok.type == TokenType::EndOfFile) {
      break;
    }
    if (tok.type == TokenType::RBrace) {
      context.Diagnostics().Error(tok.location, errs::UnexpectedClosingBrace{});
      lexer.ConsumeToken(tok);
      continue;
    }
    std::unique_ptr<ASTNode> stmt;
    size_t previous_consumed_count = lexer.GetConsumedTokenCount();
    try {
      stmt = ParseStmt(lexer, context);
    } catch (const ParserRecovery &) {
      SynchronizeToStatementBoundary(lexer, context, false);
      Token next = lexer.PeekNextToken();
      // Force progress when synchronization stopped at the failing token.
      if (lexer.GetConsumedTokenCount() == previous_consumed_count &&
          next.type != TokenType::RBrace && next.type != TokenType::EndOfFile) {
        lexer.ConsumeToken(next);
      }
      continue;
    }
    if (stmt && stmt->GetType() != ASTNodeType::EmptyStmt) {
      translation_unit_location =
          CombineLocation(translation_unit_location, stmt->location);
      has_decls = true;
      TU->decls.push_back(std::move(stmt));
    }
  }
  if (!has_decls) {
    translation_unit_location = first_token.location;
  }
  TU->location = translation_unit_location;
  return TU;
}

AST_PRINT_CHILDREN_IMPL(TranslationUnit, AST_CHILDREN(decls))
