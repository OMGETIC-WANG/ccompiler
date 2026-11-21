#include "Declaration.h"
#include "../Util/Util.h"
#include "AST.h"
#include "Diagnostics/Errors.generated.h"
#include "Expr.h"
#include "ParserError.h"
#include "TranslationUnit.h"
#include <list>

static OptUnique<StructDecl>
ParseStructDecl(Lexer &lexer, std::string_view name, CompileContext &context);

static OptUnique<UnionDecl> ParseUnionDecl(Lexer &lexer, std::string_view name,
                                           CompileContext &context);

static OptUnique<EnumDecl> ParseEnumDecl(Lexer &lexer, std::string_view name,
                                         CompileContext &context);

static OptUnique<FunctionDecl> ParseFunctionDecl(Lexer &lexer, ParsedType type,
                                                 StorageClass storage_class,
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

static std::optional<std::monostate>
ParseTypeSpecifier(DeclarationSpecifier &decl_spec, Lexer &lexer,
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
  auto fail = [&](SourceLocation location,
                  auto &&error) -> std::optional<std::monostate> {
    context.Diagnostics().Error(location, std::forward<decltype(error)>(error));
    return std::nullopt;
  };
  Token next;

  while (true) {
    TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
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
        return fail(tok.location, errs::MultipleStorageClasses{});
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
        return fail(tok.location, errs::SignedAndUnsignedTogether{});
      }
      if (is_unsigned ? saw_unsigned : saw_signed) {
        return fail(tok.location, errs::DuplicateSignednessSpecifier{});
      }
      saw_signed |= !is_unsigned;
      saw_unsigned |= is_unsigned;
      lexer.ConsumeToken(tok);
      break;
    }
    case TokenType::KwStruct: {
      if (has_nominal_type() || has_builtin_type()) {
        return fail(tok.location, errs::MultipleBaseTypes{});
      }
      spec.is_struct = true;
      lexer.ConsumeToken(tok);
      Token struct_token = tok;
      TRY_ASSIGN(tok, lexer.PeekNextToken());
      if (tok.type == TokenType::Identifier) {
        spec.type_name = context.Sources().GetText(tok);
        lexer.ConsumeToken(tok);
        TRY_ASSIGN(next, lexer.PeekNextToken());
        if (next.type == TokenType::LBrace ||
            next.type == TokenType::Semicolon) {
          TRY_ASSIGN_NEW(
              struct_decl,
              ParseStructDecl(lexer, context.Sources().GetText(tok), context));
          struct_decl->location = CombineLocation(
              struct_token.location, lexer.GetLastConsumedLocation());
          decl_spec.decl = std::move(struct_decl);
        }
      } else if (tok.type == TokenType::LBrace) {
        std::string_view name = GenerateAnoymousSymbol("struct");
        TRY_ASSIGN_NEW(struct_decl, ParseStructDecl(lexer, name, context));
        decl_spec.decl = std::move(struct_decl);
        decl_spec.decl->location = CombineLocation(
            struct_token.location, lexer.GetLastConsumedLocation());
        spec.type_name = name;
      } else {
        ReportParserError(context, tok.location,
                          errs::ExpectedStructNameOrDefinition{});
        return std::nullopt;
      }
      break;
    }
    case TokenType::KwUnion: {
      if (has_nominal_type() || has_builtin_type()) {
        return fail(tok.location, errs::MultipleBaseTypes{});
      }
      spec.is_union = true;
      lexer.ConsumeToken(tok);
      Token union_token = tok;
      TRY_ASSIGN(tok, lexer.PeekNextToken());
      if (tok.type == TokenType::Identifier) {
        spec.type_name = context.Sources().GetText(tok);
        lexer.ConsumeToken(tok);
        TRY_ASSIGN(next, lexer.PeekNextToken());
        if (next.type == TokenType::LBrace ||
            next.type == TokenType::Semicolon) {
          TRY_ASSIGN_NEW(
              union_decl,
              ParseUnionDecl(lexer, context.Sources().GetText(tok), context));
          union_decl->location = CombineLocation(
              union_token.location, lexer.GetLastConsumedLocation());
          decl_spec.decl = std::move(union_decl);
        }
      } else if (tok.type == TokenType::LBrace) {
        std::string_view name = GenerateAnoymousSymbol("union");
        TRY_ASSIGN_NEW(union_decl, ParseUnionDecl(lexer, name, context));
        decl_spec.decl = std::move(union_decl);
        decl_spec.decl->location = CombineLocation(
            union_token.location, lexer.GetLastConsumedLocation());
        spec.type_name = name;
      } else {
        ReportParserError(context, tok.location,
                          errs::ExpectedUnionNameOrDefinition{});
        return std::nullopt;
      }
      break;
    }
    case TokenType::KwEnum:
      if (has_nominal_type() || has_builtin_type()) {
        return fail(tok.location, errs::MultipleBaseTypes{});
      }
      spec.is_enum = true;
      lexer.ConsumeToken(tok);
      {
        Token enum_token = tok;
        TRY_ASSIGN(tok, lexer.PeekNextToken());
        if (tok.type == TokenType::Identifier) {
          spec.type_name = context.Sources().GetText(tok);
          lexer.ConsumeToken(tok);
          TRY_ASSIGN(next, lexer.PeekNextToken());
          if (next.type == TokenType::LBrace || next.type == TokenType::Colon ||
              next.type == TokenType::Semicolon) {
            TRY_ASSIGN_NEW(
                enum_decl,
                ParseEnumDecl(lexer, context.Sources().GetText(tok), context));
            enum_decl->location = CombineLocation(
                enum_token.location, lexer.GetLastConsumedLocation());
            decl_spec.decl = std::move(enum_decl);
          }
        } else {
          TRY_ASSIGN(next, lexer.PeekNextToken());
        }
        if (tok.type != TokenType::Identifier &&
            (tok.type == TokenType::LBrace || next.type == TokenType::Colon)) {
          std::string_view name = GenerateAnoymousSymbol("enum");
          TRY_ASSIGN_NEW(enum_decl, ParseEnumDecl(lexer, name, context));
          decl_spec.decl = std::move(enum_decl);
          decl_spec.decl->location = CombineLocation(
              enum_token.location, lexer.GetLastConsumedLocation());
          spec.type_name = name;
        } else if (tok.type != TokenType::Identifier) {
          ReportParserError(context, tok.location,
                            errs::ExpectedEnumNameOrDefinition{});
          return std::nullopt;
        }
      }
      break;
    case TokenType::KwRegister:
      return fail(tok.location, errs::UnsupportedStorageClass{
                                    context.Sources().GetText(tok)});
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
            return fail(tok.location, errs::TooManyLongSpecifiers{});
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
          return fail(tok.location, errs::DuplicateTypeSpecifier{});
        } else {
          type_mask |= MDouble;
        }
        lexer.ConsumeToken(tok);
        break;
      } else if (it != tok_type_to_mask.end()) {
        if (has_nominal_type()) {
          return fail(tok.location, errs::MultipleBaseTypes{});
        }
        int mask = it->second;
        if ((type_mask & mask) != 0) {
          return fail(tok.location, errs::DuplicateTypeSpecifier{});
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
    return fail(spec.location, errs::SignedAndUnsignedTogether{});
  }
  if (has_nominal_type() && (saw_signed || saw_unsigned)) {
    return fail(spec.location, errs::SignednessCannotQualifyTag{});
  }

  if (has_nominal_type()) {
    if (has_builtin_type()) {
      return fail(spec.location, errs::MultipleBaseTypes{});
    }
    return std::monostate{};
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
      return fail(spec.location, errs::InvalidTypeSpecifierCombination{});
    }
  }

  constexpr unsigned int integer_mask =
      MChar | MShort | MInt | MLong | MLongLong;
  if (!(type_mask & integer_mask) && (saw_unsigned || saw_signed)) {
    return fail(spec.location, errs::SignednessRequiresIntegerType{});
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
  return std::monostate{};
}

std::optional<DeclarationSpecifier>
ParseDeclarationSpecifier(Lexer &lexer, CompileContext &context) {
  DeclarationSpecifier decl_spec;
  TRY_ASSIGN_NEW(first, lexer.PeekNextToken());
  decl_spec.type_spec.location = first.location;
  TRY(ParseTypeSpecifier(decl_spec, lexer, context));
  return decl_spec;
}

static OptUnique<Declarator> ParseDeclarator(Lexer &lexer,
                                             CompileContext &context);

static std::optional<ParamListItem>
ParseParamListItem(Lexer &lexer, CompileContext &context) {
  ParamListItem item;
  TRY_ASSIGN_NEW(specifier, ParseDeclarationSpecifier(lexer, context));
  item.specifier = std::move(specifier);
  if (item.specifier.storage_class != StorageClass::None) {
    context.Diagnostics().Error(item.specifier.type_spec.location,
                                errs::ParameterCannotHaveStorageClass{});
    return std::nullopt;
  }
  if (item.specifier.type_spec.is_typedef) {
    context.Diagnostics().Error(item.specifier.type_spec.location,
                                errs::ParameterCannotBeTypedef{});
    return std::nullopt;
  }
  TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
  if (tok.type != TokenType::RParen && tok.type != TokenType::Comma) {
    TRY_ASSIGN_NEW(declarator, ParseDeclarator(lexer, context));
    item.declarator = std::move(declarator);
  }
  return item;
}

static OptUnique<ParamList> ParseParamList(Lexer &lexer,
                                           CompileContext &context) {
  auto param_list = std::make_unique<ParamList>();
  Token next;
  TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
  if (tok.type == TokenType::RParen) {
    return param_list;
  }
  while (true) {
    TRY_ASSIGN(tok, lexer.PeekNextToken());
    if (tok.type == TokenType::DotDotDot) {
      param_list->has_va_args = true;
      lexer.ConsumeToken(tok);
      TRY_ASSIGN(next, lexer.PeekNextToken());
      if (next.type != TokenType::RParen) {
        ReportParserError(context, next.location,
                          errs::UnexpectedTokenAfterEllipsis{});
        return std::nullopt;
      }
      break;
    }
    TRY_ASSIGN_NEW(item, ParseParamListItem(lexer, context));
    param_list->params.push_back(std::move(item));
    TRY_ASSIGN(tok, lexer.PeekNextToken());
    if (tok.type == TokenType::RParen) {
      break;
    }
    if (tok.type != TokenType::Comma) {
      ReportParserError(context, tok.location, errs::ExpectedParameterComma{});
      return std::nullopt;
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

static OptUnique<ArraySuffix> ParseArraySuffix(Lexer &lexer,
                                               CompileContext &context) {
  auto array_suffix = std::make_unique<ArraySuffix>();
  Token next;
  TRY_ASSIGN_NEW(start_token, lexer.PeekNextToken());
  array_suffix->location = start_token.location;
  TRY_ASSIGN(next, lexer.PeekNextToken());
  if (next.type == TokenType::RBracket) {
    return array_suffix;
  }
  Token tok = next;
  if (tok.type != TokenType::LiteralNum) {
    ReportParserError(context, tok.location, errs::ExpectedArraySizeNumber{});
    return std::nullopt;
  }
  lexer.ConsumeToken(tok);
  array_suffix->size = std::stoul(std::string(context.Sources().GetText(tok)));
  array_suffix->location = CombineLocation(start_token.location, tok.location);
  return array_suffix;
}

static std::optional<std::unique_ptr<DirectDeclarator>>
ParseDirectDeclarator(Lexer &lexer, CompileContext &context) {
  auto direct_declarator = std::make_unique<DirectDeclarator>();
  Token next;
  TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
  Token start_token = tok;
  if (tok.type == TokenType::Identifier) {
    lexer.ConsumeToken(tok);
    direct_declarator->declarator =
        std::make_unique<IdentifierDeclarator>(context.Sources().GetText(tok));
    direct_declarator->declarator->location = tok.location;
  } else if (tok.type == TokenType::LParen) {
    lexer.ConsumeToken(tok);
    TRY_ASSIGN_NEW(declarator, ParseDeclarator(lexer, context));
    direct_declarator->declarator = std::move(declarator);
    TRY_ASSIGN(next, lexer.PeekNextToken());
    Token rparen = next;
    if (rparen.type != TokenType::RParen) {
      ReportParserError(context, rparen.location, errs::ExpectedClosingParen{});
      return std::nullopt;
    }
    lexer.ConsumeToken(rparen);
  } else if (tok.type != TokenType::LBracket) {
    return std::unique_ptr<DirectDeclarator>{};
  }

  while (true) {
    TRY_ASSIGN(tok, lexer.PeekNextToken());
    if (tok.type == TokenType::LParen) {
      lexer.ConsumeToken(tok);
      TRY_ASSIGN_NEW(param_list, ParseParamList(lexer, context));
      direct_declarator->suffixes.push_back(std::move(param_list));
      TRY_ASSIGN(next, lexer.PeekNextToken());
      Token rparen = next;
      if (rparen.type != TokenType::RParen) {
        ReportParserError(context, rparen.location,
                          errs::ExpectedClosingParen{});
        return std::nullopt;
      }
      lexer.ConsumeToken(rparen);
      direct_declarator->suffixes.back()->location =
          CombineLocation(tok.location, rparen.location);
    } else if (tok.type == TokenType::LBracket) {
      lexer.ConsumeToken(tok);
      TRY_ASSIGN_NEW(array_suffix, ParseArraySuffix(lexer, context));
      direct_declarator->suffixes.push_back(std::move(array_suffix));
      TRY_ASSIGN(next, lexer.PeekNextToken());
      Token rbracket = next;
      if (rbracket.type != TokenType::RBracket) {
        ReportParserError(context, rbracket.location,
                          errs::ExpectedClosingBracket{});
        return std::nullopt;
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

static OptUnique<Declarator> ParseDeclarator(Lexer &lexer,
                                             CompileContext &context) {
  auto declarator = std::make_unique<Declarator>();
  Token next;
  TRY_ASSIGN_NEW(start_token, lexer.PeekNextToken());
  while (true) {
    TRY_ASSIGN(next, lexer.PeekNextToken());
    Token tok = next;
    if (tok.type == TokenType::Star) {
      lexer.ConsumeToken(tok);
      Declarator::PointerQualifier qualifier;
      while (true) {
        TRY_ASSIGN(next, lexer.PeekNextToken());
        if (!IsTypeQualifierKeyword(next.type)) {
          break;
        }
        TRY_ASSIGN_NEW(qualifier_token, lexer.ConsumeNextToken());
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
  TRY_ASSIGN_NEW(direct, ParseDirectDeclarator(lexer, context));
  declarator->direct_declarator = std::move(direct);
  declarator->location =
      CombineLocation(start_token.location, lexer.GetLastConsumedLocation());
  return declarator;
}

static OptUnique<ValueInitializer>
ParseValueInitializer(Lexer &lexer, CompileContext &context) {
  auto value_initializer = std::make_unique<ValueInitializer>();
  TRY_ASSIGN_NEW(value, ParseAssignmentExpr(lexer, context));
  value_initializer->value = std::move(value);
  value_initializer->location = value_initializer->value
                                    ? value_initializer->value->location
                                    : lexer.GetLastConsumedLocation();
  return value_initializer;
}

static OptUnique<FieldInitializer>
ParseFieldInitializer(Lexer &lexer, CompileContext &context) {
  auto initializer = std::make_unique<FieldInitializer>();
  Token next;
  TRY_ASSIGN_NEW(start_token, lexer.PeekNextToken());
  while (true) {
    TRY_ASSIGN(next, lexer.PeekNextToken());
    Token tok = next;
    if (tok.type == TokenType::RBrace) {
      break;
    }
    if (tok.type == TokenType::Comma) {
      lexer.ConsumeToken(tok);
      TRY_ASSIGN(tok, lexer.PeekNextToken());
    }
    if (tok.type != TokenType::Dot) {
      ReportParserError(context, tok.location,
                        errs::ExpectedFieldInitializerDot{});
      return std::nullopt;
    }
    lexer.ConsumeToken(tok);

    TRY_ASSIGN(tok, lexer.PeekNextToken());
    if (tok.type != TokenType::Identifier) {
      ReportParserError(context, tok.location,
                        errs::ExpectedFieldInitializerIdentifier{});
      return std::nullopt;
    }
    lexer.ConsumeToken(tok);
    std::string_view field_name = context.Sources().GetText(tok);

    TRY_ASSIGN(next, lexer.PeekNextToken());
    Token equal = next;
    if (equal.type != TokenType::Equal) {
      ReportParserError(context, equal.location,
                        errs::ExpectedFieldInitializerEqual{});
      return std::nullopt;
    }
    lexer.ConsumeToken(equal);

    TRY_ASSIGN_NEW(value_initializer, ParseValueInitializer(lexer, context));
    initializer->field_initializers.push_back(
        {field_name, std::move(value_initializer)});
  }
  initializer->location =
      CombineLocation(start_token.location, lexer.GetLastConsumedLocation());
  return initializer;
}

static OptUnique<ArrayInitializer>
ParseArrayInitializer(Lexer &lexer, CompileContext &context) {
  auto initializer = std::make_unique<ArrayInitializer>();
  Token next;
  TRY_ASSIGN_NEW(start_token, lexer.PeekNextToken());
  while (true) {
    TRY_ASSIGN(next, lexer.PeekNextToken());
    if (next.type == TokenType::RBrace) {
      break;
    }
    if (initializer->initializers.size()) {
      TRY_ASSIGN(next, lexer.PeekNextToken());
      Token comma = next;
      if (comma.type != TokenType::Comma) {
        ReportParserError(context, comma.location,
                          errs::ExpectedArrayInitializerComma{});
        return std::nullopt;
      }
      lexer.ConsumeToken(comma);
    }
    TRY_ASSIGN(next, lexer.PeekNextToken());
    if (next.type == TokenType::RBrace) {
      break;
    }
    TRY_ASSIGN_NEW(value_initializer, ParseValueInitializer(lexer, context));
    initializer->initializers.push_back(std::move(value_initializer));
  }
  initializer->location =
      CombineLocation(start_token.location, lexer.GetLastConsumedLocation());
  return initializer;
}

static OptUnique<Initializer> ParseInitializer(Lexer &lexer,
                                               CompileContext &context) {
  Token next;
  TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
  if (tok.type == TokenType::EndOfFile || tok.type == TokenType::Semicolon ||
      tok.type == TokenType::Comma) {
    ReportParserError(context, tok.location, errs::UnexpectedToken{});
    return std::nullopt;
  }
  if (tok.type == TokenType::LBrace) {
    lexer.ConsumeToken(tok);
    Token lbrace = tok;
    TRY_ASSIGN(next, lexer.PeekNextToken());
    if (next.type == TokenType::Dot) {
      TRY_ASSIGN_NEW(result, ParseFieldInitializer(lexer, context));
      TRY_ASSIGN(next, lexer.PeekNextToken());
      Token rbrace = next;
      if (rbrace.type != TokenType::RBrace) {
        ReportParserError(context, rbrace.location,
                          errs::ExpectedClosingBracePhrase{});
        return std::nullopt;
      }
      lexer.ConsumeToken(rbrace);
      result->location = CombineLocation(lbrace.location, rbrace.location);
      return result;
    } else {
      TRY_ASSIGN_NEW(result, ParseArrayInitializer(lexer, context));
      TRY_ASSIGN(next, lexer.PeekNextToken());
      Token rbrace = next;
      if (rbrace.type != TokenType::RBrace) {
        ReportParserError(context, rbrace.location,
                          errs::ExpectedClosingBracePhrase{});
        return std::nullopt;
      }
      lexer.ConsumeToken(rbrace);
      result->location = CombineLocation(lbrace.location, rbrace.location);
      return result;
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

std::optional<TypeRef> ParseTypeName(Lexer &lexer, CompileContext &context) {
  Token next;
  TRY_ASSIGN_NEW(decl_spec, ParseDeclarationSpecifier(lexer, context));
  TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
  if (!IsDeclaratorStart(tok)) {
    TRY_ASSIGN_NEW(base_type, context.GetType(decl_spec.type_spec));
    if (!base_type) {
      context.Diagnostics().Error(
          decl_spec.type_spec.location,
          errs::UnknownTagName{decl_spec.type_spec.type_name});
      return std::nullopt;
    }
    return base_type;
  }

  TRY_ASSIGN_NEW(declarator, ParseDeclarator(lexer, context));
  TRY_ASSIGN_NEW(
      parsed, ParseDeclaratorType(decl_spec.type_spec, *declarator, context));
  TRY_ASSIGN(tok, lexer.PeekNextToken());
  if (tok.type == TokenType::Equal) {
    ReportParserError(context, tok.location, errs::UnexpectedInitializer{});
    return std::nullopt;
  }
  return parsed.type;
}

std::optional<std::unique_ptr<ASTNode>>
ParseDeclaration(Lexer &lexer, CompileContext &context) {
  Token next;
  TRY_ASSIGN_NEW(decl_spec, ParseDeclarationSpecifier(lexer, context));
  DeclStmtBuilder decl_stmt_builder;
  if (decl_spec.decl) {
    decl_stmt_builder.Add(std::move(decl_spec.decl));
  }

  if (decl_spec.type_spec.is_typedef &&
      decl_spec.storage_class != StorageClass::None) {
    context.Diagnostics().Error(decl_spec.type_spec.location,
                                errs::TypedefCannotHaveStorageClass{});
    return std::nullopt;
  }

  TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
  if (tok.type == TokenType::Semicolon || tok.type == TokenType::LBrace) {
    if (!decl_spec.type_spec.is_typedef &&
        decl_spec.storage_class != StorageClass::None) {
      context.Diagnostics().Error(decl_spec.type_spec.location,
                                  errs::StorageClassRequiresDeclarator{});
      return std::nullopt;
    }
    return decl_stmt_builder.Build();
  }

  while (true) {
    TRY_ASSIGN_NEW(declarator, ParseDeclarator(lexer, context));
    TRY_ASSIGN_NEW(parsed_type, ParseDeclaratorType(decl_spec.type_spec,
                                                    *declarator, context));
    if (parsed_type.declarator_name.empty()) {
      context.Diagnostics().Error(declarator->location,
                                  errs::ExpectedDeclaratorIdentifier{});
      return std::nullopt;
    }

    if (decl_spec.type_spec.is_typedef) {
      auto typedef_decl = std::make_unique<TypedefDecl>();
      typedef_decl->location = parsed_type.location;
      typedef_decl->name = parsed_type.declarator_name;
      typedef_decl->type = parsed_type.type;
      context.AddTypedef(typedef_decl.get());
      decl_stmt_builder.Add(std::move(typedef_decl));
    } else if (parsed_type.type->GetType() == TypeBaseType::FunctionType) {
      TRY_ASSIGN_NEW(function_decl,
                     ParseFunctionDecl(lexer, std::move(parsed_type),
                                       decl_spec.storage_class, context));
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

      TRY_ASSIGN(tok, lexer.PeekNextToken());
      if (tok.type == TokenType::Equal) {
        lexer.ConsumeToken(tok);
        TRY_ASSIGN_NEW(initializer, ParseInitializer(lexer, context));
        var_decl_ref->initializer = std::move(initializer);
        var_decl_ref->location = CombineLocation(
            var_decl_ref->location, var_decl_ref->initializer->location);
      }
    }

    TRY_ASSIGN(tok, lexer.PeekNextToken());
    if (tok.type == TokenType::Semicolon || tok.type == TokenType::LBrace ||
        tok.type == TokenType::EndOfFile || tok.type == TokenType::RParen) {
      break;
    }
    if (tok.type != TokenType::Comma) {
      if (IsDeclarationSpecifier(tok, context)) {
        ReportParserError(context, tok.location,
                          errs::ExpectedSemicolonAfterDeclarationPunctuated{});
        return std::nullopt;
      }
      ReportParserError(context, tok.location,
                        errs::ExpectedDeclarationComma{});
      return std::nullopt;
    }
    lexer.ConsumeToken(tok);
  }
  return decl_stmt_builder.Build();
}

static OptUnique<StructDecl>
ParseStructDecl(Lexer &lexer, std::string_view name, CompileContext &context) {
  auto struct_decl = std::make_unique<StructDecl>();
  struct_decl->name = name;
  struct_decl->type =
      context.Types().GetOrCreateStruct(context.CurrentScopeId(), name);
  context.AddStruct(struct_decl.get());
  TRY_ASSIGN_NEW(next, lexer.PeekNextToken());
  if (next.type == TokenType::LBrace) {
    TRY_ASSIGN_NEW(fields_compound, ParseCompoundStmt(lexer, context));
    struct_decl->fields = std::move(fields_compound);
    std::vector<StructField> fields;
    for (const auto &statement : struct_decl->fields->statements) {
      if (statement->GetType() == ASTNodeType::DeclStmt) {
        for (const auto &decl : statement->As<DeclStmt>()->decls) {
          if (decl->GetType() != ASTNodeType::VarDecl) {
            context.Diagnostics().Error(decl->location,
                                        errs::StructFieldsRequireVariables{});
            return std::nullopt;
          }
          auto *field = decl->As<VarDecl>();
          if (field->storage_class != StorageClass::None) {
            context.Diagnostics().Error(
                field->location, errs::StructFieldCannotHaveStorageClass{});
            return std::nullopt;
          }
          fields.push_back(
              {.name = std::string(field->name), .type = field->type});
        }
      } else {
        if (statement->GetType() != ASTNodeType::VarDecl) {
          context.Diagnostics().Error(statement->location,
                                      errs::StructFieldsRequireVariables{});
          return std::nullopt;
        }
        auto *field = statement->As<VarDecl>();
        if (field->storage_class != StorageClass::None) {
          context.Diagnostics().Error(
              field->location, errs::StructFieldCannotHaveStorageClass{});
          return std::nullopt;
        }
        fields.push_back(
            {.name = std::string(field->name), .type = field->type});
      }
    }
    context.Types().CompleteStruct(struct_decl->type, fields);
  } else {
    if (next.type != TokenType::Semicolon) {
      ReportParserError(context, next.location, errs::ExpectedSemicolon{});
      return std::nullopt;
    }
  }
  return struct_decl;
}

static OptUnique<UnionDecl> ParseUnionDecl(Lexer &lexer, std::string_view name,
                                           CompileContext &context) {
  auto union_decl = std::make_unique<UnionDecl>();
  union_decl->name = name;
  union_decl->type =
      context.Types().GetOrCreateUnion(context.CurrentScopeId(), name);
  context.AddUnion(union_decl.get());
  TRY_ASSIGN_NEW(next, lexer.PeekNextToken());
  if (next.type == TokenType::LBrace) {
    TRY_ASSIGN_NEW(fields_compound, ParseCompoundStmt(lexer, context));
    union_decl->fields = std::move(fields_compound);
    std::vector<UnionField> fields;
    auto add_field = [&](const ASTNode *decl) -> bool {
      if (auto field = decl->DynCast<VarDecl>()) {
        if (field->storage_class != StorageClass::None) {
          context.Diagnostics().Error(field->location,
                                      errs::UnionFieldCannotHaveStorageClass{});
          return false;
        }
        fields.push_back({.name = field->name, .type = field->type});
      } else {
        if (decl->GetType() != ASTNodeType::StructDecl &&
            decl->GetType() != ASTNodeType::UnionDecl &&
            decl->GetType() != ASTNodeType::EnumDecl) {
          context.Diagnostics().Error(decl->location,
                                      errs::UnionFieldsRequireVariables{});
          return false;
        }
      }
      return true;
    };
    for (const auto &statement : union_decl->fields->statements) {
      if (statement->GetType() == ASTNodeType::DeclStmt) {
        for (const auto &decl : statement->As<DeclStmt>()->decls) {
          if (!add_field(decl.get())) {
            return std::nullopt;
          }
        }
      } else {
        if (!add_field(statement.get())) {
          return std::nullopt;
        }
      }
    }
    context.Types().CompleteUnion(union_decl->type, fields);
  } else {
    if (next.type != TokenType::Semicolon) {
      ReportParserError(context, next.location, errs::ExpectedSemicolon{});
      return std::nullopt;
    }
  }
  return union_decl;
}

static std::optional<std::monostate>
ParseEnumContent(Lexer &lexer, CompileContext &context, EnumDecl *decl) {
  Token next;
  while (true) {
    TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
    if (tok.type == TokenType::RBrace) {
      lexer.ConsumeToken(tok);
      return std::monostate{};
    }
    if (tok.type == TokenType::EndOfFile) {
      context.Diagnostics().Error(tok.location,
                                  errs::ExpectedClosingBraceAfterEnum{});
      return std::monostate{};
    }

    if (tok.type != TokenType::Identifier) {
      ReportParserError(context, tok.location, errs::ExpectedEnumIdentifier{});
      return std::nullopt;
    }
    std::string_view name = context.Sources().GetText(tok);
    lexer.ConsumeToken(tok);

    auto elem_decl = std::make_unique<EnumElemDecl>();
    Token name_token = tok;
    elem_decl->location = name_token.location;
    elem_decl->name = name;
    TRY_ASSIGN(tok, lexer.PeekNextToken());
    if (tok.type == TokenType::Equal) {
      lexer.ConsumeToken(tok);
      TRY_ASSIGN_NEW(value, ParseAssignmentExpr(lexer, context));
      elem_decl->value = std::move(value);
      elem_decl->location =
          CombineLocation(name_token.location, elem_decl->value->location);
    }
    decl->values.push_back(std::move(elem_decl));
    context.AddEnumerator(decl->values.back().get());
    TRY_ASSIGN(tok, lexer.PeekNextToken());
    if (tok.type == TokenType::Comma) {
      lexer.ConsumeToken(tok);
    }
  }
}

static OptUnique<EnumDecl> ParseEnumDecl(Lexer &lexer, std::string_view name,
                                         CompileContext &context) {
  auto enum_decl = std::make_unique<EnumDecl>();
  enum_decl->name = name;
  enum_decl->type =
      context.Types().GetOrCreateEnum(context.CurrentScopeId(), name);
  context.AddEnum(enum_decl.get());
  TRY_ASSIGN_NEW(next, lexer.PeekNextToken());
  if (next.type == TokenType::Colon) {
    TRY_ASSIGN_NEW(colon, lexer.ConsumeNextToken());
    TRY_ASSIGN_NEW(parsed_type, ParseTypeName(lexer, context));
    if ((parsed_type.get_tag() & TypeQualifierTags) != 0) {
      context.Diagnostics().Error(colon.location,
                                  errs::UnexpectedEnumUnderlyingSpecifier{});
      return std::nullopt;
    }
    if (!IsIntegerType(parsed_type)) {
      context.Diagnostics().Error(colon.location,
                                  errs::ExpectedIntegerEnumUnderlyingType{});
      return std::nullopt;
    }
    enum_decl->underlying_type = parsed_type;
  } else {
    enum_decl->underlying_type = context.Types().getInt();
  }
  TRY_ASSIGN(next, lexer.PeekNextToken());
  if (next.type == TokenType::LBrace) {
    enum_decl->is_definition = true;
    TRY(lexer.ConsumeNextToken());
    TRY(ParseEnumContent(lexer, context, enum_decl.get()));
  } else {
    if (next.type != TokenType::Semicolon) {
      ReportParserError(context, next.location, errs::ExpectedSemicolon{});
      return std::nullopt;
    }
  }
  return enum_decl;
}

static OptUnique<FunctionDecl> ParseFunctionDecl(Lexer &lexer, ParsedType type,
                                                 StorageClass storage_class,
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
  TRY_ASSIGN_NEW(next, lexer.PeekNextToken());
  if (next.type == TokenType::LBrace) {
    TRY_ASSIGN_NEW(body, ParseCompoundStmt(lexer, context));
    function_decl->body = std::move(body);
    function_decl->location =
        CombineLocation(function_decl->location, function_decl->body->location);
  } else {
    if (next.type != TokenType::Semicolon && next.type != TokenType::Comma &&
        next.type != TokenType::EndOfFile && next.type != TokenType::RParen) {
      ReportParserError(context, next.location,
                        errs::ExpectedFunctionBodyOrSemicolon{});
      return std::nullopt;
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
