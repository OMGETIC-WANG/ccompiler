#include "TranslationUnit.h"
#include "../Exception.h"
#include "Context.h"
#include "Declaration.h"
#include "Expr.h"

static std::unique_ptr<FunctionDecl>
ParseFunctionDecl(FilteredLexer &lexer, ParsedType type,
                  StorageClass storage_class, CompileContext &context);

std::unique_ptr<StructDecl> ParseStructDecl(FilteredLexer &lexer,
                                            std::string_view name,
                                            CompileContext &context);

std::unique_ptr<EnumDecl> ParseEnumDecl(FilteredLexer &lexer,
                                        std::string_view name,
                                        CompileContext &context);

std::unique_ptr<CompoundStmt> ParseCompoundStmt(FilteredLexer &lexer,
                                                CompileContext &context);

static std::unique_ptr<ASTNode> ParseControlCondExpr(FilteredLexer &lexer,
                                                     CompileContext &context) {
  Token tok = lexer.PeekNextToken();
  ASSERT_EQ(tok.type, TokenType::LParen, "Expected (");
  lexer.ConsumeToken(tok);
  auto expr = ParseExpr(lexer, context);
  tok = lexer.PeekNextToken();
  ASSERT_EQ(tok.type, TokenType::RParen, "Expected )");
  lexer.ConsumeToken(tok);
  return expr;
}

static std::unique_ptr<ASTNode> ParseIfStmt(FilteredLexer &lexer,
                                            CompileContext &context);

static std::unique_ptr<WhileStmt> ParseWhileStmt(FilteredLexer &lexer,
                                                 CompileContext &context);

static std::unique_ptr<ForStmt> ParseForStmt(FilteredLexer &lexer,
                                             CompileContext &context);

static std::unique_ptr<SwitchStmt> ParseSwitchStmt(FilteredLexer &lexer,
                                                   CompileContext &context);

static std::unique_ptr<ASTNode> ParseReturnStmt(FilteredLexer &lexer,
                                                CompileContext &context);

static std::unique_ptr<ASTNode> ParseGotoStmt(FilteredLexer &lexer,
                                              CompileContext &context);

static std::unique_ptr<ASTNode> ParseStmtOnly(FilteredLexer &lexer,
                                              CompileContext &context) {
  std::unique_ptr<ASTNode> stmt;
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::RawLLVMIR) {
    lexer.ConsumeToken(tok);
    auto raw_ir = std::make_unique<RawLLVMIR>();
    size_t start = begin_raw_ir_label.size();
    size_t end = tok.value.size() - end_raw_ir_label.size();
    raw_ir->ir = tok.value.substr(start + 1, end - start - 1);
    stmt = std::move(raw_ir);
  } else if (IsDeclarationSpecifier(tok, context)) {
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
      ASSERT(storage_class == StorageClass::None,
             "A typedef cannot have a storage class");
      for (auto &parsed_type : decl_types) {
        if (!parsed_type.declarator_name.empty()) {
          auto typedef_decl = std::make_unique<TypedefDecl>();
          typedef_decl->name = parsed_type.declarator_name;
          typedef_decl->type = parsed_type.type;
          context.AddTypedef(typedef_decl->name, parsed_type.type);
          decl_stmt_builder.Add(std::move(typedef_decl));
        }
      }
    } else {
      ASSERT(storage_class == StorageClass::None ||
                 !decl->init_declarators.empty(),
             "A storage class requires a declared object or function");
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
            var_decl->name = parsed_type.declarator_name;
            var_decl->type = type;
            var_decl->storage_class = storage_class;
            var_decl->initializer =
                std::move(decl->init_declarators[i]->initializer);
            context.AddVariable(var_decl->name, type);
            decl_stmt_builder.Add(std::move(var_decl));
          } else {
            ASSERT_EQ(lexer.PeekNextToken().type, TokenType::Semicolon,
                      "Expected semicolon after declaration");
            lexer.ConsumeNextToken();
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
      break;
    case TokenType::KwContinue:
      lexer.ConsumeToken(tok);
      stmt = std::make_unique<ContinueStmt>();
      break;
    default: {
      Token next_tok = lexer.PeekNextToken(1);
      if (next_tok.type == TokenType::Colon) {
        lexer.ConsumeTokens(2);
        auto label_stmt = std::make_unique<LabelStmt>();
        label_stmt->name = tok.value;
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

// Parse one statement or a compound statement.
static std::unique_ptr<ASTNode> ParseStmt(FilteredLexer &lexer,
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

  while (lexer.PeekNextToken().type == TokenType::Semicolon) {
    lexer.ConsumeNextToken();
  }

  std::unique_ptr<ASTNode> stmt = ParseStmtOnly(lexer, context);

  while (lexer.PeekNextToken().type == TokenType::Semicolon)
    lexer.ConsumeNextToken();

  return stmt;
}

std::unique_ptr<CompoundStmt> ParseCompoundStmt(FilteredLexer &lexer,
                                                CompileContext &context) {
  auto tok = lexer.PeekNextToken();
  ASSERT_EQ(tok.type, TokenType::LBrace, "Expected {");
  lexer.ConsumeToken(tok);
  auto CS = std::make_unique<CompoundStmt>();
  CompileContextLayerGuard layer_guard(context);
  while (true) {
    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::EndOfFile) {
      return CS;
    }
    if (tok.type == TokenType::RBrace) {
      lexer.ConsumeToken(tok);
      break;
    }
    CS->statements.push_back(ParseStmt(lexer, context));
  }
  return CS;
}

static std::unique_ptr<ASTNode> TryParseStmt(FilteredLexer &lexer,
                                             CompileContext &context) {
  if (lexer.PeekNextToken().type == TokenType::Semicolon) {
    lexer.ConsumeNextToken();
    return std::make_unique<EmptyStmt>();
  } else {
    return ParseStmt(lexer, context);
  }
}

static std::unique_ptr<ASTNode> ParseIfStmt(FilteredLexer &lexer,
                                            CompileContext &context) {
  auto if_stmt = std::make_unique<IfStmt>();
  lexer.ConsumeNextToken(); // consume "if"
  if_stmt->condition = ParseControlCondExpr(lexer, context);
  if_stmt->then_branch = TryParseStmt(lexer, context);
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::KwElse) {
    lexer.ConsumeToken(tok);
    if_stmt->else_branch = TryParseStmt(lexer, context);
  }
  return if_stmt;
}

static std::unique_ptr<WhileStmt> ParseWhileStmt(FilteredLexer &lexer,
                                                 CompileContext &context) {
  auto while_stmt = std::make_unique<WhileStmt>();
  lexer.ConsumeNextToken(); // consume "while"
  while_stmt->condition = ParseControlCondExpr(lexer, context);
  while_stmt->body = TryParseStmt(lexer, context);
  return while_stmt;
}

static std::unique_ptr<ForStmt> ParseForStmt(FilteredLexer &lexer,
                                             CompileContext &context) {
  auto for_stmt = std::make_unique<ForStmt>();
  lexer.ConsumeNextToken();
  ASSERT_EQ(lexer.PeekNextToken().type, TokenType::LParen, "Expected a '('");
  lexer.ConsumeNextToken();

  if (lexer.PeekNextToken().type == TokenType::Semicolon) {
    for_stmt->init = nullptr;
  } else {
    for_stmt->init = ParseStmtOnly(lexer, context);
  }
  ASSERT_EQ(lexer.PeekNextToken().type, TokenType::Semicolon, "Expect a ';'");
  lexer.ConsumeNextToken();

  if (lexer.PeekNextToken().type == TokenType::Semicolon) {
    for_stmt->condition = nullptr;
  } else {
    for_stmt->condition = ParseStmtOnly(lexer, context);
  }
  ASSERT_EQ(lexer.PeekNextToken().type, TokenType::Semicolon, "Expect a ';'");
  lexer.ConsumeNextToken();

  if (lexer.PeekNextToken().type == TokenType::Semicolon) {
    for_stmt->increment = nullptr;
  } else {
    for_stmt->increment = ParseStmtOnly(lexer, context);
  }

  ASSERT_EQ(lexer.PeekNextToken().type, TokenType::RParen, "Expected a ')'");
  lexer.ConsumeNextToken();
  for_stmt->body = TryParseStmt(lexer, context);
  return for_stmt;
}

static bool IsCaseEnd(TokenType type) {
  return type == TokenType::KwCase || type == TokenType::KwDefault ||
         type == TokenType::RBrace;
}

static std::unique_ptr<SwitchStmt> ParseSwitchStmt(FilteredLexer &lexer,
                                                   CompileContext &context) {
  auto switch_stmt = std::make_unique<SwitchStmt>();
  lexer.ConsumeNextToken(); // consume "switch"
  switch_stmt->value = ParseControlCondExpr(lexer, context);
  if (lexer.PeekNextToken().type == TokenType::Semicolon) {
    lexer.ConsumeNextToken();
    return switch_stmt;
  } else {
    ASSERT_EQ(lexer.PeekNextToken().type, TokenType::LBrace, "Expected a '{'");
    lexer.ConsumeNextToken();
    while (true) {
      Token tok = lexer.PeekNextToken();
      switch (tok.type) {
      case TokenType::KwCase: {
        auto case_stmt = std::make_unique<CaseStmt>();
        while (lexer.PeekNextToken().type == TokenType::KwCase) {
          lexer.ConsumeNextToken();
          case_stmt->case_vals.push_back(ParseExpr(lexer, context));
          ASSERT_EQ(lexer.PeekNextToken().type, TokenType::Colon,
                    "Expected a ':'");
          lexer.ConsumeNextToken();
        }
        do {
          case_stmt->body.push_back(TryParseStmt(lexer, context));
        } while (!IsCaseEnd(lexer.PeekNextToken().type));
        switch_stmt->cases.push_back(std::move(case_stmt));
        break;
      }
      case TokenType::KwDefault: {
        lexer.ConsumeToken(tok);
        auto default_stmt = std::make_unique<DefaultStmt>();
        ASSERT_EQ(lexer.PeekNextToken().type, TokenType::Colon,
                  "Expected a ':'");
        lexer.ConsumeNextToken();
        do {
          default_stmt->body.push_back(TryParseStmt(lexer, context));
        } while (!IsCaseEnd(lexer.PeekNextToken().type));
        switch_stmt->cases.push_back(std::move(default_stmt));
        break;
      }
      case TokenType::RBrace:
        lexer.ConsumeToken(tok);
        return switch_stmt;
      default:
        RAISE_ERROR("Unexpected token in switch statement:", tok.type);
      }
    }
  }
}

static std::unique_ptr<ASTNode> ParseReturnStmt(FilteredLexer &lexer,
                                                CompileContext &context) {
  auto ret_stmt = std::make_unique<ReturnStmt>();
  lexer.ConsumeNextToken(); // consume "return"
  ret_stmt->ret_value = ParseExpr(lexer, context);
  return ret_stmt;
}

static std::unique_ptr<ASTNode> ParseGotoStmt(FilteredLexer &lexer,
                                              CompileContext &context) {
  auto goto_stmt = std::make_unique<GotoStmt>();
  lexer.ConsumeNextToken(); // consume "goto"
  Token tok = lexer.ConsumeNextToken();
  ASSERT_EQ(tok.type, TokenType::Identifier, "Expected identifier after goto");
  goto_stmt->target = tok.value;
  return goto_stmt;
}

std::unique_ptr<StructDecl> ParseStructDecl(FilteredLexer &lexer,
                                            std::string_view name,
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
          ASSERT_EQ(decl->GetType(), ASTNodeType::VarDecl,
                    "Struct fields must be variable declarations");
          auto *field = decl->As<VarDecl>();
          ASSERT(field->storage_class == StorageClass::None,
                 "A struct field cannot have a storage class");
          fields.push_back(
              {.name = std::string(field->name), .type = field->type});
        }
      } else {
        ASSERT_EQ(statement->GetType(), ASTNodeType::VarDecl,
                  "Struct fields must be variable declarations");
        auto *field = statement->As<VarDecl>();
        ASSERT(field->storage_class == StorageClass::None,
               "A struct field cannot have a storage class");
        fields.push_back(
            {.name = std::string(field->name), .type = field->type});
      }
    }
    context.Types().CompleteStruct(SD->type, fields);
  } else {
    ASSERT_EQ(lexer.PeekNextToken().type, TokenType::Semicolon);
  }
  return SD;
}

std::unique_ptr<UnionDecl> ParseUnionDecl(FilteredLexer &lexer,
                                          std::string_view name,
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
        ASSERT(field->storage_class == StorageClass::None,
               "A Union field cannot have a storage class");
        fields.push_back({.name = field->name, .type = field->type});
      } else {
        ASSERT(decl->GetType() == ASTNodeType::StructDecl ||
                   decl->GetType() == ASTNodeType::UnionDecl ||
                   decl->GetType() == ASTNodeType::EnumDecl,
               "Union fields must be variable declarations");
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
    ASSERT_EQ(lexer.PeekNextToken().type, TokenType::Semicolon);
  }
  return UD;
}

static void ParseEnumContent(FilteredLexer &lexer, CompileContext &context,
                             EnumDecl *decl) {
  while (true) {
    Token tok = lexer.PeekNextToken();
    if (tok.type == TokenType::RBrace) {
      lexer.ConsumeToken(tok);
      return;
    }
    if (tok.type == TokenType::EndOfFile) {
      RAISE_ERROR("Expect }");
    }

    ASSERT_EQ(tok.type, TokenType::Identifier);
    std::string_view name = tok.value;
    lexer.ConsumeToken(tok);

    auto elem_decl = std::make_unique<EnumElemDecl>();
    elem_decl->name = name;
    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::Equal) {
      lexer.ConsumeToken(tok);
      elem_decl->value = ParseExpr(lexer, context);
    }
    decl->values.push_back(std::move(elem_decl));
    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::Comma) {
      lexer.ConsumeToken(tok);
    }
  }
}

std::unique_ptr<EnumDecl> ParseEnumDecl(FilteredLexer &lexer,
                                        std::string_view name,
                                        CompileContext &context) {
  auto ED = std::make_unique<EnumDecl>();
  ED->name = name;
  ED->type = context.Types().GetOrCreateEnum(context.CurrentScopeId(), name);
  if (lexer.PeekNextToken().type == TokenType::LBrace) {
    ED->is_definition = true;
    lexer.ConsumeNextToken();
    ParseEnumContent(lexer, context, ED.get());
  } else {
    ASSERT_EQ(lexer.PeekNextToken().type, TokenType::Semicolon);
  }
  return ED;
}

static std::unique_ptr<FunctionDecl>
ParseFunctionDecl(FilteredLexer &lexer, ParsedType type,
                  StorageClass storage_class, CompileContext &context) {
  auto FD = std::make_unique<FunctionDecl>();
  FD->name = type.declarator_name;
  FD->parameter_names = std::move(type.parameter_names);
  FD->storage_class = storage_class;
  context.PushLayer();
  FD->function_type = type.type;
  if (lexer.PeekNextToken().type == TokenType::LBrace) {
    FD->body = ParseCompoundStmt(lexer, context);
  } else
    ASSERT_EQ(lexer.PeekNextToken().type, TokenType::Semicolon);
  context.PopLayer();
  return FD;
}

static void AddVaList(CompileContext &context) {
  auto *va_list_type = context.Types().GetOrCreateStruct(
      context.CurrentScopeId(), "__builtin_va_list_type");
  StructField fields[] = {
      {"", context.Types().Builtin(BuiltinKind::Int), 0},
      {"", context.Types().Pointer(context.Types().Builtin(BuiltinKind::Void)),
       1},
      {"", context.Types().Pointer(context.Types().Builtin(BuiltinKind::Void)),
       2},
      {"", context.Types().Builtin(BuiltinKind::Int), 3}};
  context.Types().CompleteStruct(va_list_type, fields);
  context.CurrentLayer().AddTypedef("va_list", va_list_type);
}

std::unique_ptr<TranslationUnit> ParseTranslationUnit(FilteredLexer &lexer,
                                                      TypeContext &types) {
  std::unique_ptr<TranslationUnit> TU = std::make_unique<TranslationUnit>();
  TU->body = std::make_unique<CompoundStmt>();
  CompileContext context(types);
  context.PushLayer();
  AddVaList(context);
  while (true) {
    Token tok = lexer.PeekNextToken();
    if (tok.type == TokenType::EndOfFile) {
      break;
    }
    TU->body->statements.push_back(ParseStmt(lexer, context));
  }
  return TU;
}
