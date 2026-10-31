#include "Expr.h"
#include "AST.h"
#include "Declaration.h"
#include "Operations.h"

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

static std::variant<double, uint64_t, int64_t>
ParseLiteralNumValue(std::string_view s) {
  ASSERT(!s.empty(), "Unexpected empty literal num");

  bool is_float = false;
  if (s.back() == 'f' || s.back() == 'F') {
    is_float = true;
    s.remove_suffix(1);
    ASSERT(!s.empty(), "Unexpected empty float literal");
  } else if (s.find_first_of(".eE") != std::string_view::npos) {
    is_float = true;
  }

  if (is_float) {
    double val = 0.0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
    ASSERT(ec == std::errc{} && ptr == s.data() + s.size(),
           "Failed to parse float literal");
    return val;
  }

  uint64_t u_val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), u_val);
  ASSERT(ec == std::errc{} && ptr == s.data() + s.size(),
         "Failed to parse integer literal");

  if (u_val <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return static_cast<int64_t>(u_val);
  }
  return u_val;
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
    auto literal_num = std::make_unique<LiteralNum>();
    literal_num->value = ParseLiteralNumValue(tok.value);
    return literal_num;
  }
  case TokenType::LiteralString: {
    lexer.ConsumeToken(tok);
    auto literal_string = std::make_unique<LiteralString>();
    literal_string->value = ParseLiteralStringValue(tok.value);
    return literal_string;
  }
  case TokenType::Identifier: {
    lexer.ConsumeToken(tok);
    if (tok.value == "__builtin_va_start") {
      ASSERT_EQ(lexer.PeekNextToken().type, TokenType::LParen,
                "Expected '(' after __builtin_va_start");
      lexer.ConsumeNextToken();

      auto va_start = std::make_unique<VaStart>();
      va_start->va_list = ParseExpr(lexer, context);

      ASSERT_EQ(lexer.PeekNextToken().type, TokenType::Comma,
                "Expected ',' after va_list in __builtin_va_start");
      lexer.ConsumeNextToken();

      va_start->last_named_arg = ParseExpr(lexer, context);

      ASSERT_EQ(lexer.PeekNextToken().type, TokenType::RParen,
                "Expected ')' after __builtin_va_start");
      lexer.ConsumeNextToken();
      return va_start;
    } else if (tok.value == "__builtin_va_arg") {
      ASSERT_EQ(lexer.PeekNextToken().type, TokenType::LParen,
                "Expected '(' after __builtin_va_arg");
      lexer.ConsumeNextToken();

      auto va_arg = std::make_unique<VaArg>();
      va_arg->va_list = ParseExpr(lexer, context);

      ASSERT_EQ(lexer.PeekNextToken().type, TokenType::Comma,
                "Expected ',' after va_list in __builtin_va_arg");
      lexer.ConsumeNextToken();

      auto declaration = ParseDeclaration(lexer, context);
      auto parsed_type = ParseType(declaration.get(), context, false);
      ASSERT(parsed_type.size() == 1, "Expected a single type in cast");

      va_arg->type = parsed_type[0].type;

      ASSERT_EQ(lexer.PeekNextToken().type, TokenType::RParen,
                "Expected ')' after __builtin_va_arg");
      lexer.ConsumeNextToken();
      return va_arg;
    } else if (tok.value == "__builtin_va_end") {
      ASSERT_EQ(lexer.PeekNextToken().type, TokenType::LParen,
                "Expected '(' after __builtin_va_end");
      lexer.ConsumeNextToken();

      auto va_end = std::make_unique<VaEnd>();
      va_end->va_list = ParseExpr(lexer, context);

      ASSERT_EQ(lexer.PeekNextToken().type, TokenType::RParen,
                "Expected ')' after __builtin_va_end");
      lexer.ConsumeNextToken();
      return va_end;
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
        auto parsed_type = ParseType(declaration.get(), context, false);
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

std::unique_ptr<ASTNode> ParseExpr(FilteredLexer &lexer,
                                   CompileContext &context, int min_prec) {
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
      return lhs;
    default:
      int op_prec = GetPrecedence(tok.type);
      if (!IsBinaryOp(tok.type) || op_prec < min_prec) {
        return lhs;
      }

      lexer.ConsumeToken(tok);

      int next_min_prec = IsRightCombineOp(tok.type) ? op_prec : op_prec + 1;

      auto rhs = ParseExpr(lexer, context, next_min_prec);

      auto bin_op = std::make_unique<BinaryOp>(GetBinaryOpType(tok.type));
      bin_op->left = std::move(lhs);
      bin_op->right = std::move(rhs);
      lhs = std::move(bin_op);
    }
  }

  return lhs;
}
