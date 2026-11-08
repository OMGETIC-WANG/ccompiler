#include "Expr.h"
#include "../Util/StringUtil.h"
#include "AST.h"
#include "Declaration.h"
#include "Intrinsic.h"
#include "Operations.h"
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
  case TokenType::AddEqual:
    return BinaryOpType::AddAssign;
  case TokenType::SubEqual:
    return BinaryOpType::SubAssign;
  case TokenType::MulEqual:
    return BinaryOpType::MulAssign;
  case TokenType::DivEqual:
    return BinaryOpType::DivAssign;
  case TokenType::ModEqual:
    return BinaryOpType::ModAssign;
  case TokenType::BitAndEqual:
    return BinaryOpType::BitAndAssign;
  case TokenType::BitOrEqual:
    return BinaryOpType::BitOrAssign;
  case TokenType::BitXorEqual:
    return BinaryOpType::BitXorAssign;
  case TokenType::LShiftEqual:
    return BinaryOpType::LShiftAssign;
  case TokenType::RShiftEqual:
    return BinaryOpType::RShiftAssign;
  case TokenType::Equal:
    return BinaryOpType::Assignment;
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

std::vector<std::unique_ptr<ASTNode>> ParseQuotedExpr(FilteredLexer &lexer,
                                                      CompileContext &context) {
  std::vector<std::unique_ptr<ASTNode>> results;
  while (true) {
    Token tok = lexer.PeekNextToken();
    if (tok.type == TokenType::RParen || tok.type == TokenType::EndOfFile ||
        tok.type == TokenType::Semicolon) {
      break;
    }

    results.push_back(ParseExpr(lexer, context));

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

static std::unique_ptr<LiteralNum> ParseLiteralNum(std::string_view s,
                                                   CompileContext &context) {
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
      ASSERT(Contains(num, 'p') || Contains(num, 'P'),
             "Hex literal must have 'p' or 'P'");
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

  ASSERT(!is_float || base != 8, "oct float literal not support");

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
  ASSERT(ec == std::errc{}, "Error occured when parsing num \'", s, "\'");
  ASSERT(ptr == num.data() + num.size(),
         "Not all characters were consumed when parsing num \'", s, "\'");

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

static char CastSimpleEscape(char escape) {
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
    RAISE_ERROR("Unknown escape sequence: \\", escape);
  }
}

static int HexToInt(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  } else if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  } else if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  } else {
    RAISE_ERROR("Invalid hex digit: ", c);
  }
}

static CowString ParseLiteralStringValue(std::string_view s_) {
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
        res.push_back(CastSimpleEscape(s[i + 1]));
        i += 2;
      } else if (s[i + 1] == 'x') {
        int h1 = HexToInt(s[i + 2]);
        int h0 = HexToInt(s[i + 3]);
        ASSERT(h1 >= 0 && h1 <= 15 && h0 >= 0 && h0 <= 15,
               "Invalid hex escape sequence");
        res.push_back(static_cast<char>(h1 * 16 + h0));
        i += 4;
      } else {
        int o2 = s[i + 1] - '0';
        int o1 = s[i + 2] - '0';
        int o0 = s[i + 3] - '0';
        ASSERT(o2 >= 0 && o2 <= 7 && o1 >= 0 && o1 <= 7 && o0 >= 0 && o0 <= 7,
               "Invalid octal escape sequence");
        res.push_back(static_cast<char>(o2 * 64 + o1 * 8 + o0));
        i += 4;
      }
    }
  }
  return res;
}

std::unique_ptr<ASTNode> ParsePrimary(FilteredLexer &lexer,
                                      CompileContext &context) {
  Token tok = lexer.PeekNextToken();
  switch (tok.type) {
  case TokenType::LiteralNum: {
    lexer.ConsumeToken(tok);
    return ParseLiteralNum(tok.value, context);
  }
  case TokenType::LiteralString: {
    lexer.ConsumeToken(tok);
    auto literal_string = std::make_unique<LiteralString>();
    CowString s = ParseLiteralStringValue(tok.value);
    while (lexer.PeekNextToken().type == TokenType::LiteralString) {
      s.string() +=
          ParseLiteralStringValue(lexer.ConsumeNextToken().value).view();
    }
    literal_string->value = std::move(s);
    return literal_string;
  }
  case TokenType::Identifier: {
    lexer.ConsumeToken(tok);
    if (const IntrinsicDesc *desc =
            IntrinsicManager::GetIntrinsicDesc(tok.value)) {
      return ParseIntrinsicCall(desc, lexer, context);
    } else {
      auto var_ref = std::make_unique<VarRef>();
      var_ref->name = tok.value;
      return var_ref;
    }
  }
  case TokenType::LParen: {
    lexer.ConsumeToken(tok);
    auto expr = ParseQuotedExpr(lexer, context);
    ASSERT(lexer.ConsumeNextToken().type == TokenType::RParen,
           "Expected closing paren");
    if (expr.size() == 1) {
      return std::move(expr[0]);
    } else {
      auto seq_eval = std::make_unique<SequenceEval>();
      seq_eval->expressions = std::move(expr);
      return seq_eval;
    }
  }
  default:
    RAISE_ERROR("Unexpected token");
  }
}

std::unique_ptr<ASTNode> ParsePostfix(FilteredLexer &lexer,
                                      CompileContext &context,
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
      func_invoke->arguments = ParseQuotedExpr(lexer, context);
      ASSERT(lexer.ConsumeNextToken().type == TokenType::RParen,
             "Expected closing paren");
      primary = std::move(func_invoke);
      break;
    }
    case TokenType::LBracket: {
      lexer.ConsumeToken(tok);
      auto subscript_op = std::make_unique<BinaryOp>(BinaryOpType::Subscript);
      subscript_op->left = std::move(primary);
      subscript_op->right = ParseExpr(lexer, context);
      ASSERT(lexer.ConsumeNextToken().type == TokenType::RBracket,
             "Expected closing bracket");
      primary = std::move(subscript_op);
      break;
    }
    case TokenType::RightArrow: {
      lexer.ConsumeToken(tok);
      auto get_field_op = std::make_unique<PtrGetField>();
      get_field_op->operand = std::move(primary);
      Token field_tok = lexer.ConsumeNextToken();
      ASSERT(field_tok.type == TokenType::Identifier, "Expected field name");
      get_field_op->field_name = field_tok.value;
      primary = std::move(get_field_op);
      break;
    }
    case TokenType::Dot: {
      lexer.ConsumeToken(tok);
      auto get_field_op = std::make_unique<GetField>();
      get_field_op->operand = std::move(primary);
      Token field_tok = lexer.ConsumeNextToken();
      ASSERT(field_tok.type == TokenType::Identifier, "Expected field name");
      get_field_op->field_name = field_tok.value;
      primary = std::move(get_field_op);
      break;
    }
    case TokenType::AddAdd: {
      lexer.ConsumeToken(tok);
      auto inc_after_use_op =
          std::make_unique<UnaryOp>(UnaryOpType::IncAfterUse);
      inc_after_use_op->operand = std::move(primary);
      primary = std::move(inc_after_use_op);
      break;
    }
    case TokenType::SubSub: {
      lexer.ConsumeToken(tok);
      auto dec_after_use_op =
          std::make_unique<UnaryOp>(UnaryOpType::DecAfterUse);
      dec_after_use_op->operand = std::move(primary);
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

std::unique_ptr<ASTNode> ParsePrefixPostfixExpr(FilteredLexer &lexer,
                                                CompileContext &context) {
  std::unique_ptr<UnaryOp> prefix;
  UnaryOp *last_prefix = nullptr;
  auto add_prefix = [&prefix,
                     &last_prefix](std::unique_ptr<UnaryOp> new_prefix) {
    if (prefix) {
      last_prefix->operand = std::move(new_prefix);
      last_prefix = (UnaryOp *)last_prefix->operand.get();
    } else {
      prefix = std::move(new_prefix);
      last_prefix = (UnaryOp *)prefix.get();
    }
  };
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
    case TokenType::Et:
    case TokenType::BitNot:
    case TokenType::Not:
      lexer.ConsumeToken(tok);
      add_prefix(std::make_unique<UnaryOp>(tok_to_unary_op_type[tok.type]));
      break;
    case TokenType::LParen: {
      Token type_start = lexer.PeekNextToken(1);
      if (IsDeclarationSpecifier(type_start, context)) {
        lexer.ConsumeToken(tok);

        auto declaration = ParseDeclaration(lexer, context);
        auto parsed_type = ParseType(declaration.get(), context);
        ASSERT(parsed_type.size() == 1, "Expected a single type in cast");

        auto type_cast = std::make_unique<TypeCast>();
        type_cast->target_type = parsed_type[0].type;
        add_prefix(std::move(type_cast));
        ASSERT(lexer.ConsumeNextToken().type == TokenType::RParen,
               "Expected closing paren");
        break;
      }
    }
    default:
      auto primary = ParsePrimary(lexer, context);
      auto postfixed = ParsePostfix(lexer, context, std::move(primary));
      if (last_prefix) {
        last_prefix->operand = std::move(postfixed);
        return std::move(prefix);
      } else {
        return postfixed;
      }
    }
  }
}

#define MAKE_BINARY_OP_CASE(op) case TokenType::op:

static bool IsBinaryOp(TokenType type) {
  switch (type) {
    BINARY_OP_TOKEN_VALUES(MAKE_BINARY_OP_CASE)
    return true;
  case TokenType::Et:
    return true;
  default:
    return false;
  }
}

static bool IsRightCombineOp(TokenType op) {
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

std::unique_ptr<ASTNode> ParseSingleExpr(FilteredLexer &lexer,
                                         CompileContext &context,
                                         int min_prec = 0) {
  auto lhs = ParsePrefixPostfixExpr(lexer, context);
  while (true) {
    Token tok = lexer.PeekNextToken();
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
      int op_prec = GetPrecedence(tok.type);
      if (!IsBinaryOp(tok.type) || op_prec < min_prec) {
        return lhs;
      }

      lexer.ConsumeToken(tok);

      int next_min_prec = IsRightCombineOp(tok.type) ? op_prec : op_prec + 1;

      auto rhs = ParseSingleExpr(lexer, context, next_min_prec);

      auto bin_op = std::make_unique<BinaryOp>(GetBinaryOpType(tok.type));
      bin_op->left = std::move(lhs);
      bin_op->right = std::move(rhs);
      lhs = std::move(bin_op);
    }
  }

  return lhs;
}

std::unique_ptr<ASTNode> ParseExpr(FilteredLexer &lexer,
                                   CompileContext &context) {
  auto expr = ParseSingleExpr(lexer, context);
  if (lexer.PeekNextToken().type == TokenType::Question) {
    lexer.ConsumeNextToken();
    auto cond_expr = std::make_unique<ConditionalExpr>();
    cond_expr->condition = std::move(expr);
    cond_expr->true_expr = ParseExpr(lexer, context);
    ASSERT_EQ(lexer.PeekNextToken().type, TokenType::Colon,
              "Expected ':' in conditional expression");
    lexer.ConsumeNextToken();
    cond_expr->false_expr = ParseExpr(lexer, context);
    return cond_expr;
  }
  return expr;
}
