#include "Expr.h"
#include "../Util/StringUtil.h"
#include "../Util/Util.h"
#include "AST.h"
#include "Declaration.h"
#include "Diagnostics/Errors.generated.h"
#include "Intrinsic.h"
#include "Operations.h"
#include "ParserError.h"
#include <charconv>

enum class PrecLevel {
  Primary,
  Assign,
  LogicOr,
  LogicAnd,
  BitOr,
  BitXor,
  BitAnd,
  LogicEqNe,
  LogicLessGreater,
  Shift,
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
  case TokenType::LShift:
  case TokenType::RShift:
    return (int)PrecLevel::Shift;
  case TokenType::Equal:
  case TokenType::AddEqual:
  case TokenType::SubEqual:
  case TokenType::MulEqual:
  case TokenType::DivEqual:
  case TokenType::ModEqual:
  case TokenType::BitAndEqual:
  case TokenType::BitOrEqual:
  case TokenType::BitXorEqual:
  case TokenType::LShiftEqual:
  case TokenType::RShiftEqual:
    return (int)PrecLevel::Assign;
  case TokenType::Eq:
  case TokenType::Ne:
    return (int)PrecLevel::LogicEqNe;
  case TokenType::And:
    return (int)PrecLevel::LogicAnd;
  case TokenType::Or:
    return (int)PrecLevel::LogicOr;
  case TokenType::BitOr:
    return (int)PrecLevel::BitOr;
  case TokenType::BitXor:
    return (int)PrecLevel::BitXor;
  case TokenType::Et:
    return (int)PrecLevel::BitAnd;
  case TokenType::Lt:
  case TokenType::Gt:
  case TokenType::Le:
  case TokenType::Ge:
    return (int)PrecLevel::LogicLessGreater;

  default:
    RAISE_ERROR("Operator not supported yet");
  }
}

static BinaryOpType GetBinaryOpType(TokenType type) {
  switch (type) {
  case TokenType::Add:
    return BinaryOpType::Add;
  case TokenType::Sub:
    return BinaryOpType::Sub;
  case TokenType::Star:
    return BinaryOpType::Mul;
  case TokenType::Div:
    return BinaryOpType::Div;
  case TokenType::Mod:
    return BinaryOpType::Mod;
  case TokenType::Et:
    return BinaryOpType::BitAnd;
  case TokenType::BitOr:
    return BinaryOpType::BitOr;
  case TokenType::BitXor:
    return BinaryOpType::BitXor;
  case TokenType::LShift:
    return BinaryOpType::LShift;
  case TokenType::RShift:
    return BinaryOpType::RShift;
  case TokenType::Eq:
    return BinaryOpType::Eq;
  case TokenType::Ne:
    return BinaryOpType::Ne;
  case TokenType::And:
    return BinaryOpType::And;
  case TokenType::Or:
    return BinaryOpType::Or;
  case TokenType::Lt:
    return BinaryOpType::Lt;
  case TokenType::Gt:
    return BinaryOpType::Gt;
  case TokenType::Le:
    return BinaryOpType::Le;
  case TokenType::Ge:
    return BinaryOpType::Ge;
  default:
    RAISE_ERROR("Operator not supported yet");
  }
}

static std::optional<BinaryOpType> GetAssignmentOperation(TokenType type) {
  switch (type) {
  case TokenType::Equal:
    return std::nullopt;
  case TokenType::AddEqual:
    return BinaryOpType::Add;
  case TokenType::SubEqual:
    return BinaryOpType::Sub;
  case TokenType::MulEqual:
    return BinaryOpType::Mul;
  case TokenType::DivEqual:
    return BinaryOpType::Div;
  case TokenType::ModEqual:
    return BinaryOpType::Mod;
  case TokenType::BitAndEqual:
    return BinaryOpType::BitAnd;
  case TokenType::BitOrEqual:
    return BinaryOpType::BitOr;
  case TokenType::BitXorEqual:
    return BinaryOpType::BitXor;
  case TokenType::LShiftEqual:
    return BinaryOpType::LShift;
  case TokenType::RShiftEqual:
    return BinaryOpType::RShift;
  default:
    RAISE_ERROR("Operator is not an assignment");
  }
}

static std::optional<std::vector<std::unique_ptr<ASTNode>>>
ParseQuotedExpr(Lexer &lexer, CompileContext &context) {
  std::vector<std::unique_ptr<ASTNode>> results;
  Token next;
  while (true) {
    TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
    if (tok.type == TokenType::RParen || tok.type == TokenType::EndOfFile ||
        tok.type == TokenType::Semicolon) {
      break;
    }

    TRY_ASSIGN_NEW(expression, ParseAssignmentExpr(lexer, context));
    results.push_back(std::move(expression));

    TRY_ASSIGN(tok, lexer.PeekNextToken());
    if (tok.type == TokenType::RParen || tok.type == TokenType::EndOfFile ||
        tok.type == TokenType::Semicolon) {
      break;
    } else {
      if (tok.type != TokenType::Comma) {
        ReportParserError(context, tok.location, errs::ExpectedComma{});
        return std::nullopt;
      }
      lexer.ConsumeToken(tok);
    }
  }
  return results;
}

static bool IsOct(std::string_view s) {
  if (s[0] != '0' || s.size() == 1) {
    return false;
  }
  for (size_t i = 1; i < s.size(); i++) {
    if (s[i] < '0' || s[i] > '7') {
      return false;
    }
  }
  return true;
}

static OptUnique<LiteralNum> ParseLiteralNum(Token tok,
                                             CompileContext &context) {
  std::string_view s = context.Sources().GetText(tok);
  ASSERT(!s.empty(), "Unexpected empty literal num");

  CowString num_s(s);
  if (s.find('\'') != std::string_view::npos) {
    std::string cleaned;
    cleaned.reserve(s.size());
    for (char c : s) {
      if (c != '\'') {
        cleaned.push_back(c);
      }
    }
    num_s = cleaned;
  }

  std::string_view num = num_s.view();

  bool kind_declared = true;
  BuiltinKind kind = BuiltinKind::Int;
  bool is_unsigned = false;

  int base = 10;
  if (StartsWithInsensitive(s, "0x")) {
    base = 16;
    num = num.substr(2);
  } else if (StartsWithInsensitive(s, "0b")) {
    base = 2;
    num = num.substr(2);
  }

  bool is_float_num;
  if (base == 16) {
    if (Contains(num, '.')) {
      is_float_num = true;
      if (!Contains(num, 'p') && !Contains(num, 'P')) {
        ReportParserError(context, tok.location,
                          errs::HexFloatRequiresExponentMarker{});
        return std::nullopt;
      }
    } else if (Contains(num, 'p') || Contains(num, 'P')) {
      is_float_num = true;
    } else {
      is_float_num = false;
    }
  } else {
    is_float_num =
        Contains(num, '.') || Contains(num, 'e') || Contains(num, 'E');
  }

  if (EndsWithInsensitive(num, "ull")) {
    kind = BuiltinKind::LongLong;
    is_unsigned = true;
    num = num.substr(0, num.size() - 3);
  } else if (EndsWithInsensitive(num, "ll")) {
    kind = BuiltinKind::LongLong;
    is_unsigned = false;
    num = num.substr(0, num.size() - 2);
  } else if (EndsWithInsensitive(num, "ul")) {
    kind = BuiltinKind::Long;
    is_unsigned = true;
    num = num.substr(0, num.size() - 2);
  } else if (EndsWithInsensitive(num, "f")) {
    if (base == 16 && !is_float_num) {
      kind = BuiltinKind::Int;
      is_unsigned = false;
    } else {
      kind = BuiltinKind::Float;
      num = num.substr(0, num.size() - 1);
    }
  } else if (EndsWithInsensitive(num, "l")) {
    kind = BuiltinKind::Long;
    is_unsigned = false;
    num = num.substr(0, num.size() - 1);
    if (is_float_num) {
      kind = BuiltinKind::LongDouble;
    }
  } else if (EndsWithInsensitive(num, "u")) {
    kind = BuiltinKind::Int;
    is_unsigned = true;
    num = num.substr(0, num.size() - 1);
  } else {
    kind_declared = false;
  }

  if (IsOct(num)) {
    base = 8;
    num = num.substr(1);
  }

  if (!kind_declared && is_float_num) {
    kind = BuiltinKind::Double;
  }

  bool is_float = kind_declared ? (kind == BuiltinKind::Float ||
                                   kind == BuiltinKind::Double ||
                                   kind == BuiltinKind::LongDouble)
                                : is_float_num;

  if (is_float && base == 8) {
    context.Diagnostics().Error(tok.location, errs::OctalFloatUnsupported{});
    return std::nullopt;
  }

  auto literal_num = std::make_unique<LiteralNum>();
  literal_num->type = TypeRef(
      context.Types().Builtin(kind),
      (TypeRef::TagType)(is_unsigned ? TypeTag::Unsigned : TypeTag::None));

  const char *ptr;
  std::errc ec;
  if (is_float) {
    std::chars_format fmt =
        base == 16 ? std::chars_format::hex : std::chars_format::general;
    long double val;
    auto res = std::from_chars(num.data(), num.data() + num.size(), val, fmt);
    literal_num->value = val;
    ptr = res.ptr;
    ec = res.ec;
  } else if (is_unsigned) {
    uint64_t val;
    auto res = std::from_chars(num.data(), num.data() + num.size(), val, base);
    literal_num->value = val;
    ptr = res.ptr;
    ec = res.ec;
  } else {
    int64_t val;
    auto res = std::from_chars(num.data(), num.data() + num.size(), val, base);
    literal_num->value = val;
    ptr = res.ptr;
    ec = res.ec;
  }
  if (ec != std::errc{} || ptr != num.data() + num.size()) {
    ReportParserError(context, tok.location,
                      errs::InvalidNumericLiteral{std::string(s)});
    return std::nullopt;
  }

  literal_num->location = tok.location;
  return literal_num;
}

static bool IsSimpleEscape(char c) {
  switch (c) {
  case '0':
  case '\'':
  case '\"':
  case '?':
  case '\\':
  case 'a':
  case 'b':
  case 'f':
  case 'n':
  case 'r':
  case 't':
  case 'v':
    return true;
  default:
    return false;
  }
}

static std::optional<char> CastSimpleEscape(char escape,
                                            SourceLocation location,
                                            DiagnosticContext &diagnostics) {
  switch (escape) {
  case '0':
    return '\0';
  case '\'':
    return '\'';
  case '\"':
    return '\"';
  case '?':
    return '\?';
  case '\\':
    return '\\';
  case 'a':
    return '\a';
  case 'b':
    return '\b';
  case 'f':
    return '\f';
  case 'n':
    return '\n';
  case 'r':
    return '\r';
  case 't':
    return '\t';
  case 'v':
    return '\v';
  default:
    diagnostics.Error(location, errs::UnknownEscapeSequence{escape});
    return std::nullopt;
  }
}

static std::optional<int> HexToInt(char c, SourceLocation location,
                                   DiagnosticContext &diagnostics) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  } else if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  } else if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  } else {
    diagnostics.Error(location, errs::InvalidHexDigit{c});
    return std::nullopt;
  }
}

static bool IsOctalDigit(char c) { return c >= '0' && c <= '7'; }

static bool IsHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

static unsigned HexDigitValue(char c) {
  if (c >= '0' && c <= '9') {
    return static_cast<unsigned>(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<unsigned>(c - 'a' + 10);
  }
  return static_cast<unsigned>(c - 'A' + 10);
}

static std::optional<uint64_t>
ParseCharacterLiteralValue(std::string_view spelling, SourceLocation location,
                           CompileContext &context) {
  ASSERT(spelling.size() >= 2 && spelling.front() == '\'' &&
             spelling.back() == '\'',
         "Invalid character literal token");

  std::string_view content = spelling.substr(1, spelling.size() - 2);
  if (content.empty()) {
    ReportParserError(context, location, errs::EmptyCharacterLiteral{});
    return std::nullopt;
  }

  for (char c : content) {
    if (static_cast<unsigned char>(c) >= 0x80) {
      ReportParserError(context, location, errs::NonAsciiCharacterLiteral{});
      return std::nullopt;
    }
  }

  if (content.front() != '\\') {
    if (content.size() != 1) {
      ReportParserError(context, location,
                        errs::CharacterLiteralRequiresSingleCharacter{});
      return std::nullopt;
    }
    return static_cast<unsigned char>(content.front());
  }

  size_t pos = 1;
  ASSERT(pos < content.size(), "Character literal escape is incomplete");
  char escape = content[pos++];

  if (escape == 'x') {
    const size_t digits_begin = pos;
    uint64_t value = 0;
    bool out_of_range = false;
    while (pos < content.size() && IsHexDigit(content[pos])) {
      unsigned digit = HexDigitValue(content[pos++]);
      if (!out_of_range) {
        if (value > (0xffu - digit) / 16u) {
          out_of_range = true;
        } else {
          value = value * 16 + digit;
        }
      }
    }
    if (digits_begin == pos) {
      ReportParserError(context, location,
                        errs::CharacterLiteralHexEscapeRequiresDigit{});
      return std::nullopt;
    }
    if (out_of_range) {
      ReportParserError(context, location,
                        errs::CharacterLiteralEscapeOutOfRange{});
      return std::nullopt;
    }
    if (pos != content.size()) {
      ReportParserError(context, location,
                        errs::CharacterLiteralRequiresSingleCharacter{});
      return std::nullopt;
    }
    return value;
  }

  if (IsOctalDigit(escape)) {
    uint64_t value = static_cast<unsigned>(escape - '0');
    size_t digits = 1;
    while (digits < 3 && pos < content.size() && IsOctalDigit(content[pos])) {
      value = value * 8 + static_cast<unsigned>(content[pos++] - '0');
      ++digits;
    }
    if (value > 0xff) {
      ReportParserError(context, location,
                        errs::CharacterLiteralEscapeOutOfRange{});
      return std::nullopt;
    }
    if (pos != content.size()) {
      ReportParserError(context, location,
                        errs::CharacterLiteralRequiresSingleCharacter{});
      return std::nullopt;
    }
    return value;
  }

  TRY_ASSIGN_NEW(value,
                 CastSimpleEscape(escape, location, context.Diagnostics()));
  if (pos != content.size()) {
    ReportParserError(context, location,
                      errs::CharacterLiteralRequiresSingleCharacter{});
    return std::nullopt;
  }
  return static_cast<unsigned char>(value);
}

static OptUnique<LiteralNum> ParseLiteralChar(Token tok,
                                              CompileContext &context) {
  TRY_ASSIGN_NEW(value,
                 ParseCharacterLiteralValue(context.Sources().GetText(tok),
                                            tok.location, context));

  auto literal_num = std::make_unique<LiteralNum>();
  literal_num->type = context.Types().getInt();
  literal_num->value = static_cast<int64_t>(value);
  literal_num->location = tok.location;
  return literal_num;
}

static std::optional<CowString>
ParseLiteralStringValue(std::string_view s_, SourceLocation location,
                        DiagnosticContext &diagnostics) {
  std::string_view s = s_.substr(1, s_.size() - 2); // remove quote
  for (auto c : s) {
    if (c == '\\') {
      goto has_escape;
    }
  }
  return s;

has_escape:;
  std::string res;
  res.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    if (s[i] != '\\') {
      res.push_back(s[i]);
      i++;
    } else {
      if (IsSimpleEscape(s[i + 1])) {
        TRY_ASSIGN_NEW(escaped,
                       CastSimpleEscape(s[i + 1], location, diagnostics));
        res.push_back(escaped);
        i += 2;
      } else if (s[i + 1] == 'x') {
        TRY_ASSIGN_NEW(h1, HexToInt(s[i + 2], location, diagnostics));
        TRY_ASSIGN_NEW(h0, HexToInt(s[i + 3], location, diagnostics));
        res.push_back(static_cast<char>(h1 * 16 + h0));
        i += 4;
      } else {
        int o2 = s[i + 1] - '0';
        int o1 = s[i + 2] - '0';
        int o0 = s[i + 3] - '0';
        if (o2 < 0 || o2 > 7 || o1 < 0 || o1 > 7 || o0 < 0 || o0 > 7) {
          diagnostics.Error(location, errs::InvalidOctalEscape{});
          return std::nullopt;
        }
        res.push_back(static_cast<char>(o2 * 64 + o1 * 8 + o0));
        i += 4;
      }
    }
  }
  return res;
}

OptUnique<ASTNode> ParsePrimary(Lexer &lexer, CompileContext &context) {
  Token next;
  TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
  switch (tok.type) {
  case TokenType::LiteralNum: {
    lexer.ConsumeToken(tok);
    return ParseLiteralNum(tok, context);
  }
  case TokenType::LiteralChar: {
    lexer.ConsumeToken(tok);
    return ParseLiteralChar(tok, context);
  }
  case TokenType::LiteralString: {
    lexer.ConsumeToken(tok);
    auto literal_string = std::make_unique<LiteralString>();
    literal_string->location = tok.location;
    TRY_ASSIGN_NEW(
        string, ParseLiteralStringValue(context.Sources().GetText(tok),
                                        tok.location, context.Diagnostics()));
    while (true) {
      TRY_ASSIGN(next, lexer.PeekNextToken());
      if (next.type != TokenType::LiteralString) {
        break;
      }
      TRY_ASSIGN_NEW(next_literal, lexer.ConsumeNextToken());
      TRY_ASSIGN_NEW(next_value,
                     ParseLiteralStringValue(
                         context.Sources().GetText(next_literal),
                         next_literal.location, context.Diagnostics()));
      string.string() += next_value.view();
      literal_string->location =
          CombineLocation(literal_string->location, next_literal.location);
    }
    literal_string->value = std::move(string);
    return literal_string;
  }
  case TokenType::Identifier: {
    lexer.ConsumeToken(tok);
    std::string_view name = context.Sources().GetText(tok);
    if (const IntrinsicDesc *desc = IntrinsicManager::GetIntrinsicDesc(name)) {
      return ParseIntrinsicCall(desc, tok, lexer, context);
    } else {
      ValueDecl *decl = context.FindValue(name);
      if (!decl) {
        ReportParserError(context, tok.location, errs::UnknownVar{name});
        return std::nullopt;
      }
      auto var_ref = std::make_unique<VarRef>(decl);
      var_ref->location = tok.location;
      return var_ref;
    }
  }
  case TokenType::LParen: {
    lexer.ConsumeToken(tok);
    TRY_ASSIGN_NEW(expressions, ParseQuotedExpr(lexer, context));
    TRY_ASSIGN(next, lexer.PeekNextToken());
    Token rparen = next;
    if (rparen.type != TokenType::RParen) {
      ReportParserError(context, rparen.location, errs::ExpectedClosingParen{});
      return std::nullopt;
    }
    lexer.ConsumeToken(rparen);
    if (expressions.empty()) {
      ReportParserError(context, rparen.location, errs::UnexpectedToken{});
      return std::nullopt;
    }
    if (expressions.size() == 1) {
      expressions[0]->location = CombineLocation(tok.location, rparen.location);
      return std::move(expressions[0]);
    } else {
      auto seq_eval = std::make_unique<SequenceEval>();
      seq_eval->location = CombineLocation(tok.location, rparen.location);
      seq_eval->expressions = std::move(expressions);
      return seq_eval;
    }
  }
  default:
    ReportParserError(context, tok.location, errs::UnexpectedToken{});
    return std::nullopt;
  }
}

static OptUnique<ASTNode> ParsePostfix(Lexer &lexer, CompileContext &context,
                                       SourceLocation primary_start,
                                       std::unique_ptr<ASTNode> primary) {
  Token next;
  while (true) {
    TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
    switch (tok.type) {
    case TokenType::EndOfFile:
    case TokenType::Semicolon:
    case TokenType::RParen:
      return primary;
    case TokenType::LParen: {
      lexer.ConsumeToken(tok);
      auto func_invoke = std::make_unique<FunctionInvocation>();
      func_invoke->function = std::move(primary);
      TRY_ASSIGN_NEW(arguments, ParseQuotedExpr(lexer, context));
      func_invoke->arguments = std::move(arguments);
      TRY_ASSIGN(next, lexer.PeekNextToken());
      Token rparen = next;
      if (rparen.type != TokenType::RParen) {
        ReportParserError(context, rparen.location,
                          errs::ExpectedClosingParen{});
        return std::nullopt;
      }
      lexer.ConsumeToken(rparen);
      func_invoke->location = CombineLocation(primary_start, rparen.location);
      primary = std::move(func_invoke);
      break;
    }
    case TokenType::LBracket: {
      lexer.ConsumeToken(tok);
      auto subscript_op = std::make_unique<BinaryOp>(BinaryOpType::Subscript);
      subscript_op->left = std::move(primary);
      TRY_ASSIGN_NEW(index, ParseExpr(lexer, context));
      subscript_op->right = std::move(index);
      TRY_ASSIGN(next, lexer.PeekNextToken());
      Token rbracket = next;
      if (rbracket.type != TokenType::RBracket) {
        ReportParserError(context, rbracket.location,
                          errs::ExpectedClosingBracket{});
        return std::nullopt;
      }
      lexer.ConsumeToken(rbracket);
      subscript_op->location =
          CombineLocation(primary_start, rbracket.location);
      primary = std::move(subscript_op);
      break;
    }
    case TokenType::RightArrow: {
      lexer.ConsumeToken(tok);
      auto get_field_op = std::make_unique<PtrGetField>();
      get_field_op->operand = std::move(primary);
      TRY_ASSIGN(next, lexer.PeekNextToken());
      Token field_tok = next;
      if (field_tok.type != TokenType::Identifier) {
        ReportParserError(context, field_tok.location,
                          errs::ExpectedFieldName{});
        return std::nullopt;
      }
      lexer.ConsumeToken(field_tok);
      get_field_op->field_name = context.Sources().GetText(field_tok);
      get_field_op->location =
          CombineLocation(primary_start, field_tok.location);
      primary = std::move(get_field_op);
      break;
    }
    case TokenType::Dot: {
      lexer.ConsumeToken(tok);
      auto get_field_op = std::make_unique<GetField>();
      get_field_op->operand = std::move(primary);
      TRY_ASSIGN(next, lexer.PeekNextToken());
      Token field_tok = next;
      if (field_tok.type != TokenType::Identifier) {
        ReportParserError(context, field_tok.location,
                          errs::ExpectedFieldName{});
        return std::nullopt;
      }
      lexer.ConsumeToken(field_tok);
      get_field_op->field_name = context.Sources().GetText(field_tok);
      get_field_op->location =
          CombineLocation(primary_start, field_tok.location);
      primary = std::move(get_field_op);
      break;
    }
    case TokenType::AddAdd: {
      lexer.ConsumeToken(tok);
      auto inc_after_use_op =
          std::make_unique<UnaryOp>(UnaryOpType::IncAfterUse);
      inc_after_use_op->operand = std::move(primary);
      inc_after_use_op->location = CombineLocation(primary_start, tok.location);
      primary = std::move(inc_after_use_op);
      break;
    }
    case TokenType::SubSub: {
      lexer.ConsumeToken(tok);
      auto dec_after_use_op =
          std::make_unique<UnaryOp>(UnaryOpType::DecAfterUse);
      dec_after_use_op->operand = std::move(primary);
      dec_after_use_op->location = CombineLocation(primary_start, tok.location);
      primary = std::move(dec_after_use_op);
      break;
    }
    default:
      return primary;
    }
  }
}

std::unique_ptr<ASTNode>
AddPrefixes(std::unique_ptr<ASTNode> node,
            std::vector<std::unique_ptr<UnaryOp>> &prefixes) {
  for (int i = prefixes.size() - 1; i >= 0; i--) {
    prefixes[i]->operand = std::move(node);
    node = std::move(prefixes[i]);
  }
  return node;
}

static std::unordered_map<TokenType, UnaryOpType> tok_to_unary_op_type = {
    {TokenType::Add, UnaryOpType::UnaryPlus},
    {TokenType::Sub, UnaryOpType::UnaryMinus},
    {TokenType::AddAdd, UnaryOpType::IncBeforeUse},
    {TokenType::SubSub, UnaryOpType::DecBeforeUse},
    {TokenType::Star, UnaryOpType::Deref},
    {TokenType::Et, UnaryOpType::GetAddress},
    {TokenType::BitNot, UnaryOpType::BitNot},
    {TokenType::Not, UnaryOpType::Not}};

static std::optional<std::unique_ptr<ASTNode>>
ParsePrefixPostfixExpr(Lexer &lexer, CompileContext &context) {
  std::unique_ptr<UnaryOp> prefix;
  UnaryOp *last_prefix = nullptr;
  std::vector<UnaryOp *> prefix_nodes;
  Token next;
  auto add_prefix = [&prefix, &last_prefix,
                     &prefix_nodes](std::unique_ptr<UnaryOp> new_prefix) {
    if (prefix) {
      last_prefix->operand = std::move(new_prefix);
      last_prefix = (UnaryOp *)last_prefix->operand.get();
    } else {
      prefix = std::move(new_prefix);
      last_prefix = (UnaryOp *)prefix.get();
    }
    prefix_nodes.push_back(last_prefix);
  };
  while (true) {
    TRY_ASSIGN_NEW(tok, lexer.PeekNextToken());
    switch (tok.type) {
    case TokenType::EndOfFile:
    case TokenType::Semicolon:
      return std::unique_ptr<ASTNode>{};
    case TokenType::AddAdd:
    case TokenType::SubSub:
    case TokenType::Add:
    case TokenType::Sub:
    case TokenType::Star:
    case TokenType::Et:
    case TokenType::BitNot:
    case TokenType::Not:
      lexer.ConsumeToken(tok);
      {
        auto unary_op =
            std::make_unique<UnaryOp>(tok_to_unary_op_type[tok.type]);
        unary_op->location = tok.location;
        add_prefix(std::move(unary_op));
      }
      break;
    case TokenType::LParen: {
      TRY_ASSIGN_NEW(type_start, lexer.PeekNextToken(1));
      if (IsDeclarationSpecifier(type_start, context)) {
        lexer.ConsumeToken(tok);

        TRY_ASSIGN_NEW(target_type, ParseTypeName(lexer, context));
        TRY_ASSIGN(next, lexer.PeekNextToken());
        if (next.type == TokenType::Comma) {
          ReportParserError(context, tok.location,
                            errs::ExpectedSingleCastType{});
          return std::nullopt;
        }

        auto type_cast = std::make_unique<TypeCast>();
        type_cast->location = tok.location;
        type_cast->target_type = target_type;
        add_prefix(std::move(type_cast));
        TRY_ASSIGN(next, lexer.PeekNextToken());
        Token rparen = next;
        if (rparen.type != TokenType::RParen) {
          ReportParserError(context, rparen.location,
                            errs::ExpectedClosingParen{});
          return std::nullopt;
        }
        lexer.ConsumeToken(rparen);
        break;
      }
    }
    default:
      Token primary_start = tok;
      TRY_ASSIGN_NEW(primary, ParsePrimary(lexer, context));
      TRY_ASSIGN_NEW(postfixed,
                     ParsePostfix(lexer, context, primary_start.location,
                                  std::move(primary)));
      if (last_prefix) {
        last_prefix->operand = std::move(postfixed);
        for (auto it = prefix_nodes.rbegin(); it != prefix_nodes.rend(); ++it) {
          UnaryOp *unary_op = *it;
          unary_op->location =
              CombineLocation(unary_op->location,
                              unary_op->operand ? unary_op->operand->location
                                                : unary_op->location);
        }
        return std::move(prefix);
      } else {
        return postfixed;
      }
    }
  }
}

static bool IsBinaryOp(TokenType type) {
  switch (type) {
  case TokenType::Add:
  case TokenType::Sub:
  case TokenType::Star:
  case TokenType::Div:
  case TokenType::Mod:
  case TokenType::LShift:
  case TokenType::RShift:
  case TokenType::BitAnd:
  case TokenType::BitOr:
  case TokenType::BitXor:
  case TokenType::Et:
  case TokenType::And:
  case TokenType::Or:
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

static bool IsAssignmentOp(TokenType op) {
  switch (op) {
  case TokenType::Equal:
  case TokenType::AddEqual:
  case TokenType::SubEqual:
  case TokenType::MulEqual:
  case TokenType::DivEqual:
  case TokenType::ModEqual:
  case TokenType::BitAndEqual:
  case TokenType::BitOrEqual:
  case TokenType::BitXorEqual:
  case TokenType::LShiftEqual:
  case TokenType::RShiftEqual:
    return true;
  default:
    return false;
  }
}

static std::optional<std::unique_ptr<ASTNode>>
ParseBinaryExpr(Lexer &lexer, CompileContext &context, int min_prec = 0) {
  Token next;
  TRY_ASSIGN_NEW(start_token, lexer.PeekNextToken());
  TRY_ASSIGN_NEW(lhs, ParsePrefixPostfixExpr(lexer, context));
  while (true) {
    TRY_ASSIGN(next, lexer.PeekNextToken());
    Token tok = next;
    switch (tok.type) {
    case TokenType::EndOfFile:
    case TokenType::Semicolon:
    case TokenType::RParen:
    case TokenType::RBracket:
    case TokenType::Comma:
    case TokenType::RBrace:
    case TokenType::Question:
    case TokenType::Colon:
      return lhs;
    default:
      if (!IsBinaryOp(tok.type)) {
        return lhs;
      }
      int op_prec = GetPrecedence(tok.type);
      if (op_prec < min_prec) {
        return lhs;
      }

      lexer.ConsumeToken(tok);

      TRY_ASSIGN_NEW(rhs, ParseBinaryExpr(lexer, context, op_prec + 1));

      auto bin_op = std::make_unique<BinaryOp>(GetBinaryOpType(tok.type));
      bin_op->left = std::move(lhs);
      bin_op->right = std::move(rhs);
      bin_op->location = CombineLocation(start_token.location,
                                         bin_op->right ? bin_op->right->location
                                                       : tok.location);
      lhs = std::move(bin_op);
    }
  }

  return lhs;
}

static std::optional<std::unique_ptr<ASTNode>>
ParseConditionalExpr(Lexer &lexer, CompileContext &context) {
  Token next;
  TRY_ASSIGN_NEW(start_token, lexer.PeekNextToken());
  TRY_ASSIGN_NEW(expr, ParseBinaryExpr(lexer, context));
  TRY_ASSIGN(next, lexer.PeekNextToken());
  if (next.type == TokenType::Question) {
    TRY(lexer.ConsumeNextToken());
    auto cond_expr = std::make_unique<ConditionalExpr>();
    cond_expr->condition = std::move(expr);
    TRY_ASSIGN_NEW(true_expr, ParseExpr(lexer, context));
    cond_expr->true_expr = std::move(true_expr);
    TRY_ASSIGN(next, lexer.PeekNextToken());
    Token colon = next;
    if (colon.type != TokenType::Colon) {
      ReportParserError(context, colon.location,
                        errs::ExpectedConditionalColon{});
      return std::nullopt;
    }
    lexer.ConsumeToken(colon);
    TRY_ASSIGN_NEW(false_expr, ParseConditionalExpr(lexer, context));
    cond_expr->false_expr = std::move(false_expr);
    cond_expr->location =
        CombineLocation(start_token.location,
                        cond_expr->false_expr ? cond_expr->false_expr->location
                                              : start_token.location);
    return cond_expr;
  }
  return expr;
}

std::optional<std::unique_ptr<ASTNode>>
ParseAssignmentExpr(Lexer &lexer, CompileContext &context) {
  TRY_ASSIGN_NEW(lhs, ParseConditionalExpr(lexer, context));

  Token next;
  TRY_ASSIGN(next, lexer.PeekNextToken());
  if (!IsAssignmentOp(next.type)) {
    return lhs;
  }

  lexer.ConsumeToken(next);
  TRY_ASSIGN_NEW(rhs, ParseAssignmentExpr(lexer, context));

  auto bin_op = std::make_unique<BinaryOp>(BinaryOpType::Assignment);
  bin_op->compound_op = GetAssignmentOperation(next.type);
  bin_op->left = std::move(lhs);
  bin_op->right = std::move(rhs);
  bin_op->location =
      CombineLocation(bin_op->left->location, bin_op->right->location);
  return bin_op;
}

std::optional<std::unique_ptr<ASTNode>> ParseExpr(Lexer &lexer,
                                                  CompileContext &context) {
  Token start_token;
  TRY_ASSIGN(start_token, lexer.PeekNextToken());

  std::vector<std::unique_ptr<ASTNode>> expressions;
  while (true) {
    std::optional<std::unique_ptr<ASTNode>> expression =
        ParseAssignmentExpr(lexer, context);
    if (!expression || !*expression) {
      return std::nullopt;
    }
    expressions.push_back(std::move(*expression));

    Token next;
    TRY_ASSIGN(next, lexer.PeekNextToken());
    if (next.type != TokenType::Comma) {
      break;
    }
    lexer.ConsumeToken(next);
  }

  if (expressions.size() == 1) {
    return std::move(expressions.front());
  }

  auto seq_eval = std::make_unique<SequenceEval>();
  seq_eval->location =
      CombineLocation(start_token.location, expressions.back()->location);
  seq_eval->expressions = std::move(expressions);
  return seq_eval;
}
