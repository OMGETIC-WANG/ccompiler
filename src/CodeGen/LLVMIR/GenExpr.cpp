#include "GenExpr.h"

#include "../../AST/AST.h"
#include "../../AST/Operations.h"
#include "../../AST/Type.h"
#include "GenDecl.h"
#include "IRBuilder.h"
#include "Value.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>

#include <format>
#include <span>
#include <vector>

static std::span<const RValue> AsSpan(const std::vector<RValue> &values) {
  return values;
}

static RValue TryCastTo(TypeRef type, const RValue &value, IRBuilder &builder) {
  if (type == value.c_type) {
    return value;
  }
  if (builder.Context().LLVMType(type) ==
      builder.Context().LLVMType(value.c_type)) {
    return RValue{type, value.ir_value};
  }
  int promote = GetPromote(type, value.c_type);
  if (promote == -1) {
    ASSERT(type.get() == value.c_type.get(),
           "Failed to cast between incompatible types");
    return RValue{type, value.ir_value};
  }
  return builder.CreateNumericCast(value, type);
}

static bool LosesConstInPointerConversion(TypeRef from, TypeRef to) {
  if (!from->Is<PointerType>() || !to->Is<PointerType>()) {
    return false;
  }

  TypeRef from_pointee = from->As<PointerType>()->pointee_type;
  TypeRef to_pointee = to->As<PointerType>()->pointee_type;
  if (HasTypeTag(from_pointee, TypeTag::Const) &&
      !HasTypeTag(to_pointee, TypeTag::Const)) {
    return true;
  }
  return LosesConstInPointerConversion(from_pointee, to_pointee);
}

static void CheckConstAssignment(TypeRef target, TypeRef value) {
  ASSERT(!HasTypeTag(target, TypeTag::Const),
         "Cannot assign to a const-qualified object");
  ASSERT(!LosesConstInPointerConversion(value, target),
         "Cannot assign a pointer after discarding const");
}

struct ExprEmitter::detail {
  static void GenVarInitializer(const LValue &value, TypeRef type,
                                Initializer *initializer, ExprEmitter &self) {
    if (!initializer) {
      return;
    }
    if (auto *value_init = initializer->DynCast<ValueInitializer>()) {
      RValue init = self.EmitRValue(value_init->value.get());
      self.builder.CreateStore(TryCastTo(type, init, self.builder), value);
      return;
    }
    if (auto *field_init = initializer->DynCast<FieldInitializer>()) {
      auto *struct_type = type->DynCast<StructType>();
      ASSERT(struct_type, "FieldInitializer only supports struct type");
      for (const auto &[name, init] : field_init->field_initializers) {
        auto field = struct_type->GetField(name);
        ASSERT(field.has_value(), "Unknown field: ", name);
        LValue field_lhs = self.builder.GetElementPtr(value, field.value());
        GenVarInitializer(field_lhs, field->type, init.get(), self);
      }
      return;
    }
    RAISE_ERROR("Unsupported local initializer");
  }

  static LValue GenVarDecl(VarDecl *decl, ExprEmitter &self) {
    ASSERT(decl->storage_class == StorageClass::None,
           "Local static storage is not supported yet");
    LValue value = self.builder.CreateAlloca(decl->type);
    self.context.CurrLayer().variables[decl->name] = value;
    GenVarInitializer(value, decl->type, decl->initializer.get(), self);
    return value;
  }

  static RValue LoadAsRValue(const LValue &value, ExprEmitter &self) {
    if (auto *array = value.c_type->DynCast<ArrayType>()) {
      return RValue{self.context.Types().Pointer(array->element_type),
                    value.address};
    }
    if (value.c_type->Is<FunctionType>()) {
      return RValue{self.context.Types().Pointer(value.c_type), value.address};
    }
    return self.builder.CreateLoad(value.c_type, value);
  }

  static RValue CastToI1IfNeeded(const RValue &value, ExprEmitter &self) {
    if (value.c_type->Is<BuiltinType>() &&
        value.c_type->As<BuiltinType>()->Kind() == BuiltinKind::I1) {
      return value;
    }
    return self.builder.CreateBoolCond(value);
  }

  enum class EvalAndOr { And, Or };

  template <EvalAndOr eval_kind>
  static RValue EvalAndOr(ASTNode *left, ASTNode *right, ExprEmitter &self) {
    TypeRef bool_type =
        self.context.Types().Builtin(BuiltinKind::I1, TypeTag::Unsigned);
    RValue left_value = self.EmitRValue(left);
    llvm::BasicBlock *left_block = self.builder.GetInsertBlock();
    ASSERT(left_block, "Missing insertion block in logical expression");

    size_t label_id = self.context.counter();
    auto *right_block = llvm::BasicBlock::Create(
        self.context.LLVMContext(), std::format("logic.right.{}", label_id),
        self.context.current_function);
    auto *end_block = llvm::BasicBlock::Create(
        self.context.LLVMContext(), std::format("logic.end.{}", label_id),
        self.context.current_function);

    if constexpr (eval_kind == EvalAndOr::And) {
      self.builder.CreateCondBr(left_value, right_block, end_block);
    } else {
      self.builder.CreateCondBr(left_value, end_block, right_block);
    }

    self.builder.SetInsertPoint(right_block);
    RValue right_value = self.EmitRValue(right);
    right_value = CastToI1IfNeeded(right_value, self);
    llvm::BasicBlock *right_end_block = self.builder.GetInsertBlock();
    self.builder.CreateBr(end_block);

    self.builder.SetInsertPoint(end_block);
    bool short_circuit_value = eval_kind == EvalAndOr::Or;
    return self.builder.CreatePhi(
        RValue{bool_type,
               llvm::ConstantInt::get(self.context.LLVMType(bool_type),
                                      short_circuit_value)},
        left_block, right_value, right_end_block);
  }

  static RValue EvalLogicAnd(ASTNode *left, ASTNode *right, ExprEmitter &self) {
    return EvalAndOr<EvalAndOr::And>(left, right, self);
  }

  static RValue EvalLogicOr(ASTNode *left, ASTNode *right, ExprEmitter &self) {
    return EvalAndOr<EvalAndOr::Or>(left, right, self);
  }
};

LValue ExprEmitter::EmitLValue(ASTNode *node) {
  if (auto *var_ref = node->DynCast<VarRef>()) {
    if (auto *variable = context.FindVariable(var_ref->name)) {
      return *variable;
    }
    if (auto *function = context.FindFunction(var_ref->name)) {
      return LValue{function->c_type, function->function};
    }
    RAISE_ERROR("Unknown variable or function: ", var_ref->name);
  }
  if (auto *var_decl = node->DynCast<VarDecl>()) {
    return detail::GenVarDecl(var_decl, *this);
  }
  if (auto *get_field = node->DynCast<GetField>()) {
    LValue operand = EmitLValue(get_field->operand.get());
    return builder.GetElementPtr(operand, get_field->field_name);
  }
  if (auto *binary = node->DynCast<BinaryOp>()) {
    if (binary->op_type == BinaryOpType::Subscript) {
      RValue array = EmitRValue(binary->left.get());
      RValue index = EmitRValue(binary->right.get());
      return builder.CreateSubscript(array, index);
    }
    RAISE_ERROR("Unexpected lvalue binary operation");
  }
  if (auto *unary = node->DynCast<UnaryOp>()) {
    if (unary->op_type == UnaryOpType::Deref) {
      RValue ptr = EmitRValue(unary->operand.get());
      ASSERT(ptr.c_type->Is<PointerType>(),
             "Dereference requires a pointer operand");
      return LValue{ptr.c_type->As<PointerType>()->pointee_type, ptr.ir_value};
    }
  }
  return {};
}

RValue ExprEmitter::EmitRValue(ASTNode *node) {
  if (auto *literal_num = node->DynCast<LiteralNum>()) {
    if (std::holds_alternative<int64_t>(literal_num->value)) {
      TypeRef type = context.Types().Builtin(BuiltinKind::Int);
      return RValue{type, llvm::ConstantInt::get(
                              context.LLVMType(type),
                              std::get<int64_t>(literal_num->value), true)};
    }
    if (std::holds_alternative<double>(literal_num->value)) {
      TypeRef type = context.Types().Builtin(BuiltinKind::Float);
      return RValue{
          type, llvm::ConstantFP::get(context.LLVMType(type),
                                      std::get<double>(literal_num->value))};
    }
    RAISE_ERROR("Unsupported literal num type");
  }
  if (auto *literal_str = node->DynCast<LiteralString>()) {
    TypeRef type =
        context.Types().Pointer(context.Types().Builtin(BuiltinKind::Char));
    return RValue{type, context.CreateStringConstant(literal_str->value)};
  }
  if (auto *var_decl = node->DynCast<VarDecl>()) {
    return detail::LoadAsRValue(detail::GenVarDecl(var_decl, *this), *this);
  }
  if (auto *bin_expr = node->DynCast<BinaryOp>()) {
    switch (bin_expr->op_type) {
    case BinaryOpType::Assignment: {
      LValue left = EmitLValue(bin_expr->left.get());
      ASSERT(left.address, "Assignment requires an lvalue");
      RValue right = EmitRValue(bin_expr->right.get());
      CheckConstAssignment(left.c_type, right.c_type);
      int promote = GetPromote(left.c_type, right.c_type);
      ASSERT(promote == -1 || promote == 1, "Failed to cast assignment");
      if (promote == 1) {
        right = builder.CreateNumericCast(right, left.c_type);
      } else {
        right = TryCastTo(left.c_type, right, builder);
      }
      builder.CreateStore(right, left);
      return right;
    }
    case BinaryOpType::And:
      return detail::EvalLogicAnd(bin_expr->left.get(), bin_expr->right.get(),
                                  *this);
    case BinaryOpType::Or:
      return detail::EvalLogicOr(bin_expr->left.get(), bin_expr->right.get(),
                                 *this);
    case BinaryOpType::Eq:
    case BinaryOpType::Ne:
    case BinaryOpType::Lt:
    case BinaryOpType::Gt:
    case BinaryOpType::Le:
    case BinaryOpType::Ge:
    case BinaryOpType::Add:
    case BinaryOpType::Sub:
    case BinaryOpType::Mul:
    case BinaryOpType::Div:
    case BinaryOpType::Mod: {
      RValue left = EmitRValue(bin_expr->left.get());
      RValue right = EmitRValue(bin_expr->right.get());
      if (left.c_type->Is<PointerType>()) {
        ASSERT(IsIntegerType(right.c_type),
               "Pointer arithmetic only supports integer type");
        ASSERT(bin_expr->op_type == BinaryOpType::Add ||
                   bin_expr->op_type == BinaryOpType::Sub,
               "Pointer arithmetic only supports addition and subtraction");
        if (bin_expr->op_type == BinaryOpType::Sub) {
          right = builder.CreateInv(right);
        }
        LValue subscripted = builder.CreateSubscript(left, right);
        return RValue{left.c_type, subscripted.address};
      }

      int promote = GetPromote(left.c_type, right.c_type);
      if (promote == 0) {
        left = builder.CreateNumericCast(left, right.c_type);
      } else if (promote == 1) {
        right = builder.CreateNumericCast(right, left.c_type);
      }
      bool is_arithmetic = bin_expr->op_type == BinaryOpType::Add ||
                           bin_expr->op_type == BinaryOpType::Sub ||
                           bin_expr->op_type == BinaryOpType::Mul ||
                           bin_expr->op_type == BinaryOpType::Div ||
                           bin_expr->op_type == BinaryOpType::Mod;
      TypeRef out_type =
          is_arithmetic
              ? left.c_type
              : context.Types().Builtin(BuiltinKind::I1, TypeTag::Unsigned);
      return builder.CreateBinaryOp(bin_expr->op_type, left, right, out_type);
    }
    default:
      break;
    }
  }
  if (auto *func_invoke = node->DynCast<FunctionInvocation>()) {
    std::vector<RValue> args;
    args.reserve(func_invoke->arguments.size());
    for (const auto &argument : func_invoke->arguments) {
      args.push_back(EmitRValue(argument.get()));
    }
    RValue function = EmitRValue(func_invoke->function.get());
    return builder.CreateFuncInvoke(function, AsSpan(args));
  }
  if (auto *type_cast = node->DynCast<TypeCast>()) {
    RValue operand = EmitRValue(type_cast->operand.get());
    TypeRef target_type = type_cast->target_type;
    if (operand.c_type == target_type) {
      return operand;
    }
    if (operand.c_type->Is<PointerType>() && target_type->Is<PointerType>()) {
      ASSERT(!LosesConstInPointerConversion(operand.c_type, target_type),
             "Cannot cast a pointer after discarding const");
      return RValue{target_type, operand.ir_value};
    }
    if (IsNumericType(operand.c_type) && IsNumericType(target_type)) {
      return builder.CreateNumericCast(operand, target_type);
    }
    RAISE_ERROR("Unsupported type cast from ", operand.c_type->ToString(),
                " to ", target_type->ToString());
  }
  if (auto *unary_expr = node->DynCast<UnaryOp>()) {
    switch (unary_expr->op_type) {
    case UnaryOpType::GetAddress: {
      LValue operand = EmitLValue(unary_expr->operand.get());
      ASSERT(operand.address, "Address-of requires an lvalue");
      return RValue{context.Types().Pointer(operand.c_type), operand.address};
    }
    case UnaryOpType::IncBeforeUse:
    case UnaryOpType::IncAfterUse:
    case UnaryOpType::DecBeforeUse:
    case UnaryOpType::DecAfterUse: {
      bool is_inc = unary_expr->op_type == UnaryOpType::IncBeforeUse ||
                    unary_expr->op_type == UnaryOpType::IncAfterUse;
      bool is_before_use = unary_expr->op_type == UnaryOpType::IncBeforeUse ||
                           unary_expr->op_type == UnaryOpType::DecBeforeUse;

      LValue operand = EmitLValue(unary_expr->operand.get());
      ASSERT(operand.address, "Increment/decrement requires an lvalue");
      ASSERT(!HasTypeTag(operand.c_type, TypeTag::Const),
             "Cannot modify a const-qualified object");
      RValue old_value = builder.CreateLoad(operand.c_type, operand);
      RValue one{operand.c_type,
                 llvm::ConstantInt::get(context.LLVMType(operand.c_type), 1)};
      RValue updated =
          builder.CreateBinaryOp(is_inc ? BinaryOpType::Add : BinaryOpType::Sub,
                                 old_value, one, operand.c_type);
      builder.CreateStore(updated, operand);
      return is_before_use ? updated : old_value;
    }
    default:
      break;
    }
  }

  LValue lhs = EmitLValue(node);
  if (lhs.address) {
    return detail::LoadAsRValue(lhs, *this);
  }
  RAISE_ERROR("Unsupported ASTNode type: ", node->GetType());
}

llvm::BasicBlock *ExprEmitter::GetOrCreateLabelBlock(std::string_view name) {
  if (auto found = label_blocks.find(name); found != label_blocks.end()) {
    return found->second;
  }
  auto *block = llvm::BasicBlock::Create(context.LLVMContext(),
                                         std::format("label.{}", name),
                                         context.current_function);
  label_blocks.emplace(name, block);
  return block;
}

void ExprEmitter::EmitStmt(ASTNode *node) {
  if (auto *raw_ir = node->DynCast<RawLLVMIR>()) {
    (void)raw_ir;
    RAISE_ERROR("RawLLVMIR is no longer supported by LLVM API code "
                "generation");
  }

  if (builder.IsTerminated()) {
    if (auto *label_stmt = node->DynCast<LabelStmt>()) {
      builder.SetInsertPoint(GetOrCreateLabelBlock(label_stmt->name));
      return;
    }
    if (auto *compound = node->DynCast<CompoundStmt>()) {
      GenContextLayerGuard guard(context);
      for (const auto &statement : compound->statements) {
        EmitStmt(statement.get());
      }
    }
    return;
  }

  if (auto *var_decl = node->DynCast<VarDecl>()) {
    detail::GenVarDecl(var_decl, *this);
    return;
  }
  if (auto *label_stmt = node->DynCast<LabelStmt>()) {
    llvm::BasicBlock *target = GetOrCreateLabelBlock(label_stmt->name);
    if (builder.GetInsertBlock() != target) {
      builder.CreateBr(target);
    }
    builder.SetInsertPoint(target);
    return;
  }
  if (auto *goto_stmt = node->DynCast<GotoStmt>()) {
    builder.CreateBr(GetOrCreateLabelBlock(goto_stmt->target));
    return;
  }
  if (auto *if_stmt = node->DynCast<IfStmt>()) {
    RValue condition = EmitRValue(if_stmt->condition.get());
    size_t label_id = context.counter();
    auto *true_block = llvm::BasicBlock::Create(
        context.LLVMContext(), std::format("if.true.{}", label_id),
        context.current_function);
    llvm::BasicBlock *false_block = nullptr;
    if (if_stmt->else_branch) {
      false_block = llvm::BasicBlock::Create(
          context.LLVMContext(), std::format("if.false.{}", label_id),
          context.current_function);
    }
    auto *end_block = llvm::BasicBlock::Create(
        context.LLVMContext(), std::format("if.end.{}", label_id),
        context.current_function);
    builder.CreateCondBr(condition, true_block,
                         false_block ? false_block : end_block);

    builder.SetInsertPoint(true_block);
    {
      GenContextLayerGuard guard(context);
      EmitStmt(if_stmt->then_branch.get());
    }
    bool then_fallthrough = !builder.IsTerminated();
    if (then_fallthrough) {
      builder.CreateBr(end_block);
    }

    bool else_fallthrough = false;
    bool need_end = !if_stmt->else_branch;
    if (if_stmt->else_branch) {
      builder.SetInsertPoint(false_block);
      {
        GenContextLayerGuard guard(context);
        EmitStmt(if_stmt->else_branch.get());
      }
      else_fallthrough = !builder.IsTerminated();
      if (else_fallthrough) {
        builder.CreateBr(end_block);
      }
      need_end = then_fallthrough || else_fallthrough;
    }

    if (need_end) {
      builder.SetInsertPoint(end_block);
    } else {
      end_block->eraseFromParent();
    }
    return;
  }
  if (auto *ret_stmt = node->DynCast<ReturnStmt>()) {
    if (ret_stmt->ret_value) {
      RValue ret = EmitRValue(ret_stmt->ret_value.get());
      TypeRef return_type =
          context.current_function_type->As<FunctionType>()->return_type;
      ret = TryCastTo(return_type, ret, builder);
      builder.CreateRet(ret);
    } else {
      builder.CreateVoidRet();
    }
    return;
  }
  if (auto *compound = node->DynCast<CompoundStmt>()) {
    GenContextLayerGuard guard(context);
    for (const auto &statement : compound->statements) {
      EmitStmt(statement.get());
    }
    return;
  }
  if (auto *struct_decl = node->DynCast<StructDecl>()) {
    GenStructDecl(struct_decl, context);
    return;
  }
  EmitRValue(node);
}
