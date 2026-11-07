#include "Declaration.h"
#include "../Util/Util.h"
#include "Expr.h"
#include "TranslationUnit.h"
#include <list>

std::string_view DeclBase::GetNodeTypeName() const {
  return EnumRefl<DeclType>::GetName(GetType());
}

static std::list<std::string> anoymous_symbols;

static std::string_view
GenerateAnoymousSymbol(std::string_view symbol = "symbol") {
  std::string sym = std::format("<anoymous_{}_{}>", symbol, GetUniqueId());
  anoymous_symbols.push_back(std::move(sym));
  return anoymous_symbols.back();
}

static void ParseTypeSpecifier(DeclarationSpecifier &decl_spec,
                               FilteredLexer &lexer, CompileContext &context) {
  TypeSpecifier &spec = decl_spec.type_spec;
  spec.scope_id = context.CurrentScopeId();

  enum TypeMask : unsigned int {
    MVoid = 1 << 0,
    MChar = 1 << 1,
    MShort = 1 << 2,
    MInt = 1 << 3,
    MLong = 1 << 4,
    MFloat = 1 << 5,
    MDouble = 1 << 6,
    MLongLong = 1 << 7,
  };

  unsigned int type_mask = 0;

  bool saw_signed = false;
  bool saw_unsigned = false;

  auto has_nominal_type = [&] {
    return spec.is_struct || spec.is_union || spec.is_enum ||
           spec.is_named_type;
  };
  auto has_builtin_type = [&] {
    return type_mask & (MVoid | MChar | MShort | MInt | MFloat | MDouble |
                        MLong | MLongLong);
  };

  while (true) {
    Token tok = lexer.PeekNextToken();
    if (tok.type == TokenType::Identifier) {
      if (has_nominal_type() || has_builtin_type() || saw_signed ||
          saw_unsigned) {
        break;
      }
      spec.type_name = tok.value;
      spec.is_named_type = true;
      lexer.ConsumeToken(tok);
      continue;
    }

    if (!IsDeclarationSpecifierKeyword(tok.type)) {
      break;
    }

    switch (tok.type) {
    case TokenType::KwTypedef:
      spec.is_typedef = true;
      lexer.ConsumeToken(tok);
      break;
    case TokenType::KwStatic:
      ASSERT(decl_spec.storage_class == StorageClass::None,
             "Multiple storage classes are not supported");
      decl_spec.storage_class = StorageClass::Static;
      lexer.ConsumeToken(tok);
      break;
    case TokenType::KwConst:
      spec.is_const = true;
      lexer.ConsumeToken(tok);
      break;
    case TokenType::KwVolatile:
      RAISE_ERROR("volatile types are not supported yet");
    case TokenType::KwSigned:
    case TokenType::KwUnsigned: {
      const bool is_unsigned = tok.type == TokenType::KwUnsigned;
      ASSERT(!(saw_signed && saw_unsigned),
             "signed and unsigned cannot be used together");
      ASSERT(is_unsigned ? !saw_unsigned : !saw_signed,
             "Duplicate signedness specifier");
      saw_signed |= !is_unsigned;
      saw_unsigned |= is_unsigned;
      lexer.ConsumeToken(tok);
      break;
    }
    case TokenType::KwStruct:
      ASSERT(!has_nominal_type() && !has_builtin_type(),
             "Multiple base types in declaration specifier");
      spec.is_struct = true;
      lexer.ConsumeToken(tok);
      tok = lexer.PeekNextToken();
      if (tok.type == TokenType::Identifier) {
        spec.type_name = tok.value;
        lexer.ConsumeToken(tok);
        if (lexer.PeekNextToken().type == TokenType::LBrace ||
            lexer.PeekNextToken().type == TokenType::Semicolon) {
          auto struct_decl = ParseStructDecl(lexer, tok.value, context);
          decl_spec.decl = std::move(struct_decl);
        }
      } else if (tok.type == TokenType::LBrace) {
        std::string_view name = GenerateAnoymousSymbol("struct");
        decl_spec.decl = ParseStructDecl(lexer, name, context);
        spec.type_name = name;
      } else {
        RAISE_ERROR("Expected struct name or definition");
      }
      break;
    case TokenType::KwUnion:
      ASSERT(!has_nominal_type() && !has_builtin_type(),
             "Multiple base types in declaration specifier");
      spec.is_union = true;
      lexer.ConsumeToken(tok);
      tok = lexer.PeekNextToken();
      if (tok.type == TokenType::Identifier) {
        spec.type_name = tok.value;
        lexer.ConsumeToken(tok);
        if (lexer.PeekNextToken().type == TokenType::LBrace ||
            lexer.PeekNextToken().type == TokenType::Semicolon) {
          auto union_decl = ParseUnionDecl(lexer, tok.value, context);
          decl_spec.decl = std::move(union_decl);
        }
      } else if (tok.type == TokenType::LBrace) {
        std::string_view name = GenerateAnoymousSymbol("union");
        decl_spec.decl = ParseUnionDecl(lexer, name, context);
        spec.type_name = name;
      } else {
        RAISE_ERROR("Expected union name or definition");
      }
      break;
    case TokenType::KwEnum:
      ASSERT(!has_nominal_type() && !has_builtin_type(),
             "Multiple base types in declaration specifier");
      spec.is_enum = true;
      lexer.ConsumeToken(tok);
      tok = lexer.PeekNextToken();
      if (tok.type == TokenType::Identifier) {
        spec.type_name = tok.value;
        lexer.ConsumeToken(tok);
        if (lexer.PeekNextToken().type == TokenType::LBrace) {
          auto enum_decl = ParseEnumDecl(lexer, tok.value, context);
          decl_spec.decl = std::move(enum_decl);
        } else if (lexer.PeekNextToken().type == TokenType::Semicolon) {
          auto enum_type = context.Types().GetOrCreateEnum(
              context.CurrentScopeId(), tok.value);
          context.AddEnum(tok.value, enum_type);
        }
      } else if (tok.type == TokenType::LBrace) {
        std::string_view name = GenerateAnoymousSymbol("enum");
        decl_spec.decl = ParseEnumDecl(lexer, name, context);
        spec.type_name = name;
      } else {
        RAISE_ERROR("Expected enum name or definition");
      }
      break;
    case TokenType::KwExtern:
    case TokenType::KwRegister:
      RAISE_ERROR("Unsupported storage class ", tok.value);
    default:
      static const std::unordered_map<TokenType, TypeMask> tok_type_to_mask = {
          {TokenType::KwInt, MInt},       {TokenType::KwChar, MChar},
          {TokenType::KwShort, MShort},   {TokenType::KwFloat, MFloat},
          {TokenType::KwDouble, MDouble}, {TokenType::KwVoid, MVoid},
      };
      auto it = tok_type_to_mask.find(tok.type);
      if (tok.type == TokenType::KwLong) {
        if (type_mask & MLong) {
          type_mask &= ~MLong;
          type_mask |= MLongLong;
        } else {
          if (type_mask & MLongLong) {
            RAISE_ERROR("long long is not supported yet");
          } else {
            type_mask |= MLong;
          }
        }
        lexer.ConsumeToken(tok);
        break;
      } else if (it != tok_type_to_mask.end()) {
        ASSERT(!has_nominal_type(),
               "Multiple base types in declaration specifier");
        int mask = it->second;
        ASSERT((type_mask & mask) == 0, "Duplicate type specifier");
        type_mask |= mask;
        lexer.ConsumeToken(tok);
        break;
      } else {
        goto end;
      }
    }
  }

end:;
  ASSERT(!saw_signed || !saw_unsigned,
         "signed and unsigned cannot be used together");
  ASSERT(!has_nominal_type() || (!saw_signed && !saw_unsigned),
         "signedness cannot qualify a struct, enum, or typedef type");

  if (has_nominal_type()) {
    ASSERT(!has_builtin_type(), "Multiple base types in declaration specifier");
    return;
  }

  if ((type_mask & MDouble) && (type_mask & MLong)) {
    RAISE_ERROR("long double is not supported yet");
  }

  if ((type_mask & (MShort | MLong | MLongLong)) && (type_mask & MInt)) {
    type_mask &= ~MInt;
  }

  ASSERT(std::has_single_bit(type_mask),
         "Invalid combination of type specifiers");

  constexpr unsigned int integer_mask =
      MChar | MShort | MInt | MLong | MLongLong;
  ASSERT((type_mask & integer_mask) || (!saw_unsigned && !saw_signed),
         "signedness can only qualify integer types");

  spec.is_builtin = true;
  spec.is_unsigned = saw_unsigned;
  switch (type_mask) {
  case MVoid:
    spec.type_name = "void";
    break;
  case MChar:
    spec.type_name = "char";
    break;
  case MShort:
    spec.type_name = "short";
    break;
  case MInt:
    spec.type_name = "int";
    break;
  case MLong:
    spec.type_name = "long";
    break;
  case MLongLong:
    spec.type_name = "long long";
    break;
  case MFloat:
    spec.type_name = "float";
    break;
  case MDouble:
    spec.type_name = "double";
    break;
  default:
    RAISE_ERROR("Unknown type mask: ", type_mask);
  }
}

DeclarationSpecifier ParseDeclarationSpecifier(FilteredLexer &lexer,
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
  ASSERT(item.specifier.storage_class == StorageClass::None,
         "A parameter cannot have a storage class");
  ASSERT(!item.specifier.type_spec.is_typedef,
         "A parameter cannot be a typedef declaration");
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
    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::DotDotDot) {
      param_list->has_va_args = true;
      lexer.ConsumeToken(tok);
      ASSERT(lexer.PeekNextToken().type == TokenType::RParen,
             "Unexpected token after '...'");
      break;
    }
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
      Declarator::PointerQualifier qualifier;
      while (lexer.PeekNextToken().type == TokenType::KwConst) {
        lexer.ConsumeNextToken();
        qualifier.is_const = true;
      }
      if (lexer.PeekNextToken().type == TokenType::KwVolatile) {
        RAISE_ERROR("volatile pointer qualifiers are not supported yet");
      }
      declarator->pointers.push_back(qualifier);
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

static std::unique_ptr<ArrayInitializer>
ParseArrayInitializer(FilteredLexer &lexer, CompileContext &context) {
  auto initializer = std::make_unique<ArrayInitializer>();
  while (true) {
    if (lexer.PeekNextToken().type == TokenType::RBrace) {
      break;
    }
    if (initializer->initializers.size()) {
      ASSERT(lexer.PeekNextToken().type == TokenType::Comma,
             "Expect comma in array initializer");
      lexer.ConsumeNextToken();
    }
    if (lexer.PeekNextToken().type == TokenType::RBrace) {
      break;
    }
    auto val_initializer = ParseValueInitializer(lexer, context);
    initializer->initializers.push_back(std::move(val_initializer));
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
    if (lexer.PeekNextToken().type == TokenType::Dot) {
      auto res = ParseFieldInitializer(lexer, context);
      ASSERT_EQ(lexer.ConsumeNextToken().type, TokenType::RBrace,
                "Expected closing brace");
      return res;
    } else {
      auto res = ParseArrayInitializer(lexer, context);
      ASSERT_EQ(lexer.ConsumeNextToken().type, TokenType::RBrace,
                "Expected closing brace");
      return res;
    }
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
  if (IsDeclarationSpecifierKeyword(tok.type)) {
    return true;
  }
  if (tok.type == TokenType::Identifier) {
    if (context.Is(tok.value, ContextItemType::Typedef) ||
        context.Is(tok.value, ContextItemType::Enum)) {
      return true;
    }
  }
  return false;
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
        tok.type == TokenType::EndOfFile || tok.type == TokenType::RParen) {
      break;
    }
    ASSERT(tok.type == TokenType::Comma, "Expected comma in declaration");
    lexer.ConsumeToken(tok);
  }
  return declaration;
}

bool IsConstantFoldable(const Initializer *initializer) {
  if (initializer == nullptr) {
    return true;
  }

  if (auto val_init = initializer->DynCast<ValueInitializer>()) {
    return IsConstantFoldable(val_init->value.get());
  }
  if (auto field_init = initializer->DynCast<FieldInitializer>()) {
    for (const auto &[name, field_init] : field_init->field_initializers) {
      if (!IsConstantFoldable(field_init.get())) {
        return false;
      }
    }
    return true;
  }
  if (auto array_init = initializer->DynCast<ArrayInitializer>()) {
    for (const auto &elem_init : array_init->initializers) {
      if (!IsConstantFoldable(elem_init.get())) {
        return false;
      }
    }
    return true;
  }
  return false;
}
