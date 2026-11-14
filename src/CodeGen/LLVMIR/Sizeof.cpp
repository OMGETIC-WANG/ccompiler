#include "Sizeof.h"
#include "../../AST/AST.h"
#include "../../AST/Operations.h"
#include "../../Util/Diagnostic.h"
#include "GenContext.h"
#include "GenExpr.h"

static TypeRef EvalBinOpResType(TypeRef left, TypeRef right, BinaryOp *node,
                                GenContext &context) {
  ASSERT(node != nullptr, "Binary operator node must not be null");
  BinaryOpType op = node->op_type;
  TypeContext &types = context.Types();
  auto decay_array = [&types](TypeRef type) -> TypeRef {
    if (auto *array = type->DynCast<ArrayType>()) {
      return types.Pointer(array->element_type);
    }
    return type;
  };

  switch (op) {
  case BinaryOpType::Assignment:
  case BinaryOpType::AddAssign:
  case BinaryOpType::SubAssign:
  case BinaryOpType::MulAssign:
  case BinaryOpType::DivAssign:
  case BinaryOpType::ModAssign:
  case BinaryOpType::BitAndAssign:
  case BinaryOpType::BitOrAssign:
  case BinaryOpType::BitXorAssign:
  case BinaryOpType::LShiftAssign:
  case BinaryOpType::RShiftAssign:
    return StripTypeTag(left, TypeTag::Const);
  case BinaryOpType::Subscript:
    left = decay_array(left);
    right = decay_array(right);
    if (auto *pointer = left->DynCast<PointerType>()) {
      return pointer->pointee_type;
    }
    if (auto *pointer = right->DynCast<PointerType>()) {
      return pointer->pointee_type;
    }
    context.Diagnostics().Fail(
        node->location, "Subscript requires an array or pointer operand");
  case BinaryOpType::Add:
  case BinaryOpType::Sub:
    left = decay_array(left);
    right = decay_array(right);
    if (left->Is<PointerType>()) {
      if (right->Is<PointerType>()) {
        if (op != BinaryOpType::Sub) {
          context.Diagnostics().Fail(
              node->location,
              "Pointer subtraction is the only pointer-pointer operation");
        }
        return types.getLong();
      }
      if (!IsIntegerType(right)) {
        context.Diagnostics().Fail(
            node->location, "Pointer arithmetic requires an integer operand");
      }
      return left;
    }
    if (right->Is<PointerType>() && op == BinaryOpType::Add) {
      if (!IsIntegerType(left)) {
        context.Diagnostics().Fail(
            node->location, "Pointer arithmetic requires an integer operand");
      }
      return right;
    }
    [[fallthrough]];
  case BinaryOpType::Mul:
  case BinaryOpType::Div:
    if (!IsNumericType(left) || !IsNumericType(right)) {
      context.Diagnostics().Fail(
          node->location, "Arithmetic operators require numeric operands");
    }
    return UsualArithmeticType(left, right, types);
  case BinaryOpType::Mod:
  case BinaryOpType::BitAnd:
  case BinaryOpType::BitOr:
  case BinaryOpType::BitXor:
    if (!IsIntegerType(left) || !IsIntegerType(right)) {
      context.Diagnostics().Fail(
          node->location,
          "Modulo and bitwise operators require integer operands");
    }
    return UsualArithmeticType(left, right, types);
  case BinaryOpType::LShift:
  case BinaryOpType::RShift:
    if (!IsIntegerType(left) || !IsIntegerType(right)) {
      context.Diagnostics().Fail(node->location,
                                 "Shift operators require integer operands");
    }
    return PromoteInteger(left, types);
  case BinaryOpType::Eq:
  case BinaryOpType::Ne:
  case BinaryOpType::Lt:
  case BinaryOpType::Gt:
  case BinaryOpType::Le:
  case BinaryOpType::Ge:
    left = decay_array(left);
    right = decay_array(right);
    if (!((IsNumericType(left) && IsNumericType(right)) ||
          (left->Is<PointerType>() && right->Is<PointerType>()))) {
      context.Diagnostics().Fail(
          node->location,
          "Comparison requires compatible numeric or pointer operands");
    }
    return types.getInt();
  case BinaryOpType::And:
  case BinaryOpType::Or:
    left = decay_array(left);
    right = decay_array(right);
    if (!((IsNumericType(left) || left->Is<PointerType>()) &&
          (IsNumericType(right) || right->Is<PointerType>()))) {
      context.Diagnostics().Fail(node->location,
                                 "Logical operators require scalar operands");
    }
    return types.getInt();
  }

  context.Diagnostics().Fail(
      node->location, "Unsupported binary operator for type evaluation: ", op);
}

static TypeRef EvalUnaryOpResType(TypeRef operand, UnaryOp *node,
                                  GenContext &context) {
  ASSERT(node != nullptr, "Unary operator node must not be null");
  UnaryOpType op = node->op_type;
  TypeContext &types = context.Types();
  auto decay_array = [&types](TypeRef type) -> TypeRef {
    if (auto *array = type->DynCast<ArrayType>()) {
      return types.Pointer(array->element_type);
    }
    return type;
  };

  switch (op) {
  case UnaryOpType::IncBeforeUse:
  case UnaryOpType::IncAfterUse:
  case UnaryOpType::DecBeforeUse:
  case UnaryOpType::DecAfterUse:
    if (!IsNumericType(operand) && !operand->Is<PointerType>()) {
      context.Diagnostics().Fail(
          node->location, "Increment/decrement requires a scalar operand");
    }
    return StripTypeTag(operand, TypeTag::Const);
  case UnaryOpType::Deref:
    if (auto *pointer = operand->DynCast<PointerType>()) {
      return pointer->pointee_type;
    }
    context.Diagnostics().Fail(node->location,
                               "Dereference requires a pointer operand");
  case UnaryOpType::GetAddress:
    return types.Pointer(operand);
  case UnaryOpType::Not:
    operand = decay_array(operand);
    if (!IsNumericType(operand) && !operand->Is<PointerType>()) {
      context.Diagnostics().Fail(node->location,
                                 "Logical NOT requires a scalar operand");
    }
    return types.getInt();
  case UnaryOpType::BitNot:
    if (!IsIntegerType(operand)) {
      context.Diagnostics().Fail(node->location,
                                 "Bitwise NOT requires an integer operand");
    }
    return PromoteInteger(operand, types);
  case UnaryOpType::UnaryPlus:
  case UnaryOpType::UnaryMinus:
    if (!IsNumericType(operand)) {
      context.Diagnostics().Fail(node->location,
                                 "Unary plus/minus requires a numeric operand");
    }
    if (IsIntegerType(operand)) {
      return PromoteInteger(operand, types);
    }
    return StripTypeTag(operand, TypeTag::Const);
  case UnaryOpType::GetField:
  case UnaryOpType::PtrGetField:
    RAISE_ERROR("Field access must be evaluated by EvalType");
  case UnaryOpType::TypeCast:
    RAISE_ERROR("Type cast must be evaluated by EvalType");
  }

  context.Diagnostics().Fail(
      node->location, "Unsupported unary operator for type evaluation: ", op);
}

static TypeRef EvalType(ASTNode *node, GenContext &context) {
  auto get_field_type = [&context](TypeRef type, std::string_view field_name,
                                   SourceLocation location) -> TypeRef {
    if (auto *struct_type = type->DynCast<StructType>()) {
      auto field = struct_type->GetField(field_name);
      if (!field.has_value()) {
        context.Diagnostics().Fail(location,
                                   "Unknown struct field: ", field_name);
      }
      return field->type;
    }
    if (auto *union_type = type->DynCast<UnionType>()) {
      auto field = union_type->GetField(field_name);
      if (!field.has_value()) {
        context.Diagnostics().Fail(location,
                                   "Unknown union field: ", field_name);
      }
      return field->type;
    }
    context.Diagnostics().Fail(
        location, "Field access requires a struct or union operand");
  };
  auto eval_unary = [&context](UnaryOp *unary_expr) {
    auto operand_type = EvalType(unary_expr->operand.get(), context);
    return EvalUnaryOpResType(operand_type, unary_expr, context);
  };

  if (auto literal_num = node->DynCast<LiteralNum>()) {
    return literal_num->type;
  }
  if (node->Is<LiteralString>()) {
    return context.Types().Pointer(context.Types().getChar());
  }
  if (auto bin_expr = node->DynCast<BinaryOp>()) {
    auto left_type = EvalType(bin_expr->left.get(), context);
    auto right_type = EvalType(bin_expr->right.get(), context);
    return EvalBinOpResType(left_type, right_type, bin_expr, context);
  }
  if (auto type_cast = node->DynCast<TypeCast>()) {
    EvalType(type_cast->operand.get(), context);
    ASSERT(type_cast->target_type != nullptr,
           "Type cast is missing its target type");
    return type_cast->target_type;
  }
  if (auto get_field = node->DynCast<GetField>()) {
    auto operand_type = EvalType(get_field->operand.get(), context);
    return get_field_type(operand_type, get_field->field_name,
                          get_field->location);
  }
  if (auto ptr_get_field = node->DynCast<PtrGetField>()) {
    auto operand_type = EvalType(ptr_get_field->operand.get(), context);
    if (auto *pointer = operand_type->DynCast<PointerType>()) {
      return get_field_type(pointer->pointee_type, ptr_get_field->field_name,
                            ptr_get_field->location);
    }
    context.Diagnostics().Fail(
        ptr_get_field->location,
        "Pointer field access requires a pointer operand");
  }
  if (auto unary_expr = node->DynCast<UnaryOp>()) {
    return eval_unary(unary_expr);
  }
  if (auto func_invoke = node->DynCast<FunctionInvocation>()) {
    auto func_type = EvalType(func_invoke->function.get(), context);
    if (auto *func = func_type->DynCast<FunctionType>()) {
      return func->return_type;
    } else {
      context.Diagnostics().Fail(
          func_invoke->location,
          "Function invocation requires a function or function pointer");
    }
  }
  if (auto var_ref = node->DynCast<VarRef>()) {
    if (!var_ref->decl) {
      context.Diagnostics().Fail(
          var_ref->location, "Unknown variable or function: ", var_ref->name);
    }
    if (auto type = context.FindDeclType(var_ref->decl)) {
      return *type;
    }
    context.Diagnostics().Fail(
        var_ref->location,
        "Declaration has no generated value: ", var_ref->name);
  }
  context.Diagnostics().Fail(
      node->location, "Cannot evaluate type of node: ", node->ToString());
}

RValue GenSizeof(std::vector<IntrinsicCallArg> &args,
                 std::variant<ExprEmitter *, GenContext *> arg) {
  GenContext *context;
  if (std::holds_alternative<ExprEmitter *>(arg)) {
    context = &std::get<ExprEmitter *>(arg)->context;
  } else {
    context = std::get<GenContext *>(arg);
  }

  TypeRef ull_type = context->Types().getLongLong();
  size_t res;

  if (std::holds_alternative<TypeRef>(args[0])) {
    TypeRef type = std::get<TypeRef>(args[0]);
    res = type->GetSize();
  } else {
    ASTNode *node = std::get<std::unique_ptr<ASTNode>>(args[0]).get();
    res = EvalType(node, *context)->GetSize();
  }
  return RValue{ull_type, llvm::ConstantInt::get(
                              llvm::Type::getInt64Ty(context->LLVMContext()),
                              res, false)};
}
