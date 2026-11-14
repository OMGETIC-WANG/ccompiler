#include "ConstantEval.h"
#include "../../AST/AST.h"
#include "../../AST/Intrinsic.h"
#include "../../AST/Operations.h"
#include "../../Util/Diagnostic.h"

#define MAKE_TRUE_CASE_BIN(op)                                                 \
  case BinaryOpType::op:                                                       \
    return true;

static bool IsEvalable(BinaryOpType op_type) {
  switch (op_type) {
    BINARY_NUMERIC_ARITHMETIC_OP_VALUES(MAKE_TRUE_CASE_BIN)
    BINARY_BITWISE_ARITHMETIC_OP_VALUES(MAKE_TRUE_CASE_BIN)
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

template <typename To> NumericValue CastTo(NumericValue val) {
  return std::visit([](auto v) -> NumericValue { return static_cast<To>(v); },
                    val);
}

static std::pair<NumericValue, NumericValue> PromoteVal(NumericValue left,
                                                        NumericValue right) {
  if (std::holds_alternative<long double>(left) ||
      std::holds_alternative<long double>(right)) {
    return {CastTo<long double>(left), CastTo<long double>(right)};
  }
  if (std::holds_alternative<uint64_t>(left) ||
      std::holds_alternative<uint64_t>(right)) {
    return {CastTo<uint64_t>(left), CastTo<uint64_t>(right)};
  }
  return {left, right};
}

static bool HoldsInteger(NumericValue val) {
  return std::holds_alternative<uint64_t>(val) ||
         std::holds_alternative<int64_t>(val);
}

template <typename T, typename Op>
static NumericValue CalcAs(NumericValue left, NumericValue right) {
  if constexpr (std::is_invocable_v<Op, T, T>) {
    auto res = Op{}(std::get<T>(left), std::get<T>(right));
    if constexpr (std::is_same_v<decltype(res), bool>) {
      return (int64_t)res;
    } else {
      return res;
    }
  } else {
    return (uint64_t)0; // left and right types are checked in Calc
  }
}

template <typename Op>
static NumericValue Calc(NumericValue left, NumericValue right) {
  ASSERT(left.index() == right.index(), "Expect promoted left and right");
  if (std::holds_alternative<long double>(left)) {
    return CalcAs<long double, Op>(left, right);
  } else if (std::holds_alternative<uint64_t>(left)) {
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
  template <typename T> auto operator()(T l, T r) -> decltype(l << r) {
    return l >> r;
  }
};

static NumericValue EvalNode(ASTNode *node, GenContext &context);

static NumericValue EvalLiteral(LiteralNum *literal) { return literal->value; }

static NumericValue EvalBinary(BinaryOp *bin_expr, GenContext &context) {
  if (!IsEvalable(bin_expr->op_type)) {
    context.Diagnostics().Fail(bin_expr->location,
                               "Cannot evaluate non-numeric binary operation");
  }
  NumericValue left = EvalNode(bin_expr->left.get(), context);
  NumericValue right = EvalNode(bin_expr->right.get(), context);
  if (IsBitwiseOp(bin_expr->op_type)) {
    if (!HoldsInteger(left) || !HoldsInteger(right)) {
      context.Diagnostics().Fail(bin_expr->location,
                                 "Bitwise operations require integer operands");
    }
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
    context.Diagnostics().Fail(
        bin_expr->location,
        "Unsupported binary operation for constant evaluation");
  }
}

static NumericValue EvalUnary(UnaryOp *unary_expr, GenContext &context) {
  if (!IsEvalable(unary_expr->op_type)) {
    context.Diagnostics().Fail(unary_expr->location,
                               "Cannot evaluate non-numeric unary operation");
  }
  NumericValue operand = EvalNode(unary_expr->operand.get(), context);
  switch (unary_expr->op_type) {
  case UnaryOpType::UnaryPlus:
    return operand;
  case UnaryOpType::UnaryMinus:
    return std::visit([](auto v) -> NumericValue { return -v; }, operand);
  case UnaryOpType::Not:
    return std::visit([](auto v) -> NumericValue { return (int64_t)(!v); },
                      operand);
  case UnaryOpType::BitNot:
    if (!HoldsInteger(operand)) {
      context.Diagnostics().Fail(unary_expr->location,
                                 "Bitwise NOT requires an integer operand");
    }
    return std::visit(
        [](auto v) -> NumericValue {
          if constexpr (std::is_invocable_v<std::bit_not<>, decltype(v)>) {
            return std::bit_not<>{}(v);
          } else {
            return 0ull; // operand types has been checked.
          }
        },
        operand);
  default:
    context.Diagnostics().Fail(
        unary_expr->location,
        "Unsupported unary operation for constant evaluation");
  }
}

static NumericValue EvalConditional(ConditionalExpr *cond_expr,
                                    GenContext &context) {
  NumericValue condition = EvalNode(cond_expr->condition.get(), context);
  bool bool_cond = std::visit([](auto v) -> bool { return !!v; }, condition);
  if (bool_cond) {
    return EvalNode(cond_expr->true_expr.get(), context);
  } else {
    return EvalNode(cond_expr->false_expr.get(), context);
  }
}

static NumericValue EvalIntrinsic(IntrinsicCall *intrinsic_call,
                                  GenContext &context) {
  if (!intrinsic_call->desc->is_constant_evalable) {
    context.Diagnostics().Fail(
        intrinsic_call->location,
        "Intrinsic is not constant-evaluable: ", intrinsic_call->desc->name);
  }
  RValue res = intrinsic_call->Eval(&context);
  if (!IsIntegerType(res.c_type) && !IsFloatingType(res.c_type)) {
    context.Diagnostics().Fail(
        intrinsic_call->location,
        "Constant-evaluable intrinsic must produce a numeric result");
  }
  if (auto *integer = llvm::dyn_cast<llvm::ConstantInt>(res.ir_value)) {
    if (HasTypeTag(res.c_type, TypeTag::Unsigned)) {
      return integer->getZExtValue();
    } else {
      return integer->getSExtValue();
    }
  } else if (auto *floating = llvm::dyn_cast<llvm::ConstantFP>(res.ir_value)) {
    return floating->getValueAPF().convertToDouble();
  } else {
    context.Diagnostics().Fail(
        intrinsic_call->location,
        "Unexpected non-numeric result from constant-evaluable intrinsic");
  }
}

static NumericValue EvalNode(ASTNode *node, GenContext &context) {
  if (auto *literal = node->DynCast<LiteralNum>()) {
    return EvalLiteral(literal);
  }
  if (auto *binary = node->DynCast<BinaryOp>()) {
    return EvalBinary(binary, context);
  }
  if (auto *unary = node->DynCast<UnaryOp>()) {
    return EvalUnary(unary, context);
  }
  if (auto *cond_expr = node->DynCast<ConditionalExpr>()) {
    return EvalConditional(cond_expr, context);
  }
  if (auto *intrinsic_call = node->DynCast<IntrinsicCall>()) {
    return EvalIntrinsic(intrinsic_call, context);
  }
  context.Diagnostics().Fail(
      node->location,
      "Unexpected AST node type for constant evaluation: ", node->GetType());
}

NumericValue EvalNumeric(ASTNode *node, GenContext &context) {
  return EvalNode(node, context);
}

llvm::Constant *EvalNumericConstant(TypeRef type, ASTNode *node,
                                    GenContext &context) {
  NumericValue val = EvalNode(node, context);

  llvm::Type *llvm_type = context.LLVMType(type);
  if (std::holds_alternative<int64_t>(val)) {
    if (!llvm_type->isIntegerTy()) {
      context.Diagnostics().Fail(node->location,
                                 "Integer initializer type mismatch");
    }
    return llvm::ConstantInt::get(llvm_type, std::get<int64_t>(val), true);
  }
  if (std::holds_alternative<uint64_t>(val)) {
    if (!llvm_type->isIntegerTy()) {
      context.Diagnostics().Fail(node->location,
                                 "Integer initializer type mismatch");
    }
    return llvm::ConstantInt::get(llvm_type, std::get<uint64_t>(val), false);
  }
  if (!llvm_type->isFloatingPointTy()) {
    context.Diagnostics().Fail(node->location,
                               "Floating initializer type mismatch");
  }
  return llvm::ConstantFP::get(llvm_type, std::get<long double>(val));
}
