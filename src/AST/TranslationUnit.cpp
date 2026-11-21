#include "TranslationUnit.h"
#include "../Exception.h"
#include "../Util/Util.h"
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
static std::optional<std::monostate>
SynchronizeToStatementBoundary(Lexer &lexer, CompileContext &context,
                               bool in_compound_stmt) {
  size_t paren_depth = 0;
  size_t bracket_depth = 0;
  size_t brace_depth = 0;

  while (true) {
    TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
    if (tok.type == TokenType::EndOfFile) {
      return std::monostate{};
    }

    const bool at_top_level =
        paren_depth == 0 && bracket_depth == 0 && brace_depth == 0;
    if (at_top_level) {
      if (tok.type == TokenType::RBrace) {
        if (in_compound_stmt) {
          return std::monostate{};
        }
        context.Diagnostics().Error(tok.location,
                                    errs::UnexpectedClosingBrace{});
        lexer.ConsumeToken(tok);
        return std::monostate{};
      }
      if (tok.type == TokenType::Semicolon) {
        lexer.ConsumeToken(tok);
        return std::monostate{};
      }
      if (IsStatementStart(tok, context)) {
        return std::monostate{};
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

static std::optional<std::unique_ptr<ASTNode>>
ParseControlCondExpr(Lexer &lexer, CompileContext &context) {
  Token next;
  TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
  if (tok.type != TokenType::LParen) {
    ReportParserError(context, tok.location, errs::ExpectedLParen{});
    return std::nullopt;
  }
  lexer.ConsumeToken(tok);
  TRY_ASSIGN_NEW(expression, ParseExpr(lexer, context));
  TRY_ASSIGN(tok, lexer.PeekNextToken());
  if (tok.type != TokenType::RParen) {
    ReportParserError(context, tok.location, errs::ExpectedRParen{});
    return std::nullopt;
  }
  lexer.ConsumeToken(tok);
  return expression;
}

static OptUnique<ASTNode> ParseIfStmt(Lexer &lexer, CompileContext &context);

static OptUnique<WhileStmt> ParseWhileStmt(Lexer &lexer,
                                           CompileContext &context);

static OptUnique<DoWhileStmt> ParseDoWhileStmt(Lexer &lexer,
                                               CompileContext &context);

static OptUnique<ForStmt> ParseForStmt(Lexer &lexer, CompileContext &context);

static OptUnique<SwitchStmt> ParseSwitchStmt(Lexer &lexer,
                                             CompileContext &context);

static OptUnique<ASTNode> ParseReturnStmt(Lexer &lexer,
                                          CompileContext &context);

static OptUnique<ASTNode> ParseGotoStmt(Lexer &lexer, CompileContext &context);

static std::optional<std::unique_ptr<ASTNode>>
ParseStmtOnly(Lexer &lexer, CompileContext &context) {
  std::unique_ptr<ASTNode> stmt;

  Token next;
  TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
  if (IsDeclarationSpecifier(tok, context)) {
    TRY_ASSIGN_NEW(declaration, ParseDeclaration(lexer, context));
    stmt = std::move(declaration);
  } else {
    switch (tok.type) {
    case TokenType::KwIf: {
      TRY_ASSIGN_NEW(statement, ParseIfStmt(lexer, context));
      stmt = std::move(statement);
      break;
    }
    case TokenType::KwWhile: {
      TRY_ASSIGN_NEW(statement, ParseWhileStmt(lexer, context));
      stmt = std::move(statement);
      break;
    }
    case TokenType::KwDo: {
      TRY_ASSIGN_NEW(statement, ParseDoWhileStmt(lexer, context));
      stmt = std::move(statement);
      break;
    }
    case TokenType::KwFor: {
      TRY_ASSIGN_NEW(statement, ParseForStmt(lexer, context));
      stmt = std::move(statement);
      break;
    }
    case TokenType::KwSwitch: {
      TRY_ASSIGN_NEW(statement, ParseSwitchStmt(lexer, context));
      stmt = std::move(statement);
      break;
    }
    case TokenType::KwReturn: {
      TRY_ASSIGN_NEW(statement, ParseReturnStmt(lexer, context));
      stmt = std::move(statement);
      break;
    }
    case TokenType::KwGoto: {
      TRY_ASSIGN_NEW(statement, ParseGotoStmt(lexer, context));
      stmt = std::move(statement);
      break;
    }
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
      TRY_ASSIGN_NEW(next_token, lexer.PeekNextToken(1));
      if (next_token.type == TokenType::Colon) {
        lexer.ConsumeToken(tok);
        TRY_ASSIGN(next, lexer.PeekNextToken());
        Token colon = next;
        if (colon.type != TokenType::Colon) {
          ReportParserError(context, colon.location,
                            errs::ExpectedLabelColon{});
          return std::nullopt;
        }
        lexer.ConsumeToken(colon);
        auto label_stmt = std::make_unique<LabelStmt>();
        label_stmt->location = CombineLocation(tok.location, colon.location);
        label_stmt->name = context.Sources().GetText(tok);
        context.AddLabel(label_stmt->name);
        stmt = std::move(label_stmt);
      } else {
        TRY_ASSIGN_NEW(expression, ParseExpr(lexer, context));
        stmt = std::move(expression);
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
static std::optional<std::unique_ptr<ASTNode>>
ParseStmt(Lexer &lexer, CompileContext &context) {
  Token next;
  TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
  if (tok.type == TokenType::LBrace) {
    TRY_ASSIGN_NEW(compound_stmt, ParseCompoundStmt(lexer, context));
    return std::move(compound_stmt);
  }
  if (tok.type == TokenType::RBrace) {
    return std::unique_ptr<ASTNode>{};
  }
  if (tok.type == TokenType::EndOfFile) {
    return std::unique_ptr<ASTNode>{};
  }
  if (tok.type == TokenType::Semicolon) {
    lexer.ConsumeToken(tok);
    auto empty_stmt = std::make_unique<EmptyStmt>();
    empty_stmt->location = tok.location;
    return empty_stmt;
  }

  TRY_ASSIGN_NEW(stmt, ParseStmtOnly(lexer, context));

  if (StmtNeedsSemicolon(stmt.get())) {
    TRY_ASSIGN(next, lexer.PeekNextToken());
    if (next.type == TokenType::Semicolon) {
      lexer.ConsumeToken(next);
    } else {
      context.Diagnostics().Error(next.location,
                                  errs::ExpectedSemicolonAfterStatement{});
      if (next.type != TokenType::EndOfFile && next.type != TokenType::RBrace &&
          !IsStatementStart(next, context)) {
        context.RequestParserRecovery();
        return std::nullopt;
      }
    }
  }

  return stmt;
}

OptUnique<CompoundStmt> ParseCompoundStmt(Lexer &lexer,
                                          CompileContext &context) {
  Token next;
  TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
  if (tok.type != TokenType::LBrace) {
    ReportParserError(context, tok.location, errs::ExpectedLBrace{});
    return std::nullopt;
  }
  lexer.ConsumeToken(tok);
  auto CS = std::make_unique<CompoundStmt>();
  SourceLocation start_location = tok.location;
  CompileContextLayerGuard layer_guard(context);
  while (true) {
    TRY_ASSIGN(tok, lexer.PeekNextToken());
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
    std::optional<std::unique_ptr<ASTNode>> stmt = ParseStmt(lexer, context);
    if (!stmt) {
      if (!context.ConsumeParserRecovery()) {
        return std::nullopt;
      }
      TRY(SynchronizeToStatementBoundary(lexer, context, true));
      TRY_ASSIGN(next, lexer.PeekNextToken());
      // A boundary token may also be the token that triggered the error.
      if (lexer.GetConsumedTokenCount() == previous_consumed_count &&
          next.type != TokenType::RBrace && next.type != TokenType::EndOfFile) {
        lexer.ConsumeToken(next);
      }
      continue;
    }
    CS->AddStmt(std::move(*stmt));
  }
  return CS;
}

static OptUnique<ASTNode> ParseIfStmt(Lexer &lexer, CompileContext &context) {
  Token next;
  TRY_ASSIGN_NEW(start_token, lexer.PeekNextToken());
  auto if_stmt = std::make_unique<IfStmt>();
  TRY(lexer.ConsumeNextToken()); // consume "if"
  TRY_ASSIGN_NEW(condition, ParseControlCondExpr(lexer, context));
  if_stmt->condition = std::move(condition);
  {
    CompileContextLayerGuard guard(context);
    TRY_ASSIGN_NEW(branch, ParseStmt(lexer, context));
    if_stmt->then_branch = std::move(branch);
  }
  TRY_ASSIGN(next, lexer.PeekNextToken());
  Token tok = next;
  if (tok.type == TokenType::KwElse) {
    lexer.ConsumeToken(tok);
    CompileContextLayerGuard guard(context);
    TRY_ASSIGN_NEW(branch, ParseStmt(lexer, context));
    if_stmt->else_branch = std::move(branch);
  }
  SourceLocation end_location =
      if_stmt->else_branch
          ? if_stmt->else_branch->location
          : (if_stmt->then_branch ? if_stmt->then_branch->location
                                  : lexer.GetLastConsumedLocation());
  if_stmt->location = CombineLocation(start_token.location, end_location);
  return if_stmt;
}

static OptUnique<WhileStmt> ParseWhileStmt(Lexer &lexer,
                                           CompileContext &context) {
  TRY_ASSIGN_NEW(start_token, lexer.PeekNextToken());
  auto while_stmt = std::make_unique<WhileStmt>();
  TRY(lexer.ConsumeNextToken()); // consume "while"
  TRY_ASSIGN_NEW(condition, ParseControlCondExpr(lexer, context));
  while_stmt->condition = std::move(condition);
  {
    CompileContextLayerGuard guard(context);
    TRY_ASSIGN_NEW(body, ParseStmt(lexer, context));
    while_stmt->body = std::move(body);
  }
  SourceLocation end_location = while_stmt->body
                                    ? while_stmt->body->location
                                    : lexer.GetLastConsumedLocation();
  while_stmt->location = CombineLocation(start_token.location, end_location);
  return while_stmt;
}

static OptUnique<DoWhileStmt> ParseDoWhileStmt(Lexer &lexer,
                                               CompileContext &context) {
  Token next;
  TRY_ASSIGN_NEW(start_token, lexer.PeekNextToken());
  auto do_while_stmt = std::make_unique<DoWhileStmt>();
  TRY(lexer.ConsumeNextToken()); // consume "do"
  {
    CompileContextLayerGuard guard(context);
    TRY_ASSIGN_NEW(body, ParseStmt(lexer, context));
    do_while_stmt->body = std::move(body);
  }
  TRY_ASSIGN(next, lexer.PeekNextToken());
  Token while_token = next;
  if (while_token.type != TokenType::KwWhile) {
    ReportParserError(context, while_token.location,
                      errs::ExpectedWhileAfterDo{});
    return std::nullopt;
  }
  lexer.ConsumeToken(while_token);
  TRY_ASSIGN_NEW(condition, ParseControlCondExpr(lexer, context));
  do_while_stmt->condition = std::move(condition);
  do_while_stmt->location =
      CombineLocation(start_token.location, lexer.GetLastConsumedLocation());
  return do_while_stmt;
}

static OptUnique<ForStmt> ParseForStmt(Lexer &lexer, CompileContext &context) {
  Token next;
  TRY_ASSIGN_NEW(start_token, lexer.PeekNextToken());
  auto for_stmt = std::make_unique<ForStmt>();
  TRY(lexer.ConsumeNextToken());
  TRY_ASSIGN(next, lexer.PeekNextToken());
  Token lparen = next;
  if (lparen.type != TokenType::LParen) {
    ReportParserError(context, lparen.location, errs::ExpectedLParen{});
    return std::nullopt;
  }
  lexer.ConsumeToken(lparen);
  CompileContextLayerGuard guard(context);

  TRY_ASSIGN(next, lexer.PeekNextToken());
  if (next.type == TokenType::Semicolon) {
    for_stmt->init = nullptr;
  } else {
    TRY_ASSIGN_NEW(init, ParseStmtOnly(lexer, context));
    for_stmt->init = std::move(init);
  }
  TRY_ASSIGN(next, lexer.PeekNextToken());
  Token semicolon = next;
  if (semicolon.type != TokenType::Semicolon) {
    ReportParserError(context, semicolon.location, errs::ExpectedSemicolon{});
    return std::nullopt;
  }
  lexer.ConsumeToken(semicolon);

  TRY_ASSIGN(next, lexer.PeekNextToken());
  if (next.type == TokenType::Semicolon) {
    for_stmt->condition = nullptr;
  } else {
    TRY_ASSIGN_NEW(condition, ParseStmtOnly(lexer, context));
    for_stmt->condition = std::move(condition);
  }
  TRY_ASSIGN(next, lexer.PeekNextToken());
  semicolon = next;
  if (semicolon.type != TokenType::Semicolon) {
    ReportParserError(context, semicolon.location, errs::ExpectedSemicolon{});
    return std::nullopt;
  }
  lexer.ConsumeToken(semicolon);

  TRY_ASSIGN(next, lexer.PeekNextToken());
  if (next.type == TokenType::RParen) {
    for_stmt->increment = nullptr;
  } else {
    TRY_ASSIGN_NEW(increment, ParseStmtOnly(lexer, context));
    for_stmt->increment = std::move(increment);
  }

  TRY_ASSIGN(next, lexer.PeekNextToken());
  Token rparen = next;
  if (rparen.type != TokenType::RParen) {
    ReportParserError(context, rparen.location, errs::ExpectedRParen{});
    return std::nullopt;
  }
  lexer.ConsumeToken(rparen);
  {
    CompileContextLayerGuard body_guard(context);
    TRY_ASSIGN_NEW(body, ParseStmt(lexer, context));
    for_stmt->body = std::move(body);
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

static OptUnique<SwitchStmt> ParseSwitchStmt(Lexer &lexer,
                                             CompileContext &context) {
  Token next;
  TRY_ASSIGN_NEW(start_token, lexer.PeekNextToken());
  auto switch_stmt = std::make_unique<SwitchStmt>();
  TRY(lexer.ConsumeNextToken()); // consume "switch"
  TRY_ASSIGN_NEW(value, ParseControlCondExpr(lexer, context));
  switch_stmt->value = std::move(value);
  TRY_ASSIGN(next, lexer.PeekNextToken());
  if (next.type == TokenType::Semicolon) {
    switch_stmt->location =
        CombineLocation(start_token.location, lexer.GetLastConsumedLocation());
    return switch_stmt;
  } else {
    Token lbrace = next;
    if (lbrace.type != TokenType::LBrace) {
      ReportParserError(context, lbrace.location, errs::ExpectedLBrace{});
      return std::nullopt;
    }
    lexer.ConsumeToken(lbrace);
    CompileContextLayerGuard guard(context);
    while (true) {
      TRY_ASSIGN(next, lexer.PeekNextToken());
      Token tok = next;
      switch (tok.type) {
      case TokenType::KwCase: {
        Token case_start = tok;
        SourceLocation case_end = tok.location;
        auto case_stmt = std::make_unique<CaseStmt>();
        while (true) {
          TRY_ASSIGN(next, lexer.PeekNextToken());
          if (next.type != TokenType::KwCase) {
            break;
          }
          TRY(lexer.ConsumeNextToken());
          TRY_ASSIGN_NEW(case_value, ParseAssignmentExpr(lexer, context));
          case_stmt->case_vals.push_back(std::move(case_value));
          case_end = case_stmt->case_vals.back()->location;
          TRY_ASSIGN(next, lexer.PeekNextToken());
          Token colon = next;
          if (colon.type != TokenType::Colon) {
            ReportParserError(context, colon.location, errs::ExpectedColon{});
            return std::nullopt;
          }
          case_end = colon.location;
          lexer.ConsumeToken(colon);
        }
        do {
          TRY_ASSIGN_NEW(statement, ParseStmt(lexer, context));
          case_stmt->body.push_back(std::move(statement));
          TRY_ASSIGN(next, lexer.PeekNextToken());
        } while (!IsCaseEnd(next.type));
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
        TRY_ASSIGN(next, lexer.PeekNextToken());
        Token colon = next;
        if (colon.type != TokenType::Colon) {
          ReportParserError(context, colon.location, errs::ExpectedColon{});
          return std::nullopt;
        }
        default_end = colon.location;
        lexer.ConsumeToken(colon);
        do {
          TRY_ASSIGN_NEW(statement, ParseStmt(lexer, context));
          default_stmt->body.push_back(std::move(statement));
          TRY_ASSIGN(next, lexer.PeekNextToken());
        } while (!IsCaseEnd(next.type));
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
        ReportParserError(context, tok.location,
                          errs::UnexpectedSwitchToken{tok.type});
        return std::nullopt;
      }
    }
  }
}

static OptUnique<ASTNode> ParseReturnStmt(Lexer &lexer,
                                          CompileContext &context) {
  TRY_ASSIGN_NEW(start_token, lexer.PeekNextToken());
  auto ret_stmt = std::make_unique<ReturnStmt>();
  TRY(lexer.ConsumeNextToken()); // consume "return"
  TRY_ASSIGN_NEW(value, ParseExpr(lexer, context));
  ret_stmt->ret_value = std::move(value);
  ret_stmt->location = CombineLocation(
      start_token.location, ret_stmt->ret_value ? ret_stmt->ret_value->location
                                                : start_token.location);
  return ret_stmt;
}

static OptUnique<ASTNode> ParseGotoStmt(Lexer &lexer, CompileContext &context) {
  Token next;
  TRY_ASSIGN_NEW(start_token, lexer.PeekNextToken());
  auto goto_stmt = std::make_unique<GotoStmt>();
  TRY(lexer.ConsumeNextToken()); // consume "goto"
  TRY_ASSIGN(next, lexer.PeekNextToken());
  Token tok = next;
  if (tok.type != TokenType::Identifier) {
    ReportParserError(context, tok.location, errs::ExpectedGotoIdentifier{});
    return std::nullopt;
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

OptUnique<TranslationUnit>
ParseTranslationUnit(Lexer &lexer, TypeContext &types, SourceManager &sources,
                     DiagnosticContext &diagnostics) {
  std::unique_ptr<TranslationUnit> TU = std::make_unique<TranslationUnit>();
  Token next;
  TRY_ASSIGN_NEW(first_token, lexer.PeekNextToken());
  SourceLocation translation_unit_location = first_token.location;
  bool has_decls = false;
  CompileContext context(types, sources, diagnostics);
  context.PushLayer();
  AddVaList(context);
  while (true) {
    TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
    if (tok.type == TokenType::EndOfFile) {
      break;
    }
    if (tok.type == TokenType::RBrace) {
      context.Diagnostics().Error(tok.location, errs::UnexpectedClosingBrace{});
      lexer.ConsumeToken(tok);
      continue;
    }
    size_t previous_consumed_count = lexer.GetConsumedTokenCount();
    std::optional<std::unique_ptr<ASTNode>> stmt = ParseStmt(lexer, context);
    if (!stmt) {
      if (!context.ConsumeParserRecovery()) {
        return std::nullopt;
      }
      TRY(SynchronizeToStatementBoundary(lexer, context, false));
      TRY_ASSIGN(next, lexer.PeekNextToken());
      // Force progress when synchronization stopped at the failing token.
      if (lexer.GetConsumedTokenCount() == previous_consumed_count &&
          next.type != TokenType::RBrace && next.type != TokenType::EndOfFile) {
        lexer.ConsumeToken(next);
      }
      continue;
    }
    if (*stmt && (*stmt)->GetType() != ASTNodeType::EmptyStmt) {
      translation_unit_location =
          CombineLocation(translation_unit_location, (*stmt)->location);
      has_decls = true;
      TU->decls.push_back(std::move(*stmt));
    }
  }
  if (!has_decls) {
    translation_unit_location = first_token.location;
  }
  TU->location = translation_unit_location;
  return TU;
}

AST_PRINT_CHILDREN_IMPL(TranslationUnit, AST_CHILDREN(decls))
