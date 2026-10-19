#include "TranslationUnit.h"
#include "../Exception.h"
#include "Context.h"
#include "Declaration.h"
#include "Expr.h"

static bool IsFlowControlKeyword(Token tok) {
  if (tok.type != TokenType::Keyword) {
    return false;
  }
  static const std::string_view flow_control_keywords[] = {
      "if", "else", "while", "for", "return", "break", "continue", "goto"};
  return Contains(flow_control_keywords, tok.value);
}

static std::unique_ptr<FunctionDecl> ParseFunctionDecl(FilteredLexer &lexer,
                                                       ParsedType type,
                                                       CompileContext &context);

std::unique_ptr<StructDecl> ParseStructDecl(FilteredLexer &lexer,
                                            std::string_view name,
                                            CompileContext &context);

std::unique_ptr<EnumDecl> ParseEnumDecl(FilteredLexer &lexer,
                                        std::string_view name,
                                        CompileContext &context);

std::unique_ptr<CompoundStmt> ParseCompoundStmt(FilteredLexer &lexer,
                                                CompileContext &context);

static std::unique_ptr<ASTNode> ParseControlCondExpr(FilteredLexer &lexer) {
  Token tok = lexer.PeekNextToken();
  ASSERT_EQ(tok.type, TokenType::LParen, "Expected (");
  lexer.ConsumeToken(tok);
  auto expr = ParseExpr(lexer);
  tok = lexer.PeekNextToken();
  ASSERT_EQ(tok.type, TokenType::RParen, "Expected )");
  lexer.ConsumeToken(tok);
  return expr;
}

static std::unique_ptr<ASTNode> ParseIfStmt(FilteredLexer &lexer,
                                            CompileContext &context);

static std::unique_ptr<ASTNode> ParseReturnStmt(FilteredLexer &lexer,
                                                CompileContext &context);

static std::unique_ptr<ASTNode> ParseGotoStmt(FilteredLexer &lexer,
                                              CompileContext &context);

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

  std::unique_ptr<ASTNode> stmt;

  tok = lexer.PeekNextToken();
  if (IsDeclarationSpecifier(tok, context)) {
    auto decl = ParseDeclaration(lexer, context);
    auto decl_types = ParseType(decl.get());
    auto type_spec = std::move(decl->declaration_specifier.type_spec);

    DeclStmtBuilder decl_stmt_builder;

    if (type_spec.is_typedef) {
      if (decl->declaration_specifier.anoymous_decl) {
        decl_stmt_builder.Add(
            std::move(decl->declaration_specifier.anoymous_decl));
      }
      for (auto &parsed_type : decl_types) {
        if (!parsed_type.declarator_name.empty()) {
          auto typedef_decl = std::make_unique<TypedefDecl>();
          typedef_decl->name = parsed_type.declarator_name;
          typedef_decl->type = std::move(parsed_type.type);
          context.AddTypedef(typedef_decl->name);
          decl_stmt_builder.Add(std::move(typedef_decl));
        }
      }
    } else {
      for (size_t i = 0; i < decl_types.size(); i++) {
        auto &parsed_type = decl_types[i];
        auto &type = parsed_type.type;

        switch (type->GetType()) {
        case TypeBaseType::BuiltinType:
        case TypeBaseType::StructType:
        case TypeBaseType::EnumType:
        case TypeBaseType::OpaqueType: {
          if (!parsed_type.declarator_name.empty()) {
            auto var_decl = std::make_unique<VarDecl>();
            var_decl->name = parsed_type.declarator_name;
            var_decl->type = std::move(type);
            var_decl->initializer =
                std::move(decl->init_declarators[i]->initializer);
            context.AddVariable(var_decl->name);
            decl_stmt_builder.Add(std::move(var_decl));
          } else {
            if (type_spec.is_struct) {
              decl_stmt_builder.Add(
                  std::move(ParseStructDecl(lexer, type_spec.type, context)));
              context.AddStruct(type_spec.type);
            } else if (type_spec.is_enum) {
              decl_stmt_builder.Add(
                  std::move(ParseEnumDecl(lexer, type_spec.type, context)));
              context.AddEnum(type_spec.type);
            }
          }
          break;
        }
        case TypeBaseType::PointerType:
        case TypeBaseType::ArrayType:
          if (!parsed_type.declarator_name.empty()) {
            auto var_decl = std::make_unique<VarDecl>();
            var_decl->name = parsed_type.declarator_name;
            var_decl->type = std::move(type);
            context.AddVariable(var_decl->name);
            decl_stmt_builder.Add(std::move(var_decl));
          } else {
            RAISE_ERROR("Expected an identifier");
          }
          break;
        case TypeBaseType::FunctionType:
          decl_stmt_builder.Add(std::move(
              ParseFunctionDecl(lexer, std::move(parsed_type), context)));
          break;
        }
      }
    }
    stmt = decl_stmt_builder.Build();
  } else if (IsFlowControlKeyword(tok)) {
    if (tok.value == "if") {
      stmt = ParseIfStmt(lexer, context);
    } else if (tok.value == "return") {
      stmt = ParseReturnStmt(lexer, context);
    } else if (tok.value == "goto") {
      stmt = ParseGotoStmt(lexer, context);
    } else {
      RAISE_ERROR("Flow control not supported yet");
    }
  } else {
    Token next_tok = lexer.PeekNextToken(1);
    if (next_tok.type == TokenType::Colon) {
      lexer.ConsumeTokens(2);
      auto label_stmt = std::make_unique<LabelStmt>();
      label_stmt->name = tok.value;
      stmt = std::move(label_stmt);
      context.AddLabel(label_stmt->name);
    } else {
      stmt = ParseExpr(lexer);
    }
  }

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

static std::unique_ptr<ASTNode> ParseIfStmt(FilteredLexer &lexer,
                                            CompileContext &context) {
  auto if_stmt = std::make_unique<IfStmt>();
  lexer.ConsumeNextToken(); // consume "if"
  if_stmt->condition = ParseControlCondExpr(lexer);
  if_stmt->then_branch = ParseStmt(lexer, context);
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::Keyword && tok.value == "else") {
    lexer.ConsumeToken(tok);
    if_stmt->else_branch = ParseStmt(lexer, context);
  }
  return if_stmt;
}

static std::unique_ptr<ASTNode> ParseReturnStmt(FilteredLexer &lexer,
                                                CompileContext &context) {
  auto ret_stmt = std::make_unique<ReturnStmt>();
  lexer.ConsumeNextToken(); // consume "return"
  ret_stmt->ret_value = ParseExpr(lexer);
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
  if (lexer.PeekNextToken().type == TokenType::LBrace) {
    SD->fields = ParseCompoundStmt(lexer, context);
  } else {
    ASSERT_EQ(lexer.PeekNextToken().type, TokenType::Semicolon);
  }
  return SD;
}

static void ParseEnumContent(FilteredLexer &lexer, EnumDecl *decl) {
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

    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::Equal) {
      lexer.ConsumeToken(tok);
      auto elem_decl = std::make_unique<EnumElemDecl>();
      elem_decl->name = name;
      elem_decl->value = ParseExpr(lexer);
      decl->values.push_back(std::move(elem_decl));
    }
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
  if (lexer.PeekNextToken().type == TokenType::LBrace) {
    lexer.ConsumeNextToken();
    ParseEnumContent(lexer, ED.get());
  } else {
    ASSERT_EQ(lexer.PeekNextToken().type, TokenType::Semicolon);
  }
  return ED;
}

static std::unique_ptr<FunctionDecl>
ParseFunctionDecl(FilteredLexer &lexer, ParsedType type,
                  CompileContext &context) {
  auto FD = std::make_unique<FunctionDecl>();
  FD->name = type.declarator_name;
  FD->parameter_names = std::move(type.parameter_names);
  context.AddFunction(FD->name);
  context.PushLayer();
  FD->function_type = std::move(type.type);
  if (lexer.PeekNextToken().type == TokenType::LBrace) {
    FD->body = ParseCompoundStmt(lexer, context);
  } else
    ASSERT_EQ(lexer.PeekNextToken().type, TokenType::Semicolon);
  context.PopLayer();
  return FD;
}

std::unique_ptr<TranslationUnit> ParseTranslationUnit(FilteredLexer &lexer) {
  std::unique_ptr<TranslationUnit> TU = std::make_unique<TranslationUnit>();
  TU->body = std::make_unique<CompoundStmt>();
  CompileContext context;
  context.PushLayer();
  while (true) {
    Token tok = lexer.PeekNextToken();
    if (tok.type == TokenType::EndOfFile) {
      break;
    }
    TU->body->statements.push_back(ParseStmt(lexer, context));
  }
  return TU;
}
