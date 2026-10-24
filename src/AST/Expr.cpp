#include "Expr.h"
#include "../Util/Keyword.h"
#include "AST.h"
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
  case TokenType::Equal:
    return BinaryOpType::Assignment;
  case TokenType::Eq:
    return BinaryOpType::Eq;
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
    auto expr = ParseQuotedExpr(lexer, context);
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
    {TokenType::AddAdd, UnaryOpType::IncBeforeUse},
    {TokenType::SubSub, UnaryOpType::DecBeforeUse},
    {TokenType::Star, UnaryOpType::Deref},
    {TokenType::Et, UnaryOpType::GetAddress}};

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
      lexer.ConsumeToken(tok);
      add_prefix(std::make_unique<UnaryOp>(tok_to_unary_op_type[tok.type]));
      break;
    case TokenType::LParen: {
      Token tok1 = lexer.PeekNextToken(1);
      if (tok1.type == TokenType::Keyword) {
        if (IsTypeKeyword(tok1.value)) {
          Token tok2 = lexer.PeekNextToken(2);
          if (tok2.type == TokenType::RParen) {
            lexer.ConsumeTokens(3); // (, type-keyword, )
            auto type_cast = std::make_unique<TypeCast>();
            type_cast->target_type = context.Types().Builtin(tok1.value);
            add_prefix(std::move(type_cast));
            break;
          }
        } else if (tok1.value == "struct") {
          Token tok2 = lexer.PeekNextToken(2);
          Token tok3 = lexer.PeekNextToken(3);
          if (tok2.type == TokenType::Identifier &&
              tok3.type == TokenType::RParen) {
            lexer.ConsumeTokens(4); // (, struct, identifier, )
            auto type_cast = std::make_unique<TypeCast>();
            type_cast->target_type = context.FindStruct(tok2.value);
            add_prefix(std::move(type_cast));
            break;
          }
        }
      } else if (tok1.type == TokenType::Identifier) {
        ContextItemType tok1_type = context.GetTypeOf(tok1.value);
        if (tok1_type == ContextItemType::Typedef ||
            tok1_type == ContextItemType::Enum) {
          lexer.ConsumeTokens(2); // (, identifier, )
          auto type_cast = std::make_unique<TypeCast>();
          if (tok1_type == ContextItemType::Typedef) {
            type_cast->target_type = context.FindTypedef(tok1.value);
          } else {
            type_cast->target_type = context.FindEnum(tok1.value);
          }
          add_prefix(std::move(type_cast));
          ASSERT(lexer.ConsumeNextToken().type == TokenType::RParen,
                 "Expected closing paren");
          break;
        }
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
