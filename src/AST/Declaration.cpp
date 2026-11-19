#include "Declaration.h"
#include "../Util/Util.h"
#include "AST.h"
#include "Diagnostics/Errors.generated.h"
#include "Expr.h"
#include "ParserError.h"
#include "TranslationUnit.h"
#include <list>

static std::unique_ptr<StructDecl>
ParseStructDecl(Lexer &lexer, std::string_view name, CompileContext &context);

static std::unique_ptr<UnionDecl>
ParseUnionDecl(Lexer &lexer, std::string_view name, CompileContext &context);

static std::unique_ptr<EnumDecl>
ParseEnumDecl(Lexer &lexer, std::string_view name, CompileContext &context);

static std::unique_ptr<FunctionDecl>
ParseFunctionDecl(Lexer &lexer, ParsedType type, StorageClass storage_class,
                  CompileContext &context);

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

static void ParseTypeSpecifier(DeclarationSpecifier &decl_spec, Lexer &lexer,
                               CompileContext &context) {
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
    MLongDouble = 1 << 8,
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
                        MLong | MLongLong | MLongDouble);
  };

  while (true) {
    Token tok = lexer.PeekNextToken();
    if (tok.type == TokenType::Identifier) {
      if (has_nominal_type() || has_builtin_type() || saw_signed ||
          saw_unsigned) {
        break;
      }
      spec.type_name = context.Sources().GetText(tok);
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
    case TokenType::KwExtern:
      if (decl_spec.storage_class != StorageClass::None) {
        context.Diagnostics().Fail(tok.location,
                                   errs::MultipleStorageClasses{});
      }
      decl_spec.storage_class = tok.type == TokenType::KwStatic
                                    ? StorageClass::Static
                                    : StorageClass::Extern;
      lexer.ConsumeToken(tok);
      break;
    case TokenType::KwConst:
      spec.is_const = true;
      lexer.ConsumeToken(tok);
      break;
    case TokenType::KwVolatile:
      spec.is_volatile = true;
      lexer.ConsumeToken(tok);
      break;
    case TokenType::KwRestrict:
      spec.is_restrict = true;
      lexer.ConsumeToken(tok);
      break;
    case TokenType::KwSigned:
    case TokenType::KwUnsigned: {
      const bool is_unsigned = tok.type == TokenType::KwUnsigned;
      if (saw_signed && saw_unsigned) {
        context.Diagnostics().Fail(tok.location,
                                   errs::SignedAndUnsignedTogether{});
      }
      if (is_unsigned ? saw_unsigned : saw_signed) {
        context.Diagnostics().Fail(tok.location,
                                   errs::DuplicateSignednessSpecifier{});
      }
      saw_signed |= !is_unsigned;
      saw_unsigned |= is_unsigned;
      lexer.ConsumeToken(tok);
      break;
    }
    case TokenType::KwStruct: {
      if (has_nominal_type() || has_builtin_type()) {
        context.Diagnostics().Fail(tok.location, errs::MultipleBaseTypes{});
      }
      spec.is_struct = true;
      lexer.ConsumeToken(tok);
      Token struct_token = tok;
      tok = lexer.PeekNextToken();
      if (tok.type == TokenType::Identifier) {
        spec.type_name = context.Sources().GetText(tok);
        lexer.ConsumeToken(tok);
        if (lexer.PeekNextToken().type == TokenType::LBrace ||
            lexer.PeekNextToken().type == TokenType::Semicolon) {
          auto struct_decl =
              ParseStructDecl(lexer, context.Sources().GetText(tok), context);
          struct_decl->location = CombineLocation(
              struct_token.location, lexer.GetLastConsumedLocation());
          decl_spec.decl = std::move(struct_decl);
        }
      } else if (tok.type == TokenType::LBrace) {
        std::string_view name = GenerateAnoymousSymbol("struct");
        decl_spec.decl = ParseStructDecl(lexer, name, context);
        decl_spec.decl->location = CombineLocation(
            struct_token.location, lexer.GetLastConsumedLocation());
        spec.type_name = name;
      } else {
        RaiseParserError(context, tok.location,
                         errs::ExpectedStructNameOrDefinition{});
      }
      break;
    }
    case TokenType::KwUnion: {
      if (has_nominal_type() || has_builtin_type()) {
        context.Diagnostics().Fail(tok.location, errs::MultipleBaseTypes{});
      }
      spec.is_union = true;
      lexer.ConsumeToken(tok);
      Token union_token = tok;
      tok = lexer.PeekNextToken();
      if (tok.type == TokenType::Identifier) {
        spec.type_name = context.Sources().GetText(tok);
        lexer.ConsumeToken(tok);
        if (lexer.PeekNextToken().type == TokenType::LBrace ||
            lexer.PeekNextToken().type == TokenType::Semicolon) {
          auto union_decl =
              ParseUnionDecl(lexer, context.Sources().GetText(tok), context);
          union_decl->location = CombineLocation(
              union_token.location, lexer.GetLastConsumedLocation());
          decl_spec.decl = std::move(union_decl);
        }
      } else if (tok.type == TokenType::LBrace) {
        std::string_view name = GenerateAnoymousSymbol("union");
        decl_spec.decl = ParseUnionDecl(lexer, name, context);
        decl_spec.decl->location = CombineLocation(
            union_token.location, lexer.GetLastConsumedLocation());
        spec.type_name = name;
      } else {
        RaiseParserError(context, tok.location,
                         errs::ExpectedUnionNameOrDefinition{});
      }
      break;
    }
    case TokenType::KwEnum:
      if (has_nominal_type() || has_builtin_type()) {
        context.Diagnostics().Fail(tok.location, errs::MultipleBaseTypes{});
      }
      spec.is_enum = true;
      lexer.ConsumeToken(tok);
      {
        Token enum_token = tok;
        tok = lexer.PeekNextToken();
        if (tok.type == TokenType::Identifier) {
          spec.type_name = context.Sources().GetText(tok);
          lexer.ConsumeToken(tok);
          if (lexer.PeekNextToken().type == TokenType::LBrace ||
              lexer.PeekNextToken().type == TokenType::Colon ||
              lexer.PeekNextToken().type == TokenType::Semicolon) {
            auto enum_decl =
                ParseEnumDecl(lexer, context.Sources().GetText(tok), context);
            enum_decl->location = CombineLocation(
                enum_token.location, lexer.GetLastConsumedLocation());
            decl_spec.decl = std::move(enum_decl);
          }
        } else if (tok.type == TokenType::LBrace ||
                   lexer.PeekNextToken().type == TokenType::Colon) {
          std::string_view name = GenerateAnoymousSymbol("enum");
          decl_spec.decl = ParseEnumDecl(lexer, name, context);
          decl_spec.decl->location = CombineLocation(
              enum_token.location, lexer.GetLastConsumedLocation());
          spec.type_name = name;
        } else {
          RaiseParserError(context, tok.location,
                           errs::ExpectedEnumNameOrDefinition{});
        }
      }
      break;
    case TokenType::KwRegister:
      context.Diagnostics().Fail(
          tok.location,
          errs::UnsupportedStorageClass{context.Sources().GetText(tok)});
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
        } else if (type_mask & MDouble) {
          type_mask &= ~MDouble;
          type_mask |= MLongDouble;
        } else {
          if (type_mask & MLongLong) {
            context.Diagnostics().Fail(tok.location,
                                       errs::TooManyLongSpecifiers{});
          } else {
            type_mask |= MLong;
          }
        }
        lexer.ConsumeToken(tok);
        break;
      } else if (tok.type == TokenType::KwDouble) {
        if (type_mask & MLong) {
          type_mask &= ~MLong;
          type_mask |= MLongDouble;
        } else if ((type_mask & MDouble) || (type_mask & MLongDouble)) {
          context.Diagnostics().Fail(tok.location,
                                     errs::DuplicateTypeSpecifier{});
        } else {
          type_mask |= MDouble;
        }
        lexer.ConsumeToken(tok);
        break;
      } else if (it != tok_type_to_mask.end()) {
        if (has_nominal_type()) {
          context.Diagnostics().Fail(tok.location, errs::MultipleBaseTypes{});
        }
        int mask = it->second;
        if ((type_mask & mask) != 0) {
          context.Diagnostics().Fail(tok.location,
                                     errs::DuplicateTypeSpecifier{});
        }
        type_mask |= mask;
        lexer.ConsumeToken(tok);
        break;
      } else {
        goto end;
      }
    }
  }

end:;
  if (saw_signed && saw_unsigned) {
    context.Diagnostics().Fail(spec.location,
                               errs::SignedAndUnsignedTogether{});
  }
  if (has_nominal_type() && (saw_signed || saw_unsigned)) {
    context.Diagnostics().Fail(spec.location,
                               errs::SignednessCannotQualifyTag{});
  }

  if (has_nominal_type()) {
    if (has_builtin_type()) {
      context.Diagnostics().Fail(spec.location, errs::MultipleBaseTypes{});
    }
    return;
  }

  if ((type_mask & (MShort | MLong | MLongLong)) && (type_mask & MInt)) {
    type_mask &= ~MInt;
  }

  if (!std::has_single_bit(type_mask)) {
    if (saw_signed) {
      type_mask |= MInt;
    } else if (saw_unsigned) {
      type_mask |= MInt;
    } else {
      context.Diagnostics().Fail(spec.location,
                                 errs::InvalidTypeSpecifierCombination{});
    }
  }

  constexpr unsigned int integer_mask =
      MChar | MShort | MInt | MLong | MLongLong;
  if (!(type_mask & integer_mask) && (saw_unsigned || saw_signed)) {
    context.Diagnostics().Fail(spec.location,
                               errs::SignednessRequiresIntegerType{});
  }

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
  case MLongDouble:
    spec.type_name = "long double";
    break;
  default:
    RAISE_ERROR("Unknown type mask: ", type_mask);
  }
}

DeclarationSpecifier ParseDeclarationSpecifier(Lexer &lexer,
                                               CompileContext &context) {
  DeclarationSpecifier decl_spec;
  decl_spec.type_spec.location = lexer.PeekNextToken().location;
  ParseTypeSpecifier(decl_spec, lexer, context);
  return decl_spec;
}

static std::unique_ptr<Declarator> ParseDeclarator(Lexer &lexer,
                                                   CompileContext &context);

static ParamListItem ParseParamListItem(Lexer &lexer, CompileContext &context) {
  ParamListItem item;
  item.specifier = ParseDeclarationSpecifier(lexer, context);
  if (item.specifier.storage_class != StorageClass::None) {
    context.Diagnostics().Fail(item.specifier.type_spec.location,
                               errs::ParameterCannotHaveStorageClass{});
  }
  if (item.specifier.type_spec.is_typedef) {
    context.Diagnostics().Fail(item.specifier.type_spec.location,
                               errs::ParameterCannotBeTypedef{});
  }
  Token tok = lexer.PeekNextToken();
  if (tok.type != TokenType::RParen && tok.type != TokenType::Comma) {
    item.declarator = ParseDeclarator(lexer, context);
  }
  return item;
}

static std::unique_ptr<ParamList> ParseParamList(Lexer &lexer,
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
      Token next = lexer.PeekNextToken();
      if (next.type != TokenType::RParen) {
        RaiseParserError(context, next.location,
                         errs::UnexpectedTokenAfterEllipsis{});
      }
      break;
    }
    param_list->params.push_back(ParseParamListItem(lexer, context));
    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::RParen) {
      break;
    }
    if (tok.type != TokenType::Comma) {
      RaiseParserError(context, tok.location, errs::ExpectedParameterComma{});
    }
    lexer.ConsumeToken(tok);
  }
  if (param_list->params.size() == 1 &&
      param_list->params[0].specifier.type_spec.type_name == "void" &&
      !param_list->params[0].declarator) {
    param_list->params.clear(); // f(void) -> f()
  }
  return param_list;
}

static std::unique_ptr<ArraySuffix> ParseArraySuffix(Lexer &lexer,
                                                     CompileContext &context) {
  auto array_suffix = std::make_unique<ArraySuffix>();
  Token start_token = lexer.PeekNextToken();
  array_suffix->location = start_token.location;
  if (lexer.PeekNextToken().type == TokenType::RBracket) {
    return array_suffix;
  }
  Token tok = lexer.PeekNextToken();
  if (tok.type != TokenType::LiteralNum) {
    RaiseParserError(context, tok.location, errs::ExpectedArraySizeNumber{});
  }
  lexer.ConsumeToken(tok);
  array_suffix->size = std::stoul(std::string(context.Sources().GetText(tok)));
  array_suffix->location = CombineLocation(start_token.location, tok.location);
  return array_suffix;
}

static std::unique_ptr<DirectDeclarator>
ParseDirectDeclarator(Lexer &lexer, CompileContext &context) {
  auto direct_declarator = std::make_unique<DirectDeclarator>();
  Token tok = lexer.PeekNextToken();
  Token start_token = tok;
  if (tok.type == TokenType::Identifier) {
    lexer.ConsumeToken(tok);
    direct_declarator->declarator =
        std::make_unique<IdentifierDeclarator>(context.Sources().GetText(tok));
    direct_declarator->declarator->location = tok.location;
  } else if (tok.type == TokenType::LParen) {
    lexer.ConsumeToken(tok);
    direct_declarator->declarator = ParseDeclarator(lexer, context);
    Token rparen = lexer.PeekNextToken();
    if (rparen.type != TokenType::RParen) {
      RaiseParserError(context, rparen.location, errs::ExpectedClosingParen{});
    }
    lexer.ConsumeToken(rparen);
  } else if (tok.type != TokenType::LBracket) {
    return nullptr;
  }

  while (true) {
    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::LParen) {
      lexer.ConsumeToken(tok);
      direct_declarator->suffixes.push_back(ParseParamList(lexer, context));
      Token rparen = lexer.PeekNextToken();
      if (rparen.type != TokenType::RParen) {
        RaiseParserError(context, rparen.location,
                         errs::ExpectedClosingParen{});
      }
      lexer.ConsumeToken(rparen);
      direct_declarator->suffixes.back()->location =
          CombineLocation(tok.location, rparen.location);
    } else if (tok.type == TokenType::LBracket) {
      lexer.ConsumeToken(tok);
      direct_declarator->suffixes.push_back(ParseArraySuffix(lexer, context));
      Token rbracket = lexer.PeekNextToken();
      if (rbracket.type != TokenType::RBracket) {
        RaiseParserError(context, rbracket.location,
                         errs::ExpectedClosingBracket{});
      }
      lexer.ConsumeToken(rbracket);
      direct_declarator->suffixes.back()->location =
          CombineLocation(tok.location, rbracket.location);
    } else {
      break;
    }
  }

  direct_declarator->location =
      CombineLocation(start_token.location, lexer.GetLastConsumedLocation());
  return direct_declarator;
}

static std::unique_ptr<Declarator> ParseDeclarator(Lexer &lexer,
                                                   CompileContext &context) {
  auto declarator = std::make_unique<Declarator>();
  Token start_token = lexer.PeekNextToken();
  while (true) {
    Token tok = lexer.PeekNextToken();
    if (tok.type == TokenType::Star) {
      lexer.ConsumeToken(tok);
      Declarator::PointerQualifier qualifier;
      while (IsTypeQualifierKeyword(lexer.PeekNextToken().type)) {
        Token qualifier_token = lexer.ConsumeNextToken();
        switch (qualifier_token.type) {
        case TokenType::KwConst:
          qualifier.is_const = true;
          break;
        case TokenType::KwVolatile:
          qualifier.is_volatile = true;
          break;
        case TokenType::KwRestrict:
          qualifier.is_restrict = true;
          break;
        default:
          RAISE_ERROR("Unexpected pointer qualifier");
        }
      }
      declarator->pointers.push_back(qualifier);
    } else {
      break;
    }
  }
  declarator->direct_declarator = ParseDirectDeclarator(lexer, context);
  declarator->location =
      CombineLocation(start_token.location, lexer.GetLastConsumedLocation());
  return declarator;
}

static std::unique_ptr<ValueInitializer>
ParseValueInitializer(Lexer &lexer, CompileContext &context) {
  auto value_initializer = std::make_unique<ValueInitializer>();
  value_initializer->value = ParseExpr(lexer, context);
  value_initializer->location = value_initializer->value
                                    ? value_initializer->value->location
                                    : lexer.GetLastConsumedLocation();
  return value_initializer;
}

static std::unique_ptr<FieldInitializer>
ParseFieldInitializer(Lexer &lexer, CompileContext &context) {
  auto initializer = std::make_unique<FieldInitializer>();
  Token start_token = lexer.PeekNextToken();
  while (true) {
    Token tok = lexer.PeekNextToken();
    if (tok.type == TokenType::RBrace) {
      break;
    }
    if (tok.type == TokenType::Comma) {
      lexer.ConsumeToken(tok);
      tok = lexer.PeekNextToken();
    }
    if (tok.type != TokenType::Dot) {
      RaiseParserError(context, tok.location,
                       errs::ExpectedFieldInitializerDot{});
    }
    lexer.ConsumeToken(tok);

    tok = lexer.PeekNextToken();
    if (tok.type != TokenType::Identifier) {
      RaiseParserError(context, tok.location,
                       errs::ExpectedFieldInitializerIdentifier{});
    }
    lexer.ConsumeToken(tok);
    std::string_view field_name = context.Sources().GetText(tok);

    Token equal = lexer.PeekNextToken();
    if (equal.type != TokenType::Equal) {
      RaiseParserError(context, equal.location,
                       errs::ExpectedFieldInitializerEqual{});
    }
    lexer.ConsumeToken(equal);

    auto value_initializer = ParseValueInitializer(lexer, context);
    initializer->field_initializers.push_back(
        {field_name, std::move(value_initializer)});
  }
  initializer->location =
      CombineLocation(start_token.location, lexer.GetLastConsumedLocation());
  return initializer;
}

static std::unique_ptr<ArrayInitializer>
ParseArrayInitializer(Lexer &lexer, CompileContext &context) {
  auto initializer = std::make_unique<ArrayInitializer>();
  Token start_token = lexer.PeekNextToken();
  while (true) {
    if (lexer.PeekNextToken().type == TokenType::RBrace) {
      break;
    }
    if (initializer->initializers.size()) {
      Token comma = lexer.PeekNextToken();
      if (comma.type != TokenType::Comma) {
        RaiseParserError(context, comma.location,
                         errs::ExpectedArrayInitializerComma{});
      }
      lexer.ConsumeToken(comma);
    }
    if (lexer.PeekNextToken().type == TokenType::RBrace) {
      break;
    }
    auto val_initializer = ParseValueInitializer(lexer, context);
    initializer->initializers.push_back(std::move(val_initializer));
  }
  initializer->location =
      CombineLocation(start_token.location, lexer.GetLastConsumedLocation());
  return initializer;
}

static std::unique_ptr<Initializer> ParseInitializer(Lexer &lexer,
                                                     CompileContext &context) {
  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::EndOfFile || tok.type == TokenType::Semicolon ||
      tok.type == TokenType::Comma) {
    RaiseParserError(context, tok.location, errs::UnexpectedToken{});
  }
  if (tok.type == TokenType::LBrace) {
    lexer.ConsumeToken(tok);
    Token lbrace = tok;
    if (lexer.PeekNextToken().type == TokenType::Dot) {
      auto res = ParseFieldInitializer(lexer, context);
      Token rbrace = lexer.PeekNextToken();
      if (rbrace.type != TokenType::RBrace) {
        RaiseParserError(context, rbrace.location,
                         errs::ExpectedClosingBracePhrase{});
      }
      lexer.ConsumeToken(rbrace);
      res->location = CombineLocation(lbrace.location, rbrace.location);
      return res;
    } else {
      auto res = ParseArrayInitializer(lexer, context);
      Token rbrace = lexer.PeekNextToken();
      if (rbrace.type != TokenType::RBrace) {
        RaiseParserError(context, rbrace.location,
                         errs::ExpectedClosingBracePhrase{});
      }
      lexer.ConsumeToken(rbrace);
      res->location = CombineLocation(lbrace.location, rbrace.location);
      return res;
    }
  } else {
    return ParseValueInitializer(lexer, context);
  }
}

bool IsDeclarationSpecifier(Token tok, const CompileContext &context) {
  if (IsDeclarationSpecifierKeyword(tok.type)) {
    return true;
  }
  if (tok.type == TokenType::Identifier) {
    std::string_view name = context.Sources().GetText(tok);
    if (context.Is(name, ContextItemType::Typedef) ||
        context.Is(name, ContextItemType::Enum)) {
      return true;
    }
  }
  return false;
}

static bool IsDeclaratorStart(Token tok) {
  switch (tok.type) {
  case TokenType::Identifier:
  case TokenType::Star:
  case TokenType::LParen:
  case TokenType::LBracket:
    return true;
  default:
    return false;
  }
}

TypeRef ParseTypeName(Lexer &lexer, CompileContext &context) {
  DeclarationSpecifier decl_spec = ParseDeclarationSpecifier(lexer, context);
  Token tok = lexer.PeekNextToken();
  if (!IsDeclaratorStart(tok)) {
    TypeRef base_type = context.GetType(decl_spec.type_spec);
    if (!base_type) {
      context.Diagnostics().Fail(
          decl_spec.type_spec.location,
          errs::UnknownTagName{decl_spec.type_spec.type_name});
    }
    return base_type;
  }

  auto declarator = ParseDeclarator(lexer, context);
  ParsedType parsed =
      ParseDeclaratorType(decl_spec.type_spec, *declarator, context);
  tok = lexer.PeekNextToken();
  if (tok.type == TokenType::Equal) {
    RaiseParserError(context, tok.location, errs::UnexpectedInitializer{});
  }
  return parsed.type;
}

std::unique_ptr<ASTNode> ParseDeclaration(Lexer &lexer,
                                          CompileContext &context) {
  DeclarationSpecifier decl_spec = ParseDeclarationSpecifier(lexer, context);
  DeclStmtBuilder decl_stmt_builder;
  if (decl_spec.decl) {
    decl_stmt_builder.Add(std::move(decl_spec.decl));
  }

  if (decl_spec.type_spec.is_typedef &&
      decl_spec.storage_class != StorageClass::None) {
    context.Diagnostics().Fail(decl_spec.type_spec.location,
                               errs::TypedefCannotHaveStorageClass{});
  }

  Token tok = lexer.PeekNextToken();
  if (tok.type == TokenType::Semicolon || tok.type == TokenType::LBrace) {
    if (!decl_spec.type_spec.is_typedef &&
        decl_spec.storage_class != StorageClass::None) {
      context.Diagnostics().Fail(decl_spec.type_spec.location,
                                 errs::StorageClassRequiresDeclarator{});
    }
    return decl_stmt_builder.Build();
  }

  while (true) {
    auto declarator = ParseDeclarator(lexer, context);
    ParsedType parsed_type =
        ParseDeclaratorType(decl_spec.type_spec, *declarator, context);
    if (parsed_type.declarator_name.empty()) {
      context.Diagnostics().Fail(declarator->location,
                                 errs::ExpectedDeclaratorIdentifier{});
    }

    if (decl_spec.type_spec.is_typedef) {
      auto typedef_decl = std::make_unique<TypedefDecl>();
      typedef_decl->location = parsed_type.location;
      typedef_decl->name = parsed_type.declarator_name;
      typedef_decl->type = parsed_type.type;
      context.AddTypedef(typedef_decl.get());
      decl_stmt_builder.Add(std::move(typedef_decl));
    } else if (parsed_type.type->GetType() == TypeBaseType::FunctionType) {
      auto function_decl = ParseFunctionDecl(lexer, std::move(parsed_type),
                                             decl_spec.storage_class, context);
      const bool has_body = function_decl->body != nullptr;
      decl_stmt_builder.Add(std::move(function_decl));
      if (has_body) {
        break;
      }
    } else {
      auto var_decl = std::make_unique<VarDecl>();
      var_decl->location = parsed_type.location;
      var_decl->name = parsed_type.declarator_name;
      var_decl->type = parsed_type.type;
      var_decl->storage_class = decl_spec.storage_class;
      VarDecl *var_decl_ref = var_decl.get();
      context.AddVariable(var_decl_ref);
      decl_stmt_builder.Add(std::move(var_decl));

      tok = lexer.PeekNextToken();
      if (tok.type == TokenType::Equal) {
        lexer.ConsumeToken(tok);
        var_decl_ref->initializer = ParseInitializer(lexer, context);
        var_decl_ref->location = CombineLocation(
            var_decl_ref->location, var_decl_ref->initializer->location);
      }
    }

    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::Semicolon || tok.type == TokenType::LBrace ||
        tok.type == TokenType::EndOfFile || tok.type == TokenType::RParen) {
      break;
    }
    if (tok.type != TokenType::Comma) {
      if (IsDeclarationSpecifier(tok, context)) {
        RaiseParserError(context, tok.location,
                         errs::ExpectedSemicolonAfterDeclarationPunctuated{});
      }
      RaiseParserError(context, tok.location, errs::ExpectedDeclarationComma{});
    }
    lexer.ConsumeToken(tok);
  }
  return decl_stmt_builder.Build();
}

static std::unique_ptr<StructDecl>
ParseStructDecl(Lexer &lexer, std::string_view name, CompileContext &context) {
  auto struct_decl = std::make_unique<StructDecl>();
  struct_decl->name = name;
  struct_decl->type =
      context.Types().GetOrCreateStruct(context.CurrentScopeId(), name);
  context.AddStruct(struct_decl.get());
  if (lexer.PeekNextToken().type == TokenType::LBrace) {
    struct_decl->fields = ParseCompoundStmt(lexer, context);
    std::vector<StructField> fields;
    for (const auto &statement : struct_decl->fields->statements) {
      if (statement->GetType() == ASTNodeType::DeclStmt) {
        for (const auto &decl : statement->As<DeclStmt>()->decls) {
          if (decl->GetType() != ASTNodeType::VarDecl) {
            context.Diagnostics().Fail(decl->location,
                                       errs::StructFieldsRequireVariables{});
          }
          auto *field = decl->As<VarDecl>();
          if (field->storage_class != StorageClass::None) {
            context.Diagnostics().Fail(
                field->location, errs::StructFieldCannotHaveStorageClass{});
          }
          fields.push_back(
              {.name = std::string(field->name), .type = field->type});
        }
      } else {
        if (statement->GetType() != ASTNodeType::VarDecl) {
          context.Diagnostics().Fail(statement->location,
                                     errs::StructFieldsRequireVariables{});
        }
        auto *field = statement->As<VarDecl>();
        if (field->storage_class != StorageClass::None) {
          context.Diagnostics().Fail(field->location,
                                     errs::StructFieldCannotHaveStorageClass{});
        }
        fields.push_back(
            {.name = std::string(field->name), .type = field->type});
      }
    }
    context.Types().CompleteStruct(struct_decl->type, fields);
  } else {
    Token tok = lexer.PeekNextToken();
    if (tok.type != TokenType::Semicolon) {
      RaiseParserError(context, tok.location, errs::ExpectedSemicolon{});
    }
  }
  return struct_decl;
}

static std::unique_ptr<UnionDecl>
ParseUnionDecl(Lexer &lexer, std::string_view name, CompileContext &context) {
  auto union_decl = std::make_unique<UnionDecl>();
  union_decl->name = name;
  union_decl->type =
      context.Types().GetOrCreateUnion(context.CurrentScopeId(), name);
  context.AddUnion(union_decl.get());
  if (lexer.PeekNextToken().type == TokenType::LBrace) {
    union_decl->fields = ParseCompoundStmt(lexer, context);
    std::vector<UnionField> fields;
    auto add_field = [&](const ASTNode *decl) {
      if (auto field = decl->DynCast<VarDecl>()) {
        if (field->storage_class != StorageClass::None) {
          context.Diagnostics().Fail(field->location,
                                     errs::UnionFieldCannotHaveStorageClass{});
        }
        fields.push_back({.name = field->name, .type = field->type});
      } else {
        if (decl->GetType() != ASTNodeType::StructDecl &&
            decl->GetType() != ASTNodeType::UnionDecl &&
            decl->GetType() != ASTNodeType::EnumDecl) {
          context.Diagnostics().Fail(decl->location,
                                     errs::UnionFieldsRequireVariables{});
        }
      }
    };
    for (const auto &statement : union_decl->fields->statements) {
      if (statement->GetType() == ASTNodeType::DeclStmt) {
        for (const auto &decl : statement->As<DeclStmt>()->decls) {
          add_field(decl.get());
        }
      } else {
        add_field(statement.get());
      }
    }
    context.Types().CompleteUnion(union_decl->type, fields);
  } else {
    Token tok = lexer.PeekNextToken();
    if (tok.type != TokenType::Semicolon) {
      RaiseParserError(context, tok.location, errs::ExpectedSemicolon{});
    }
  }
  return union_decl;
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
      context.Diagnostics().Error(tok.location,
                                  errs::ExpectedClosingBraceAfterEnum{});
      return;
    }

    if (tok.type != TokenType::Identifier) {
      RaiseParserError(context, tok.location, errs::ExpectedEnumIdentifier{});
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
    context.AddEnumerator(decl->values.back().get());
    tok = lexer.PeekNextToken();
    if (tok.type == TokenType::Comma) {
      lexer.ConsumeToken(tok);
    }
  }
}

static std::unique_ptr<EnumDecl>
ParseEnumDecl(Lexer &lexer, std::string_view name, CompileContext &context) {
  auto enum_decl = std::make_unique<EnumDecl>();
  enum_decl->name = name;
  enum_decl->type =
      context.Types().GetOrCreateEnum(context.CurrentScopeId(), name);
  context.AddEnum(enum_decl.get());
  if (lexer.PeekNextToken().type == TokenType::Colon) {
    Token colon = lexer.ConsumeNextToken();
    TypeRef parsed_type = ParseTypeName(lexer, context);
    if ((parsed_type.get_tag() & TypeQualifierTags) != 0) {
      context.Diagnostics().Fail(colon.location,
                                 errs::UnexpectedEnumUnderlyingSpecifier{});
    }
    if (!IsIntegerType(parsed_type)) {
      context.Diagnostics().Fail(colon.location,
                                 errs::ExpectedIntegerEnumUnderlyingType{});
    }
    enum_decl->underlying_type = parsed_type;
  } else {
    enum_decl->underlying_type = context.Types().getInt();
  }
  if (lexer.PeekNextToken().type == TokenType::LBrace) {
    enum_decl->is_definition = true;
    lexer.ConsumeNextToken();
    ParseEnumContent(lexer, context, enum_decl.get());
  } else {
    Token tok = lexer.PeekNextToken();
    if (tok.type != TokenType::Semicolon) {
      RaiseParserError(context, tok.location, errs::ExpectedSemicolon{});
    }
  }
  return enum_decl;
}

static std::unique_ptr<FunctionDecl>
ParseFunctionDecl(Lexer &lexer, ParsedType type, StorageClass storage_class,
                  CompileContext &context) {
  auto function_decl = std::make_unique<FunctionDecl>();
  function_decl->location = type.location;
  function_decl->name = type.declarator_name;
  function_decl->storage_class = storage_class;
  function_decl->function_type = type.type;
  function_decl->parameters.reserve(type.parameters.size());
  for (auto &parameter : type.parameters) {
    auto parameter_decl = std::make_unique<ParamDecl>();
    parameter_decl->location = parameter.location;
    parameter_decl->name = parameter.name;
    parameter_decl->type = parameter.type;
    function_decl->parameters.push_back(std::move(parameter_decl));
  }
  context.AddFunction(function_decl.get());

  CompileContextLayerGuard layer_guard(context);
  for (const auto &parameter : function_decl->parameters) {
    context.AddParameter(parameter.get());
  }
  if (lexer.PeekNextToken().type == TokenType::LBrace) {
    function_decl->body = ParseCompoundStmt(lexer, context);
    function_decl->location =
        CombineLocation(function_decl->location, function_decl->body->location);
  } else {
    Token tok = lexer.PeekNextToken();
    if (tok.type != TokenType::Semicolon && tok.type != TokenType::Comma &&
        tok.type != TokenType::EndOfFile && tok.type != TokenType::RParen) {
      RaiseParserError(context, tok.location,
                       errs::ExpectedFunctionBodyOrSemicolon{});
    }
  }
  return function_decl;
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
