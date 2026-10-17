#include "TranslationUnit.h"
#include "../Exception.h"
#include "../Util/Keyword.h"
#include "Context.h"
#include "Declaration.h"
#include "Operations.h"

std::string_view ParseTypeName(FilteredLexer &lexer) {
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::Keyword) {
    if (IsTypeKeyword(tok.value)) {
      lexer.ConsumeToken(tok);
      return tok.value;
    } else if (tok.value == "struct") {
      lexer.ConsumeToken(tok);
      tok = lexer.PeekNextToken();
      ASSERT(tok.type == TokenType::Identifier,
             "Expected identifier after struct");
      lexer.ConsumeToken(tok);
      return tok.value;
    }
  } else if (tok.type == TokenType::Identifier) {
    lexer.ConsumeToken(tok);
    return tok.value;
  }
  RAISE_ERROR("Unexpected token");
}

TypeSpecifier ParseTypeSpecifier(FilteredLexer &lexer) {
  TypeSpecifier spec;
  while (true) {
    Token tok = lexer.PeekNextToken();
    if (tok.type == TokenType::Keyword) {
      if (tok.value == "struct") {
        spec.is_struct = true;
        lexer.ConsumeToken(tok);
      } else if (tok.value == "typedef") {
        spec.is_typedef = true;
        lexer.ConsumeToken(tok);
      } else if (tok.value == "static") {
        spec.is_static = true;
        lexer.ConsumeToken(tok);
      } else if (tok.value == "enum") {
        spec.is_enum = true;
        lexer.ConsumeToken(tok);
      } else if (IsTypeKeyword(tok.value)) {
        spec.is_builtin = true;
        spec.type = tok.value;
        lexer.ConsumeToken(tok);
        break;
      }
    } else if (tok.type == TokenType::Identifier) {
      spec.type = tok.value;
      lexer.ConsumeToken(tok);
      break;
    } else {
      RAISE_ERROR("Unexpected token");
      break;
    }
  }
  return spec;
}

DeclarationSpecifier ParseDeclarationSpecifier(FilteredLexer &lexer) {
  DeclarationSpecifier decl_spec;
  decl_spec.type_spec = ParseTypeSpecifier(lexer);
  return decl_spec;
}

std::unique_ptr<Declarator> ParseDeclarator(FilteredLexer &lexer);

ParamListItem ParseParamListItem(FilteredLexer &lexer) {
  ParamListItem item;
  item.specifier = ParseDeclarationSpecifier(lexer);
  Token tok = lexer.PeekNextToken();
  if (tok.type != TokenType::RParen && tok.type != TokenType::Comma) {
    item.declarator = ParseDeclarator(lexer);
  }
  return item;
}

std::unique_ptr<ParamList> ParseParamList(FilteredLexer &lexer) {
  auto param_list = std::make_unique<ParamList>();
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::RParen) {
    return param_list;
  }
  while (true) {
    param_list->params.push_back(ParseParamListItem(lexer));
    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::RParen) {
      break;
    }
    ASSERT(tok.type == TokenType::Comma, "Expected comma in parameter list");
    lexer.ConsumeToken(tok);
  }
  if (param_list->params.size() == 1 &&
      param_list->params[0].specifier.type_spec.type == "void" &&
      !param_list->params[0].declarator) {
    param_list->params.clear(); // f(void) -> f()
  }
  return param_list;
}

std::unique_ptr<ArraySuffix> ParseArraySuffix(FilteredLexer &lexer) {
  auto array_suffix = std::make_unique<ArraySuffix>();
  if (lexer.PeekNextToken().type == TokenType::RBracket) {
    return array_suffix;
  }
  Token tok = lexer.PeekNextToken();
  ASSERT(tok.type == TokenType::LiteralNum, "Expected number in array size");
  lexer.ConsumeToken(tok);
  array_suffix->size = std::stoul(std::string(tok.value));
  return array_suffix;
}

std::unique_ptr<DirectDeclarator> ParseDirectDeclarator(FilteredLexer &lexer) {
  auto direct_declarator = std::make_unique<DirectDeclarator>();
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::Identifier) {
    lexer.ConsumeToken(tok);
    direct_declarator->declarator =
        std::make_unique<IdentifierDeclarator>(tok.value);
  } else if (tok.type == TokenType::LParen) {
    lexer.ConsumeToken(tok);
    direct_declarator->declarator = ParseDeclarator(lexer);
    ASSERT(lexer.ConsumeNextToken().type == TokenType::RParen,
           "Expected closing paren");
  } else {
    return nullptr;
  }

  while (true) {
    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::LParen) {
      lexer.ConsumeToken(tok);
      direct_declarator->suffixes.push_back(ParseParamList(lexer));
      ASSERT(lexer.ConsumeNextToken().type == TokenType::RParen,
             "Expected closing paren");
    } else if (tok.type == TokenType::LBracket) {
      lexer.ConsumeToken(tok);
      direct_declarator->suffixes.push_back(ParseArraySuffix(lexer));
      ASSERT(lexer.ConsumeNextToken().type == TokenType::RBracket,
             "Expected closing bracket");
    } else {
      break;
    }
  }

  return direct_declarator;
}

std::unique_ptr<Declarator> ParseDeclarator(FilteredLexer &lexer) {
  auto declarator = std::make_unique<Declarator>();
  while (true) {
    Token tok = lexer.PeekNextToken();
    if (tok.type == TokenType::Star) {
      lexer.ConsumeToken(tok);
      declarator->pointer_count++;
    } else {
      break;
    }
  }
  declarator->direct_declarator = ParseDirectDeclarator(lexer);
  return declarator;
}

std::unique_ptr<ValueInitializer> ParseValueInitializer(FilteredLexer &lexer) {
  auto value_initializer = std::make_unique<ValueInitializer>();
  value_initializer->value = ParseExpr(lexer);
  return value_initializer;
}

std::unique_ptr<FieldInitializer> ParseFieldInitializer(FilteredLexer &lexer) {
  auto initializer = std::make_unique<FieldInitializer>();
  while (true) {
    Token tok = lexer.PeekNextToken();
    if (tok.type == TokenType::RBrace) {
      break;
    }
    if (tok.type == TokenType::Comma) {
      lexer.ConsumeToken(tok);
      tok = lexer.PeekNextToken();
    }
    ASSERT(tok.type == TokenType::Dot, "Expected dot in field initializer");
    lexer.ConsumeToken(tok);

    tok = lexer.PeekNextToken();
    ASSERT(tok.type == TokenType::Identifier,
           "Expected identifier in field initializer");
    lexer.ConsumeToken(tok);
    std::string_view field_name = tok.value;

    ASSERT(lexer.ConsumeNextToken().type == TokenType::Equal,
           "Expected equal in field initializer");

    auto value_initializer = ParseValueInitializer(lexer);
    initializer->field_initializers.push_back(
        {field_name, std::move(value_initializer)});
  }
  return initializer;
}

std::unique_ptr<Initializer> ParseInitializer(FilteredLexer &lexer) {
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::EndOfFile || tok.type == TokenType::Semicolon ||
      tok.type == TokenType::Comma) {
    RAISE_ERROR("Unexpected token");
  }
  if (tok.type == TokenType::LBrace) {
    lexer.ConsumeToken(tok);
    auto res = ParseFieldInitializer(lexer);
    // ASSERT(lexer.ConsumeNextToken().type == TokenType::RBrace,
    //        "Expected closing brace");
    ASSERT_EQ(lexer.ConsumeNextToken().type, TokenType::RBrace,
              "Expected closing brace");
    return res;
  } else {
    return ParseValueInitializer(lexer);
  }
}

std::unique_ptr<InitDeclarator> ParseInitDeclarator(FilteredLexer &lexer) {
  auto init_declarator = std::make_unique<InitDeclarator>();
  init_declarator->declarator = ParseDeclarator(lexer);
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::Equal) {
    lexer.ConsumeToken(tok);
    init_declarator->initializer = ParseInitializer(lexer);
  }
  return init_declarator;
}

bool IsDeclarationSpecifier(Token tok, const CompileContext &context) {
  switch (tok.type) {
  case TokenType::Keyword:
    if (IsTypeKeyword(tok.value) || IsStorageSpecifierKeyword(tok.value)) {
      return true;
    }
    if (tok.value == "struct" || tok.value == "enum" ||
        tok.value == "typedef") {
      return true;
    }
    return false;
  case TokenType::Identifier:
    if (context.Is(tok.value, ContextItemType::Typedef)) {
      return true;
    }
    return false;
  default:
    return false;
  }
}

std::unique_ptr<Declaration> ParseDeclaration(FilteredLexer &lexer) {
  auto declaration = std::make_unique<Declaration>();
  declaration->declaration_specifier = ParseDeclarationSpecifier(lexer);
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::Semicolon || tok.type == TokenType::LBrace) {
    return declaration;
  }
  while (true) {
    declaration->init_declarators.push_back(ParseInitDeclarator(lexer));
    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::Semicolon || tok.type == TokenType::LBrace ||
        tok.type == TokenType::EndOfFile) {
      break;
    }
    ASSERT(tok.type == TokenType::Comma, "Expected comma in declaration");
    lexer.ConsumeToken(tok);
  }
  return declaration;
}

enum class PrecLevel {
  Primary,
  Assign,
  Logic,
  AddSub,
  MulDivMod,
  Prefix,
  Postfix,
};

static int GetPrecedence(TokenType type) {
  switch (type) {
  case TokenType::Identifier:
    return (int)PrecLevel::Primary;
  case TokenType::Add:
  case TokenType::Sub:
    return (int)PrecLevel::AddSub;
  case TokenType::Star:
  case TokenType::Div:
  case TokenType::Mod:
    return (int)PrecLevel::MulDivMod;
  case TokenType::Equal:
    return (int)PrecLevel::Assign;
  case TokenType::Eq:
  case TokenType::Ne:
  case TokenType::Lt:
  case TokenType::Gt:
  case TokenType::Le:
  case TokenType::Ge:
    return (int)PrecLevel::Logic;

  default:
    RAISE_ERROR("Operator not supported yet");
  }
}

static OperationType GetOperationType(TokenType type) {
  switch (type) {
  case TokenType::Add:
    return OperationType::Add;
  case TokenType::Sub:
    return OperationType::Sub;
  case TokenType::Star:
    return OperationType::Mul;
  case TokenType::Div:
    return OperationType::Div;
  case TokenType::Mod:
    return OperationType::Mod;
  case TokenType::Equal:
    return OperationType::Assignment;
  case TokenType::Eq:
    return OperationType::Eq;
  default:
    RAISE_ERROR("Operator not supported yet");
  }
}

static std::unique_ptr<BinaryOp>
CombineBackTwo(std::vector<TokenType> &op_stack,
               std::vector<std::unique_ptr<ASTNode>> &val_stack) {
  TokenType op_type = op_stack.back();
  op_stack.pop_back();
  auto right = std::move(val_stack.back());
  val_stack.pop_back();
  auto left = std::move(val_stack.back());
  val_stack.pop_back();
  auto bin_op = MakeBinaryOp(GetOperationType(op_type));
  bin_op->left = std::move(left);
  bin_op->right = std::move(right);
  return bin_op;
}

static void TryBuildExpr(std::vector<TokenType> &op_stack,
                         std::vector<std::unique_ptr<ASTNode>> &val_stack) {
  if (op_stack.empty()) {
    return;
  }
  while (op_stack.size() >= 2) {
    auto back0_op_type = op_stack[op_stack.size() - 1];
    auto back1_op_type = op_stack[op_stack.size() - 2];
    if (GetPrecedence(back0_op_type) > GetPrecedence(back1_op_type)) {
      break;
    }
    val_stack.push_back(CombineBackTwo(op_stack, val_stack));
  }
  if (op_stack.size() == 1) {
    val_stack.push_back(CombineBackTwo(op_stack, val_stack));
    return;
  }
}

std::vector<std::unique_ptr<ASTNode>> ParseQuotedExpr(FilteredLexer &lexer) {
  std::vector<std::unique_ptr<ASTNode>> results;
  while (true) {
    Token tok = lexer.PeekNextToken();
    if (tok.type == TokenType::RParen || tok.type == TokenType::EndOfFile ||
        tok.type == TokenType::Semicolon) {
      break;
    }

    results.push_back(ParseExpr(lexer));

    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::RParen || tok.type == TokenType::EndOfFile ||
        tok.type == TokenType::Semicolon) {
      break;
    } else {
      ASSERT(tok.type == TokenType::Comma, "Expected an comma");
      lexer.ConsumeToken(tok);
    }
  }
  return results;
}

std::unique_ptr<ASTNode> ParsePrimary(FilteredLexer &lexer) {
  Token tok = lexer.PeekNextToken();
  switch (tok.type) {
  case TokenType::LiteralNum: {
    lexer.ConsumeToken(tok);
    auto literal_num = std::make_unique<LiteralNum>();
    literal_num->value = tok.value;
    return literal_num;
  }
  case TokenType::Identifier: {
    lexer.ConsumeToken(tok);
    auto var_ref = std::make_unique<VarRef>();
    var_ref->name = tok.value;
    return var_ref;
  }
  case TokenType::LParen: {
    lexer.ConsumeToken(tok);
    auto expr = ParseQuotedExpr(lexer);
    ASSERT(lexer.ConsumeNextToken().type == TokenType::RParen,
           "Expected closing paren");
    auto seq_eval = std::make_unique<SequenceEval>();
    seq_eval->expressions = std::move(expr);
    return seq_eval;
  }
  default:
    RAISE_ERROR("Unexpected token");
  }
}

std::unique_ptr<ASTNode> ParsePostfix(FilteredLexer &lexer,
                                      std::unique_ptr<ASTNode> primary) {
  while (true) {
    Token tok = lexer.PeekNextToken();
    switch (tok.type) {
    case TokenType::EndOfFile:
    case TokenType::Semicolon:
    case TokenType::RParen:
      return primary;
    case TokenType::LParen: {
      lexer.ConsumeToken(tok);
      auto func_invoke = std::make_unique<FunctionInvocation>();
      func_invoke->function = std::move(primary);
      func_invoke->arguments = ParseQuotedExpr(lexer);
      ASSERT(lexer.ConsumeNextToken().type == TokenType::RParen,
             "Expected closing paren");
      primary = std::move(func_invoke);
      break;
    }
    case TokenType::LBracket: {
      lexer.ConsumeToken(tok);
      auto subscript_op = std::make_unique<SubscriptOp>();
      subscript_op->left = std::move(primary);
      subscript_op->right = ParseExpr(lexer);
      ASSERT(lexer.ConsumeNextToken().type == TokenType::RBracket,
             "Expected closing bracket");
      primary = std::move(subscript_op);
      break;
    }
    case TokenType::Dot: {
      lexer.ConsumeToken(tok);
      auto get_field_op = std::make_unique<GetFieldOp>();
      get_field_op->left = std::move(primary);
      Token field_tok = lexer.ConsumeNextToken();
      ASSERT(field_tok.type == TokenType::Identifier, "Expected field name");
      auto field_ref = std::make_unique<VarRef>();
      field_ref->name = field_tok.value;
      get_field_op->right = std::move(field_ref);
      primary = std::move(get_field_op);
      break;
    }
    case TokenType::AddAdd: {
      lexer.ConsumeToken(tok);
      auto inc_after_use_op = std::make_unique<IncAfterUseOp>();
      inc_after_use_op->operand = std::move(primary);
      primary = std::move(inc_after_use_op);
      break;
    }
    case TokenType::SubSub: {
      lexer.ConsumeToken(tok);
      auto dec_after_use_op = std::make_unique<DecAfterUseOp>();
      dec_after_use_op->operand = std::move(primary);
      primary = std::move(dec_after_use_op);
      break;
    }
    default:
      return primary;
    }
  }
}

std::unique_ptr<ASTNode> AddPrefixes(std::unique_ptr<ASTNode> node,
                                     const std::vector<TokenType> &prefixes) {
  for (int i = prefixes.size() - 1; i >= 0; i--) {
    std::unique_ptr<UnaryOp> prefix;
    switch (prefixes[i]) {
    case TokenType::AddAdd:
      prefix = std::make_unique<IncBeforeUseOp>();
      break;
    case TokenType::SubSub:
      prefix = std::make_unique<DecBeforeUseOp>();
      break;
    case TokenType::Star:
      prefix = std::make_unique<DerefOp>();
      break;
    default:
      RAISE_ERROR("Prefix operator not supported yet");
    }
    prefix->operand = std::move(node);
    node = std::move(prefix);
  }
  return node;
}

std::unique_ptr<ASTNode> ParsePrefixPostfixExpr(FilteredLexer &lexer) {
  std::vector<TokenType> prefixes;
  while (true) {
    Token tok = lexer.PeekNextToken();
    switch (tok.type) {
    case TokenType::EndOfFile:
    case TokenType::Semicolon:
      return nullptr;
    case TokenType::AddAdd:
    case TokenType::SubSub:
    case TokenType::Add:
    case TokenType::Sub:
    case TokenType::Star:
      lexer.ConsumeToken(tok);
      prefixes.push_back(tok.type);
      break;
    default:
      auto primary = ParsePrimary(lexer);
      auto postfixed = ParsePostfix(lexer, std::move(primary));
      return AddPrefixes(std::move(postfixed), prefixes);
    }
  }
}

static bool IsBinaryOp(TokenType type) {
  switch (type) {
  case TokenType::Add:
  case TokenType::Sub:
  case TokenType::Div:
  case TokenType::Mod:
  case TokenType::Equal:
  case TokenType::Star:
  case TokenType::LShift:
  case TokenType::RShift:
  case TokenType::BitAnd:
  case TokenType::BitOr:
  case TokenType::BitXor:
  case TokenType::Eq:
  case TokenType::Ne:
  case TokenType::Lt:
  case TokenType::Gt:
  case TokenType::Le:
  case TokenType::Ge:
    return true;
  default:
    return false;
  }
}

static bool IsRightCombineOp(TokenType op) {
  switch (op) {
  case TokenType::Equal:
    return true;
  default:
    return false;
  }
}

std::unique_ptr<ASTNode> ParseExpr(FilteredLexer &lexer, int min_prec) {
  auto lhs = ParsePrefixPostfixExpr(lexer);
  while (true) {
    Token tok = lexer.PeekNextToken();
    switch (tok.type) {
    case TokenType::EndOfFile:
    case TokenType::Semicolon:
    case TokenType::RParen:
    case TokenType::RBracket:
    case TokenType::Comma:
    case TokenType::RBrace:
      return lhs;
    default:
      int op_prec = GetPrecedence(tok.type);
      if (!IsBinaryOp(tok.type) || op_prec < min_prec) {
        return lhs;
      }

      lexer.ConsumeToken(tok);

      int next_min_prec = IsRightCombineOp(tok.type) ? op_prec : op_prec + 1;

      auto rhs = ParseExpr(lexer, next_min_prec);

      auto bin_op = MakeBinaryOp(GetOperationType(tok.type));
      bin_op->left = std::move(lhs);
      bin_op->right = std::move(rhs);
      lhs = std::move(bin_op);
    }
  }

  return lhs;
}

static bool IsFlowControlKeyword(Token tok) {
  if (tok.type != TokenType::Keyword) {
    return false;
  }
  static const std::string_view flow_control_keywords[] = {
      "if", "else", "while", "for", "return", "break", "continue", "goto"};
  return Contains(flow_control_keywords, tok.value);
}

static std::unique_ptr<FunctionDecl>
ParseFunctionDecl(FilteredLexer &lexer, std::unique_ptr<TypeBase> type,
                  CompileContext &context);

static std::unique_ptr<StructDecl> ParseStructDecl(FilteredLexer &lexer,
                                                   std::string_view name,
                                                   CompileContext &context);

static std::unique_ptr<EnumDecl> ParseEnumDecl(FilteredLexer &lexer,
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
    auto decl = ParseDeclaration(lexer);
    auto decl_types = ParseType(decl.get());
    auto type_spec = decl->declaration_specifier.type_spec;

    auto decl_stmt = std::make_unique<DeclStmt>();
    auto &results = decl_stmt->decls;

    if (type_spec.is_typedef) {
      for (auto &type : decl_types) {
        if (!type->identifier.empty()) {
          auto typedef_decl = std::make_unique<TypedefDecl>();
          typedef_decl->name = type->identifier;
          typedef_decl->type = std::move(type);
          stmt = std::move(typedef_decl);
          context.AddTypedef(typedef_decl->name);
        }
      }
    } else {
      for (size_t i = 0; i < decl_types.size(); i++) {
        auto &type = decl_types[i];

        switch (type->GetType()) {
        case TypeBaseType::RawType: {
          if (!type->identifier.empty()) {
            auto var_decl = std::make_unique<VarDecl>();
            var_decl->name = type->identifier;
            var_decl->type = std::move(type);
            var_decl->initializer =
                std::move(decl->init_declarators[i]->initializer);
            context.AddVariable(var_decl->name);
            results.push_back(std::move(var_decl));
          } else {
            if (type_spec.is_struct) {
              results.push_back(
                  std::move(ParseStructDecl(lexer, type_spec.type, context)));
              context.AddStruct(type_spec.type);
            } else if (type_spec.is_enum) {
              results.push_back(
                  std::move(ParseEnumDecl(lexer, type_spec.type, context)));
              context.AddEnum(type_spec.type);
            }
          }
          break;
        }
        case TypeBaseType::PointerType:
        case TypeBaseType::ArrayType:
          if (!type->identifier.empty()) {
            auto var_decl = std::make_unique<VarDecl>();
            var_decl->name = type->identifier;
            var_decl->type = std::move(type);
            stmt = std::move(var_decl);
            context.AddVariable(var_decl->name);
          } else {
            RAISE_ERROR("Expected an identifier");
          }
          break;
        case TypeBaseType::FunctionType:
          results.push_back(
              std::move(ParseFunctionDecl(lexer, std::move(type), context)));
        }
      }
    }
    stmt = std::move(decl_stmt);
  } else if (IsFlowControlKeyword(tok)) {
    if (tok.value == "if") {
      stmt = ParseIfStmt(lexer, context);
    } else if (tok.value == "return") {
      stmt = ParseReturnStmt(lexer, context);
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

static std::unique_ptr<StructDecl> ParseStructDecl(FilteredLexer &lexer,
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

static std::unique_ptr<EnumDecl> ParseEnumDecl(FilteredLexer &lexer,
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
ParseFunctionDecl(FilteredLexer &lexer, std::unique_ptr<TypeBase> type,
                  CompileContext &context) {
  auto FD = std::make_unique<FunctionDecl>();
  FD->name = type->identifier;
  context.AddFunction(FD->name);
  context.PushLayer();
  FD->function_type = std::move(type);
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
