#include "TranslationUnit.h"
#include "../Exception.h"
#include "Context.h"
#include "Declaration.h"
#include "Expr.h"

static std::unique_ptr<FunctionDecl>
ParseFunctionDecl(Lexer &lexer, ParsedType type, StorageClass storage_class,
                  CompileContext &context);

std::unique_ptr<StructDecl> ParseStructDecl(Lexer &lexer, std::string_view name,
                                            CompileContext &context);

std::unique_ptr<EnumDecl> ParseEnumDecl(Lexer &lexer, std::string_view name,
                                        CompileContext &context);

std::unique_ptr<CompoundStmt> ParseCompoundStmt(Lexer &lexer,
                                                CompileContext &context);

static std::unique_ptr<ASTNode> ParseControlCondExpr(Lexer &lexer,
                                                     CompileContext &context) {
  Token tok = lexer.PeekNextToken();
  if (tok.type != TokenType::LParen) {
    context.Diagnostics().RaiseError(tok.location, "Expected '('");
  }
  lexer.ConsumeToken(tok);
  auto expr = ParseExpr(lexer, context);
  tok = lexer.PeekNextToken();
  if (tok.type != TokenType::RParen) {
    context.Diagnostics().RaiseError(tok.location, "Expected ')'");
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
    auto decl = ParseDeclaration(lexer, context);
    auto decl_types = ParseType(decl.get(), context);
    auto type_spec = std::move(decl->declaration_specifier.type_spec);
    const StorageClass storage_class =
        decl->declaration_specifier.storage_class;

    DeclStmtBuilder decl_stmt_builder;
    if (decl->declaration_specifier.decl) {
      decl_stmt_builder.Add(std::move(decl->declaration_specifier.decl));
    }

    if (type_spec.is_typedef) {
      if (storage_class != StorageClass::None) {
        context.Diagnostics().RaiseError(
            type_spec.location, "A typedef cannot have a storage class");
      }
      for (auto &parsed_type : decl_types) {
        if (!parsed_type.declarator_name.empty()) {
          auto typedef_decl = std::make_unique<TypedefDecl>();
          typedef_decl->location = parsed_type.location;
          typedef_decl->name = parsed_type.declarator_name;
          typedef_decl->type = parsed_type.type;
          context.AddTypedef(typedef_decl->name, parsed_type.type);
          decl_stmt_builder.Add(std::move(typedef_decl));
        }
      }
    } else {
      if (storage_class != StorageClass::None &&
          decl->init_declarators.empty()) {
        context.Diagnostics().RaiseError(
            type_spec.location,
            "A storage class requires a declared object or function");
      }
      for (size_t i = 0; i < decl_types.size(); i++) {
        auto &parsed_type = decl_types[i];
        auto &type = parsed_type.type;

        switch (type->GetType()) {
        case TypeBaseType::BuiltinType:
        case TypeBaseType::StructType:
        case TypeBaseType::UnionType:
        case TypeBaseType::EnumType:
        case TypeBaseType::PointerType:
        case TypeBaseType::ArrayType:
          if (!parsed_type.declarator_name.empty()) {
            auto var_decl = std::make_unique<VarDecl>();
            var_decl->location = parsed_type.location;
            var_decl->name = parsed_type.declarator_name;
            var_decl->type = type;
            var_decl->storage_class = storage_class;
            var_decl->initializer =
                std::move(decl->init_declarators[i]->initializer);
            context.AddVariable(var_decl->name, type);
            decl_stmt_builder.Add(std::move(var_decl));
          } else {
            Token next = lexer.PeekNextToken();
            if (next.type != TokenType::Semicolon) {
              context.Diagnostics().RaiseError(
                  next.location, "Expected semicolon after declaration");
            }
          }
          break;
        case TypeBaseType::FunctionType:
          context.AddFunction(parsed_type.declarator_name, type);
          decl_stmt_builder.Add(ParseFunctionDecl(lexer, std::move(parsed_type),
                                                  storage_class, context));
          break;
        }
      }
    }
    stmt = decl_stmt_builder.Build();
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
        Token colon = lexer.ConsumeNextToken();
        if (colon.type != TokenType::Colon) {
          context.Diagnostics().RaiseError(colon.location,
                                           "Expected ':' after label");
        }
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

  if (StmtNeedsSemicolon(stmt.get()) &&
      lexer.PeekNextToken().type == TokenType::Semicolon) {
    lexer.ConsumeNextToken();
  }

  return stmt;
}

std::unique_ptr<CompoundStmt> ParseCompoundStmt(Lexer &lexer,
                                                CompileContext &context) {
  auto tok = lexer.PeekNextToken();
  if (tok.type != TokenType::LBrace) {
    context.Diagnostics().RaiseError(tok.location, "Expected '{'");
  }
  lexer.ConsumeToken(tok);
  auto CS = std::make_unique<CompoundStmt>();
  SourceLocation start_location = tok.location;
  CompileContextLayerGuard layer_guard(context);
  while (true) {
    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::EndOfFile) {
      context.Diagnostics().RaiseError(tok.location, "Expected '}'");
    }
    if (tok.type == TokenType::RBrace) {
      lexer.ConsumeToken(tok);
      CS->location = CombineLocation(start_location, tok.location);
      break;
    }
    CS->AddStmt(ParseStmt(lexer, context));
  }
  return CS;
}

static std::unique_ptr<ASTNode> ParseIfStmt(Lexer &lexer,
                                            CompileContext &context) {
  Token start_token = lexer.PeekNextToken();
  auto if_stmt = std::make_unique<IfStmt>();
  lexer.ConsumeNextToken(); // consume "if"
  if_stmt->condition = ParseControlCondExpr(lexer, context);
  if_stmt->then_branch = ParseStmt(lexer, context);
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::KwElse) {
    lexer.ConsumeToken(tok);
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
  while_stmt->body = ParseStmt(lexer, context);
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
  do_while_stmt->body = ParseStmt(lexer, context);
  Token while_token = lexer.PeekNextToken();
  if (while_token.type != TokenType::KwWhile) {
    context.Diagnostics().RaiseError(while_token.location,
                                     "Expected 'while' after 'do' statement");
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
    context.Diagnostics().RaiseError(lparen.location, "Expected '('");
  }
  lexer.ConsumeToken(lparen);

  if (lexer.PeekNextToken().type == TokenType::Semicolon) {
    for_stmt->init = nullptr;
  } else {
    for_stmt->init = ParseStmtOnly(lexer, context);
  }
  Token semicolon = lexer.PeekNextToken();
  if (semicolon.type != TokenType::Semicolon) {
    context.Diagnostics().RaiseError(semicolon.location, "Expected ';'");
  }
  lexer.ConsumeToken(semicolon);

  if (lexer.PeekNextToken().type == TokenType::Semicolon) {
    for_stmt->condition = nullptr;
  } else {
    for_stmt->condition = ParseStmtOnly(lexer, context);
  }
  semicolon = lexer.PeekNextToken();
  if (semicolon.type != TokenType::Semicolon) {
    context.Diagnostics().RaiseError(semicolon.location, "Expected ';'");
  }
  lexer.ConsumeToken(semicolon);

  if (lexer.PeekNextToken().type == TokenType::RParen) {
    for_stmt->increment = nullptr;
  } else {
    for_stmt->increment = ParseStmtOnly(lexer, context);
  }

  Token rparen = lexer.PeekNextToken();
  if (rparen.type != TokenType::RParen) {
    context.Diagnostics().RaiseError(rparen.location, "Expected ')'");
  }
  lexer.ConsumeToken(rparen);
  for_stmt->body = ParseStmt(lexer, context);
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
      context.Diagnostics().RaiseError(lbrace.location, "Expected '{'");
    }
    lexer.ConsumeToken(lbrace);
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
            context.Diagnostics().RaiseError(colon.location, "Expected ':'");
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
          context.Diagnostics().RaiseError(colon.location, "Expected ':'");
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
        context.Diagnostics().RaiseError(
            tok.location, "Unexpected token in switch statement: ", tok.type);
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
  Token tok = lexer.ConsumeNextToken();
  if (tok.type != TokenType::Identifier) {
    context.Diagnostics().RaiseError(tok.location,
                                     "Expected identifier after goto");
  }
  goto_stmt->target = context.Sources().GetText(tok);
  goto_stmt->location = CombineLocation(start_token.location, tok.location);
  return goto_stmt;
}

std::unique_ptr<StructDecl> ParseStructDecl(Lexer &lexer, std::string_view name,
                                            CompileContext &context) {
  auto SD = std::make_unique<StructDecl>();
  SD->name = name;
  SD->type = context.Types().GetOrCreateStruct(context.CurrentScopeId(), name);
  context.AddStruct(name, SD->type);
  if (lexer.PeekNextToken().type == TokenType::LBrace) {
    SD->fields = ParseCompoundStmt(lexer, context);
    std::vector<StructField> fields;
    for (const auto &statement : SD->fields->statements) {
      if (statement->GetType() == ASTNodeType::DeclStmt) {
        for (const auto &decl : statement->As<DeclStmt>()->decls) {
          if (decl->GetType() != ASTNodeType::VarDecl) {
            context.Diagnostics().RaiseError(
                decl->location, "Struct fields must be variable declarations");
          }
          auto *field = decl->As<VarDecl>();
          if (field->storage_class != StorageClass::None) {
            context.Diagnostics().RaiseError(
                field->location, "A struct field cannot have a storage class");
          }
          fields.push_back(
              {.name = std::string(field->name), .type = field->type});
        }
      } else {
        if (statement->GetType() != ASTNodeType::VarDecl) {
          context.Diagnostics().RaiseError(
              statement->location,
              "Struct fields must be variable declarations");
        }
        auto *field = statement->As<VarDecl>();
        if (field->storage_class != StorageClass::None) {
          context.Diagnostics().RaiseError(
              field->location, "A struct field cannot have a storage class");
        }
        fields.push_back(
            {.name = std::string(field->name), .type = field->type});
      }
    }
    context.Types().CompleteStruct(SD->type, fields);
  } else {
    Token tok = lexer.PeekNextToken();
    if (tok.type != TokenType::Semicolon) {
      context.Diagnostics().RaiseError(tok.location, "Expected ';'");
    }
  }
  return SD;
}

std::unique_ptr<UnionDecl> ParseUnionDecl(Lexer &lexer, std::string_view name,
                                          CompileContext &context) {
  auto UD = std::make_unique<UnionDecl>();
  UD->name = name;
  UD->type = context.Types().GetOrCreateUnion(context.CurrentScopeId(), name);
  context.AddUnion(name, UD->type);
  if (lexer.PeekNextToken().type == TokenType::LBrace) {
    UD->fields = ParseCompoundStmt(lexer, context);
    std::vector<UnionField> fields;
    auto add_field = [&](const ASTNode *decl) {
      if (auto field = decl->DynCast<VarDecl>()) {
        if (field->storage_class != StorageClass::None) {
          context.Diagnostics().RaiseError(
              field->location, "A union field cannot have a storage class");
        }
        fields.push_back({.name = field->name, .type = field->type});
      } else {
        if (decl->GetType() != ASTNodeType::StructDecl &&
            decl->GetType() != ASTNodeType::UnionDecl &&
            decl->GetType() != ASTNodeType::EnumDecl) {
          context.Diagnostics().RaiseError(
              decl->location, "Union fields must be variable declarations");
        }
      }
    };
    for (const auto &statement : UD->fields->statements) {
      if (statement->GetType() == ASTNodeType::DeclStmt) {
        for (const auto &decl : statement->As<DeclStmt>()->decls) {
          add_field(decl.get());
        }
      } else {
        add_field(statement.get());
      }
    }
    context.Types().CompleteUnion(UD->type, fields);
  } else {
    Token tok = lexer.PeekNextToken();
    if (tok.type != TokenType::Semicolon) {
      context.Diagnostics().RaiseError(tok.location, "Expected ';'");
    }
  }
  return UD;
}

static void ParseEnumContent(Lexer &lexer, CompileContext &context,
                             EnumDecl *decl) {
  while (true) {
    Token tok = lexer.PeekNextToken();
    if (tok.type == TokenType::RBrace) {
      lexer.ConsumeToken(tok);
      return;
    }
    if (tok.type == TokenType::EndOfFile) {
      context.Diagnostics().RaiseError(tok.location, "Expected '}' after enum");
    }

    if (tok.type != TokenType::Identifier) {
      context.Diagnostics().RaiseError(
          tok.location, "Expected identifier in enum declaration");
    }
    std::string_view name = context.Sources().GetText(tok);
    lexer.ConsumeToken(tok);

    auto elem_decl = std::make_unique<EnumElemDecl>();
    Token name_token = tok;
    elem_decl->location = name_token.location;
    elem_decl->name = name;
    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::Equal) {
      lexer.ConsumeToken(tok);
      elem_decl->value = ParseExpr(lexer, context);
      elem_decl->location =
          CombineLocation(name_token.location, elem_decl->value->location);
    }
    decl->values.push_back(std::move(elem_decl));
    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::Comma) {
      lexer.ConsumeToken(tok);
    }
  }
}

std::unique_ptr<EnumDecl> ParseEnumDecl(Lexer &lexer, std::string_view name,
                                        CompileContext &context) {
  auto ED = std::make_unique<EnumDecl>();
  ED->name = name;
  ED->type = context.Types().GetOrCreateEnum(context.CurrentScopeId(), name);
  context.AddEnum(name, ED->type);
  if (lexer.PeekNextToken().type == TokenType::Colon) {
    Token colon = lexer.ConsumeNextToken();
    TypeRef parsed_type = ParseType(lexer, context);
    if ((parsed_type.get_tag() & (TypeRef::TagType)(TypeTag::Const)) != 0) {
      context.Diagnostics().RaiseError(
          colon.location, "Unexpected specifier for enum underlying type");
    }
    if (!IsIntegerType(parsed_type)) {
      context.Diagnostics().RaiseError(
          colon.location, "Expected integer type for enum underlying type");
    }
    ED->underlying_type = parsed_type;
  } else {
    ED->underlying_type = context.Types().getInt();
  }
  if (lexer.PeekNextToken().type == TokenType::LBrace) {
    ED->is_definition = true;
    lexer.ConsumeNextToken();
    ParseEnumContent(lexer, context, ED.get());
  } else {
    Token tok = lexer.PeekNextToken();
    if (tok.type != TokenType::Semicolon) {
      context.Diagnostics().RaiseError(tok.location, "Expected ';'");
    }
  }
  return ED;
}

static std::unique_ptr<FunctionDecl>
ParseFunctionDecl(Lexer &lexer, ParsedType type, StorageClass storage_class,
                  CompileContext &context) {
  auto FD = std::make_unique<FunctionDecl>();
  FD->location = type.location;
  FD->name = type.declarator_name;
  FD->parameter_names = std::move(type.parameter_names);
  FD->storage_class = storage_class;
  context.PushLayer();
  FD->function_type = type.type;
  if (lexer.PeekNextToken().type == TokenType::LBrace) {
    FD->body = ParseCompoundStmt(lexer, context);
    FD->location = CombineLocation(FD->location, FD->body->location);
  } else {
    Token tok = lexer.PeekNextToken();
    if (tok.type != TokenType::Semicolon) {
      context.Diagnostics().RaiseError(tok.location,
                                       "Expected function body or ';'");
    }
  }
  context.PopLayer();
  return FD;
}

static void AddVaList(CompileContext &context) {
  auto *va_list_type = context.Types().GetOrCreateStruct(
      context.CurrentScopeId(), "__builtin_va_list_type");
  StructField fields[] = {
      {"", context.Types().getInt(), 0},
      {"", context.Types().Pointer(context.Types().getVoid()), 1},
      {"", context.Types().Pointer(context.Types().getVoid()), 2},
      {"", context.Types().getInt(), 3}};
  context.Types().CompleteStruct(va_list_type, fields);
  context.CurrentLayer().AddTypedef("va_list", va_list_type);
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
    std::unique_ptr<ASTNode> stmt = ParseStmt(lexer, context);
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
