#include "Expr.h"
#include "Operations.h"

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

std::unique_ptr<ASTNode> ParsePrimary(FilteredLexer &lexer) {
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
    literal_string->value =
        tok.value.substr(1, tok.value.size() - 2); // remove quotes
    return literal_string;
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
      auto subscript_op = std::make_unique<BinaryOp>(OperationType::Subscript);
      subscript_op->left = std::move(primary);
      subscript_op->right = ParseExpr(lexer);
      ASSERT(lexer.ConsumeNextToken().type == TokenType::RBracket,
             "Expected closing bracket");
      primary = std::move(subscript_op);
      break;
    }
    case TokenType::Dot: {
      lexer.ConsumeToken(tok);
      auto get_field_op = std::make_unique<BinaryOp>(OperationType::GetField);
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
      auto inc_after_use_op =
          std::make_unique<UnaryOp>(OperationType::IncAfterUse);
      inc_after_use_op->operand = std::move(primary);
      primary = std::move(inc_after_use_op);
      break;
    }
    case TokenType::SubSub: {
      lexer.ConsumeToken(tok);
      auto dec_after_use_op =
          std::make_unique<UnaryOp>(OperationType::DecAfterUse);
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
      prefix = std::make_unique<UnaryOp>(OperationType::IncBeforeUse);
      break;
    case TokenType::SubSub:
      prefix = std::make_unique<UnaryOp>(OperationType::DecBeforeUse);
      break;
    case TokenType::Star:
      prefix = std::make_unique<UnaryOp>(OperationType::Deref);
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

      auto bin_op = std::make_unique<BinaryOp>(GetOperationType(tok.type));
      bin_op->left = std::move(lhs);
      bin_op->right = std::move(rhs);
      lhs = std::move(bin_op);
    }
  }

  return lhs;
}
