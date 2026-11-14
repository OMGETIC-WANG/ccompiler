#pragma once

#include "../Util/EnumRefl.h"
#include "SourceLocation.h"

#include <algorithm>
#include <cstdint>

// clang-format off
#define KEYWORD_TOKEN_VALUES(x)\
  x(KwIf)\
  x(KwElse)\
  x(KwWhile)\
  x(KwDo)\
  x(KwFor)\
  x(KwReturn)\
  x(KwContinue)\
  x(KwVoid)\
  x(KwStatic)\
  x(KwConst)\
  x(KwStruct)\
  x(KwUnion)\
  x(KwTypedef)\
  x(KwEnum)\
  x(KwSwitch)\
  x(KwCase)\
  x(KwBreak)\
  x(KwDefault)\
  x(KwGoto)\
  x(KwInt)\
  x(KwChar)\
  x(KwShort)\
  x(KwFloat)\
  x(KwDouble)\
  x(KwSigned)\
  x(KwUnsigned)\
  x(KwLong)\
  x(KwVolatile)\
  x(KwExtern)\
  x(KwRegister)

#define TOKEN_TYPE_VALUES(x)\
  x(Unknown)\
  x(EndOfFile)\
  x(Identifier)\
  x(Sharp)\
  x(SharpSharp)\
  x(NextLine)\
  KEYWORD_TOKEN_VALUES(x)\
  x(LiteralNum)\
  x(LiteralString)\
  x(LiteralChar)\
  x(AddAdd)\
  x(SubSub)\
  x(Star)\
  x(Dot)\
  x(Et)\
  x(BitNot)\
  x(Not)\
  x(Comma)\
  x(Semicolon)\
  x(Colon)\
  x(LParen)\
  x(RParen)\
  x(LBracket)\
  x(RBracket)\
  x(LBrace)\
  x(RBrace)\
  x(DotDotDot)\
  x(RightArrow)\
  x(Question)\
  x(Equal)\
  x(Add)\
  x(Sub)\
  x(Div)\
  x(Mod)\
  x(LShift)\
  x(RShift)\
  x(BitAnd)\
  x(BitOr)\
  x(BitXor)\
  x(AddEqual)\
  x(SubEqual)\
  x(MulEqual)\
  x(DivEqual)\
  x(ModEqual)\
  x(LShiftEqual)\
  x(RShiftEqual)\
  x(BitAndEqual)\
  x(BitOrEqual)\
  x(BitXorEqual)\
  x(And)\
  x(Or)\
  x(Eq)\
  x(Ne)\
  x(Lt)\
  x(Gt)\
  x(Le)\
  x(Ge)
// clang-format on

MAKE_ENUM(TokenType, TOKEN_TYPE_VALUES)

REGISTER_ENUM(TokenType, TOKEN_TYPE_VALUES)

inline bool IsTypeKeyword(TokenType type) {
  switch (type) {
  case TokenType::KwVoid:
  case TokenType::KwInt:
  case TokenType::KwChar:
  case TokenType::KwShort:
  case TokenType::KwFloat:
  case TokenType::KwDouble:
  case TokenType::KwLong:
    return true;
  default:
    return false;
  }
}

inline bool IsStorageSpecifierKeyword(TokenType type) {
  switch (type) {
  case TokenType::KwStatic:
  case TokenType::KwExtern:
  case TokenType::KwRegister:
    return true;
  default:
    return false;
  }
}

inline bool IsTypeQualifierKeyword(TokenType type) {
  return type == TokenType::KwConst || type == TokenType::KwVolatile;
}

inline bool IsDeclarationSpecifierKeyword(TokenType type) {
  return IsTypeKeyword(type) || IsStorageSpecifierKeyword(type) ||
         IsTypeQualifierKeyword(type) || type == TokenType::KwSigned ||
         type == TokenType::KwUnsigned || type == TokenType::KwStruct ||
         type == TokenType::KwUnion || type == TokenType::KwEnum ||
         type == TokenType::KwTypedef;
}

#define MAKE_KEYWORD_CASE(kw) case TokenType::kw:
inline bool IsKeyword(TokenType type) {
  switch (type) {
    KEYWORD_TOKEN_VALUES(MAKE_KEYWORD_CASE)
    return true;
  default:
    return false;
  }
}
#undef MAKE_KEYWORD_CASE

struct Token {
  Token(TokenType type_ = TokenType::Unknown, SourceLocation location_ = {},
        bool has_leading_space_ = false)
      : location(location_), type(type_),
        has_leading_space(has_leading_space_) {}

  SourceLocation location;
  TokenType type;
  bool has_leading_space;

  bool operator==(const Token &) const = default;
};
