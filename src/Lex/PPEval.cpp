#include "PPEval.h"

#include "../AST/AST.h"
#include "../AST/Operations.h"
#include "Diagnostics/Errors.generated.h"

#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

using MacroValue = std::variant<uint64_t, int64_t>;

#define MAKE_TRUE_CASE_BIN(op)                                                 \
  case BinaryOpType::op:                                                       \
    return true;

static bool IsEvalable(BinaryOpType op_type) {
  switch (op_type) {
    BINARY_NUMERIC_ARITHMETIC_OP_VALUES(MAKE_TRUE_CASE_BIN)
    BINARY_BITWISE_ARITHMETIC_OP_VALUES(MAKE_TRUE_CASE_BIN)
  case BinaryOpType::And:
  case BinaryOpType::Or:
    return true;
  default:
    return false;
  }
}

static bool IsBitwiseOp(BinaryOpType op_type) {
  switch (op_type) {
    BINARY_BITWISE_ARITHMETIC_OP_VALUES(MAKE_TRUE_CASE_BIN)
  default:
    return false;
  }
}

#define MAKE_TRUE_CASE_UNARY(op)                                               \
  case UnaryOpType::op:                                                        \
    return true;

static bool IsEvalable(UnaryOpType op_type) {
  switch (op_type) {
    UNARY_ARITHMETIC_OP_VALUES(MAKE_TRUE_CASE_UNARY)
  default:
    return false;
  }
}

#undef MAKE_TRUE_CASE_BIN
#undef MAKE_TRUE_CASE_UNARY

template <typename To> MacroValue CastTo(MacroValue val) {
  return std::visit([](auto v) -> MacroValue { return static_cast<To>(v); },
                    val);
}

static std::pair<MacroValue, MacroValue> PromoteVal(MacroValue left,
                                                    MacroValue right) {
  if (std::holds_alternative<uint64_t>(left) ||
      std::holds_alternative<uint64_t>(right)) {
    return {CastTo<uint64_t>(left), CastTo<uint64_t>(right)};
  }
  return {left, right};
}

static bool IsTrue(MacroValue val) {
  return std::visit([](auto v) { return v != 0; }, val);
}

template <typename T, typename Op>
static MacroValue CalcAs(MacroValue left, MacroValue right) {
  if constexpr (std::is_invocable_v<Op, T, T>) {
    auto res = Op{}(std::get<T>(left), std::get<T>(right));
    if constexpr (std::is_same_v<decltype(res), bool>) {
      return static_cast<int64_t>(res);
    } else {
      return res;
    }
  } else {
    return static_cast<uint64_t>(0); // left and right types are checked in Calc
  }
}

template <typename Op>
static MacroValue Calc(MacroValue left, MacroValue right) {
  ASSERT(left.index() == right.index(), "Expect promoted left and right");
  if (std::holds_alternative<uint64_t>(left)) {
    return CalcAs<uint64_t, Op>(left, right);
  } else {
    return CalcAs<int64_t, Op>(left, right);
  }
}

struct shift_left {
  template <typename T> auto operator()(T l, T r) -> decltype(l << r) {
    return l << r;
  }
};

struct shift_right {
  template <typename T> auto operator()(T l, T r) -> decltype(l >> r) {
    return l >> r;
  }
};

static MacroValue EvalNode(ASTNode *node, DiagnosticContext &diagnostics);

static MacroValue EvalLiteral(LiteralNum *literal,
                              DiagnosticContext &diagnostics) {
  if (auto *value = std::get_if<uint64_t>(&literal->value)) {
    return *value;
  }
  if (auto *value = std::get_if<int64_t>(&literal->value)) {
    return *value;
  }
  diagnostics.Fail(literal->location, errs::FloatInPreprocessorCondition{});
}

static MacroValue EvalBinary(BinaryOp *bin_expr,
                             DiagnosticContext &diagnostics) {
  if (!IsEvalable(bin_expr->op_type)) {
    diagnostics.Fail(
        bin_expr->location,
        errs::CannotEvaluateBinaryInPreprocessor{bin_expr->op_type});
  }

  MacroValue left = EvalNode(bin_expr->left.get(), diagnostics);
  if (bin_expr->op_type == BinaryOpType::And) {
    if (!IsTrue(left)) {
      return static_cast<int64_t>(0);
    }
    return static_cast<int64_t>(
        IsTrue(EvalNode(bin_expr->right.get(), diagnostics)));
  }
  if (bin_expr->op_type == BinaryOpType::Or) {
    if (IsTrue(left)) {
      return static_cast<int64_t>(1);
    }
    return static_cast<int64_t>(
        IsTrue(EvalNode(bin_expr->right.get(), diagnostics)));
  }

  MacroValue right = EvalNode(bin_expr->right.get(), diagnostics);
  if (IsBitwiseOp(bin_expr->op_type)) {
    ASSERT(std::holds_alternative<uint64_t>(left) ||
               std::holds_alternative<int64_t>(left),
           "Bitwise operations require integer operands");
  }
  std::tie(left, right) = PromoteVal(left, right);
  switch (bin_expr->op_type) {
  case BinaryOpType::Add:
    return Calc<std::plus<>>(left, right);
  case BinaryOpType::Sub:
    return Calc<std::minus<>>(left, right);
  case BinaryOpType::Mul:
    return Calc<std::multiplies<>>(left, right);
  case BinaryOpType::Div:
    return Calc<std::divides<>>(left, right);
  case BinaryOpType::Mod:
    return Calc<std::modulus<>>(left, right);
  case BinaryOpType::Eq:
    return Calc<std::equal_to<>>(left, right);
  case BinaryOpType::Ne:
    return Calc<std::not_equal_to<>>(left, right);
  case BinaryOpType::Lt:
    return Calc<std::less<>>(left, right);
  case BinaryOpType::Gt:
    return Calc<std::greater<>>(left, right);
  case BinaryOpType::Le:
    return Calc<std::less_equal<>>(left, right);
  case BinaryOpType::Ge:
    return Calc<std::greater_equal<>>(left, right);
  case BinaryOpType::BitAnd:
    return Calc<std::bit_and<>>(left, right);
  case BinaryOpType::BitOr:
    return Calc<std::bit_or<>>(left, right);
  case BinaryOpType::BitXor:
    return Calc<std::bit_xor<>>(left, right);
  case BinaryOpType::LShift:
    return Calc<shift_left>(left, right);
  case BinaryOpType::RShift:
    return Calc<shift_right>(left, right);
  default:
    diagnostics.Fail(bin_expr->location,
                     errs::UnsupportedBinaryPreprocessorOperation{});
  }
}

static MacroValue EvalUnary(UnaryOp *unary_expr,
                            DiagnosticContext &diagnostics) {
  if (!IsEvalable(unary_expr->op_type)) {
    diagnostics.Fail(
        unary_expr->location,
        errs::CannotEvaluateUnaryInPreprocessor{unary_expr->op_type});
  }
  MacroValue operand = EvalNode(unary_expr->operand.get(), diagnostics);
  switch (unary_expr->op_type) {
  case UnaryOpType::UnaryPlus:
    return operand;
  case UnaryOpType::UnaryMinus:
    return std::visit([](auto v) -> MacroValue { return -v; }, operand);
  case UnaryOpType::Not:
    return static_cast<int64_t>(!IsTrue(operand));
  case UnaryOpType::BitNot:
    return std::visit(
        [](auto v) -> MacroValue {
          if constexpr (std::is_invocable_v<std::bit_not<>, decltype(v)>) {
            return std::bit_not<>{}(v);
          } else {
            return static_cast<uint64_t>(0);
          }
        },
        operand);
  default:
    diagnostics.Fail(unary_expr->location,
                     errs::UnsupportedUnaryPreprocessorOperation{});
  }
}

static MacroValue EvalConditional(ConditionalExpr *cond_expr,
                                  DiagnosticContext &diagnostics) {
  if (IsTrue(EvalNode(cond_expr->condition.get(), diagnostics))) {
    return EvalNode(cond_expr->true_expr.get(), diagnostics);
  }
  return EvalNode(cond_expr->false_expr.get(), diagnostics);
}

static MacroValue EvalNode(ASTNode *node, DiagnosticContext &diagnostics) {
  ASSERT(node != nullptr, "Unexpected null AST node in preprocessor condition");
  if (auto *literal = node->DynCast<LiteralNum>()) {
    return EvalLiteral(literal, diagnostics);
  }
  if (auto *binary = node->DynCast<BinaryOp>()) {
    return EvalBinary(binary, diagnostics);
  }
  if (auto *unary = node->DynCast<UnaryOp>()) {
    return EvalUnary(unary, diagnostics);
  }
  if (auto *cond_expr = node->DynCast<ConditionalExpr>()) {
    return EvalConditional(cond_expr, diagnostics);
  }
  diagnostics.Fail(node->location,
                   errs::UnexpectedASTNodeInPreprocessor{node->GetType()});
}

uint64_t EvalMacroCond(ASTNode *node, DiagnosticContext &diagnostics) {
  MacroValue value = EvalNode(node, diagnostics);
  return std::visit([](auto val) -> uint64_t { return val; }, value);
}
