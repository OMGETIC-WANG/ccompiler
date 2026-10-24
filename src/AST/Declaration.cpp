#include "Declaration.h"
#include "../Util/Keyword.h"
#include "../Util/Util.h"
#include "Expr.h"
#include "TranslationUnit.h"
#include <list>

std::string_view DeclBase::GetNodeTypeName() const {
  return EnumRefl<DeclType>::GetName(GetType());
}

static std::list<std::string> anoymous_symbols;

static std::string_view GenerateAnoymousSymbol() {
  std::string sym = "<anoymous_symbol_" + std::to_string(GetUniqueId()) + ">";
  anoymous_symbols.push_back(std::move(sym));
  return anoymous_symbols.back();
}

static void ParseTypeSpecifier(DeclarationSpecifier &decl_spec,
                               FilteredLexer &lexer, CompileContext &context) {
  TypeSpecifier &spec = decl_spec.type_spec;
  spec.scope_id = context.CurrentScopeId();
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
        spec.type_name = tok.value;
        lexer.ConsumeToken(tok);
        break;
      }
    } else if (tok.type == TokenType::Identifier) {
      spec.type_name = tok.value;
      spec.is_named_type = true;
      lexer.ConsumeToken(tok);
      break;
    } else if (tok.type == TokenType::LBrace) {
      if (spec.is_struct) {
        std::string_view name = GenerateAnoymousSymbol();
        decl_spec.anoymous_decl = ParseStructDecl(lexer, name, context);
        spec.type_name = name;
        break;
      }
      if (spec.is_enum) {
        std::string_view name = GenerateAnoymousSymbol();
        decl_spec.anoymous_decl = ParseEnumDecl(lexer, name, context);
        spec.type_name = name;
        break;
      }
    } else {
      RAISE_ERROR("Unexpected token");
      break;
    }
  }
}

static DeclarationSpecifier ParseDeclarationSpecifier(FilteredLexer &lexer,
                                                      CompileContext &context) {
  DeclarationSpecifier decl_spec;
  ParseTypeSpecifier(decl_spec, lexer, context);
  return decl_spec;
}

static std::unique_ptr<Declarator> ParseDeclarator(FilteredLexer &lexer,
                                                   CompileContext &context);

static ParamListItem ParseParamListItem(FilteredLexer &lexer,
                                        CompileContext &context) {
  ParamListItem item;
  item.specifier = ParseDeclarationSpecifier(lexer, context);
  Token tok = lexer.PeekNextToken();
  if (tok.type != TokenType::RParen && tok.type != TokenType::Comma) {
    item.declarator = ParseDeclarator(lexer, context);
  }
  return item;
}

static std::unique_ptr<ParamList> ParseParamList(FilteredLexer &lexer,
                                                 CompileContext &context) {
  auto param_list = std::make_unique<ParamList>();
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::RParen) {
    return param_list;
  }
  while (true) {
    param_list->params.push_back(ParseParamListItem(lexer, context));
    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::RParen) {
      break;
    }
    ASSERT(tok.type == TokenType::Comma, "Expected comma in parameter list");
    lexer.ConsumeToken(tok);
  }
  if (param_list->params.size() == 1 &&
      param_list->params[0].specifier.type_spec.type_name == "void" &&
      !param_list->params[0].declarator) {
    param_list->params.clear(); // f(void) -> f()
  }
  return param_list;
}

static std::unique_ptr<ArraySuffix> ParseArraySuffix(FilteredLexer &lexer) {
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

static std::unique_ptr<DirectDeclarator>
ParseDirectDeclarator(FilteredLexer &lexer, CompileContext &context) {
  auto direct_declarator = std::make_unique<DirectDeclarator>();
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::Identifier) {
    lexer.ConsumeToken(tok);
    direct_declarator->declarator =
        std::make_unique<IdentifierDeclarator>(tok.value);
  } else if (tok.type == TokenType::LParen) {
    lexer.ConsumeToken(tok);
    direct_declarator->declarator = ParseDeclarator(lexer, context);
    ASSERT(lexer.ConsumeNextToken().type == TokenType::RParen,
           "Expected closing paren");
  } else {
    return nullptr;
  }

  while (true) {
    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::LParen) {
      lexer.ConsumeToken(tok);
      direct_declarator->suffixes.push_back(ParseParamList(lexer, context));
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

static std::unique_ptr<Declarator> ParseDeclarator(FilteredLexer &lexer,
                                                   CompileContext &context) {
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
  declarator->direct_declarator = ParseDirectDeclarator(lexer, context);
  return declarator;
}

static std::unique_ptr<ValueInitializer>
ParseValueInitializer(FilteredLexer &lexer, CompileContext &context) {
  auto value_initializer = std::make_unique<ValueInitializer>();
  value_initializer->value = ParseExpr(lexer, context);
  return value_initializer;
}

static std::unique_ptr<FieldInitializer>
ParseFieldInitializer(FilteredLexer &lexer, CompileContext &context) {
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

    auto value_initializer = ParseValueInitializer(lexer, context);
    initializer->field_initializers.push_back(
        {field_name, std::move(value_initializer)});
  }
  return initializer;
}

static std::unique_ptr<Initializer> ParseInitializer(FilteredLexer &lexer,
                                                     CompileContext &context) {
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::EndOfFile || tok.type == TokenType::Semicolon ||
      tok.type == TokenType::Comma) {
    RAISE_ERROR("Unexpected token");
  }
  if (tok.type == TokenType::LBrace) {
    lexer.ConsumeToken(tok);
    auto res = ParseFieldInitializer(lexer, context);
    // ASSERT(lexer.ConsumeNextToken().type == TokenType::RBrace,
    //        "Expected closing brace");
    ASSERT_EQ(lexer.ConsumeNextToken().type, TokenType::RBrace,
              "Expected closing brace");
    return res;
  } else {
    return ParseValueInitializer(lexer, context);
  }
}

static std::unique_ptr<InitDeclarator>
ParseInitDeclarator(FilteredLexer &lexer, CompileContext &context) {
  auto init_declarator = std::make_unique<InitDeclarator>();
  init_declarator->declarator = ParseDeclarator(lexer, context);
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::Equal) {
    lexer.ConsumeToken(tok);
    init_declarator->initializer = ParseInitializer(lexer, context);
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

std::unique_ptr<Declaration> ParseDeclaration(FilteredLexer &lexer,
                                              CompileContext &context) {
  auto declaration = std::make_unique<Declaration>();
  declaration->declaration_specifier =
      ParseDeclarationSpecifier(lexer, context);
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::Semicolon || tok.type == TokenType::LBrace) {
    return declaration;
  }
  while (true) {
    declaration->init_declarators.push_back(
        ParseInitDeclarator(lexer, context));
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
