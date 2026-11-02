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
  ASSERT(IsNumericType(type) && IsNumericType(value.c_type),
         "Failed to cast between incompatible types");
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

static RValue CreateIndex(size_t index, GenContext &context) {
  return RValue{
      context.Types().Builtin(BuiltinKind::Int),
      llvm::ConstantInt::get(llvm::Type::getInt64Ty(context.LLVMContext()),
                             index, false)};
}

static RValue CreateNullValue(TypeRef type, GenContext &context) {
  return RValue{type, llvm::Constant::getNullValue(context.LLVMType(type))};
}

static bool IsLiteralStringInit(const ArrayType *type,
                                const Initializer *initializer) {
  return type->element_type->Is<BuiltinType>() &&
         type->element_type->As<BuiltinType>()->Kind() == BuiltinKind::Char &&
         initializer->Is<ValueInitializer>() &&
         initializer->As<ValueInitializer>()->value->Is<LiteralString>();
}

struct ExprEmitter::detail {
  static void GenVarInitializer(const LValue &value, TypeRef type,
                                Initializer *initializer, ExprEmitter &self) {
    if (auto *array_type = type->DynCast<ArrayType>()) {
      if (auto *array_init = initializer->DynCast<ArrayInitializer>()) {
        size_t array_size =
            array_type->size.value_or(array_init->initializers.size());
        ASSERT(array_init->initializers.size() <= array_size,
               "Array initializer has more elements than the array size");

        RValue array_base = LoadAsRValue(value, self);
        for (size_t i = 0; i < array_size; ++i) {
          if (i < array_init->initializers.size()) {
            GenVarInitializer(self.builder.CreateSubscript(
                                  array_base, CreateIndex(i, self.context)),
                              array_type->element_type,
                              array_init->initializers[i].get(), self);
          } else {
            self.builder.CreateStore(
                CreateNullValue(array_type->element_type, self.context),
                self.builder.CreateSubscript(array_base,
                                             CreateIndex(i, self.context)));
          }
        }
      }
      if (IsLiteralStringInit(array_type, initializer)) {
        auto *literal_str =
            initializer->As<ValueInitializer>()->value->As<LiteralString>();
        ASSERT(array_type->size.value() >= literal_str->value.view().size() + 1,
               "Array size is smaller than the literal string size");
        auto s = self.context.CreateStringConstant(literal_str->value.view(),
                                                   array_type->size.value());
        self.builder.CreateMemcpy(value, RValue{nullptr, s},
                                  array_type->size.value(), 1);
      }
      return;
    }

    if (auto *struct_type = type->DynCast<StructType>()) {
      if (auto *field_init = initializer->DynCast<FieldInitializer>()) {
        std::vector<uint8_t> fields_inited(struct_type->fields.size(), false);
        for (const auto &[name, init] : field_init->field_initializers) {
          auto field = struct_type->GetField(name);
          ASSERT(field.has_value(), "Unknown field name: ", name);
          size_t field_index = 0;
          bool found = false;
          for (size_t i = 0; i < struct_type->fields.size(); ++i) {
            if (struct_type->fields[i].name == name) {
              field_index = i;
              found = true;
              break;
            }
          }
          ASSERT(found, "Unknown field name: ", name);
          fields_inited[field_index] = true;
          GenVarInitializer(self.builder.GetElementPtr(value, field.value()),
                            field->type, init.get(), self);
        }
        for (size_t i = 0; i < struct_type->fields.size(); i++) {
          if (!fields_inited[i]) {
            self.builder.CreateStore(
                CreateNullValue(struct_type->fields[i].type, self.context),
                self.builder.GetElementPtr(value, struct_type->fields[i]));
          }
        }
      }
      if (auto *array_init = initializer->DynCast<ArrayInitializer>()) {
        ASSERT(array_init->initializers.size() <= struct_type->fields.size(),
               "Struct initializer has more elements than the struct fields");
        for (size_t i = 0; i < array_init->initializers.size(); i++) {
          auto &field = struct_type->fields[i];
          GenVarInitializer(self.builder.GetElementPtr(value, field),
                            field.type, array_init->initializers[i].get(),
                            self);
        }
        for (size_t i = array_init->initializers.size();
             i < struct_type->fields.size(); i++) {
          auto &field = struct_type->fields[i];
          self.builder.CreateStore(CreateNullValue(field.type, self.context),
                                   self.builder.GetElementPtr(value, field));
        }
      }
      return;
    }

    if (auto *val_init = initializer->DynCast<ValueInitializer>()) {
      RValue val = self.EmitRValue(val_init->value.get());
      val = TryCastTo(type, val, self.builder);
      self.builder.CreateStore(val, value);
      return;
    }

    RAISE_ERROR("Unexpected init");
  }

  static LValue GenVarDecl(VarDecl *decl, ExprEmitter &self) {
    ASSERT(decl->storage_class == StorageClass::None,
           "Local static storage is not supported yet");
    TypeRef type = decl->type;
    if (decl->type->Is<ArrayType>()) {
      auto array_type = decl->type->As<ArrayType>();
      if (!array_type->size.has_value()) {
        ASSERT(decl->initializer.get(),
               "Array with unspecified size requires an initializer");
        if (auto *array_init = decl->initializer->DynCast<ArrayInitializer>()) {
          if (!array_type->size.has_value()) {
            type = self.context.Types().Array(array_type->element_type,
                                              array_init->initializers.size(),
                                              (TypeTag)type.get_tag());
          } else {
            ASSERT(array_type->size.value() >= array_init->initializers.size(),
                   "Array initializer has more elements than the array size");
          }
        } else {
          if (IsLiteralStringInit(array_type, decl->initializer.get())) {
            type = self.context.Types().Array(
                array_type->element_type,
                decl->initializer->As<ValueInitializer>()
                        ->value->As<LiteralString>()
                        ->value.view()
                        .size() +
                    1,
                (TypeTag)type.get_tag());
          } else {
            RAISE_ERROR("Failed to init array");
          }
        }
      } else {
        // TODO: check initializer and size
      }
    }
    LValue value = self.builder.CreateAlloca(type);
    self.context.CurrLayer().variables[decl->name] = value;
    if (decl->initializer) {
      GenVarInitializer(value, type, decl->initializer.get(), self);
    }
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

  static TypeRef PromoteNumericOperands(RValue &left, RValue &right,
                                        ExprEmitter &self) {
    TypeRef common_type =
        UsualArithmeticType(left.c_type, right.c_type, self.context.Types());
    left = TryCastTo(common_type, left, self.builder);
    right = TryCastTo(common_type, right, self.builder);
    return common_type;
  }

  static RValue EvalBinaryOp(BinaryOpType op_type, RValue left, RValue right,
                             ExprEmitter &self) {
    if (op_type == BinaryOpType::BitAnd || op_type == BinaryOpType::BitOr ||
        op_type == BinaryOpType::BitXor) {
      ASSERT(IsIntegerType(left.c_type),
             "Bitwise operators require an integer left operand");
      ASSERT(IsIntegerType(right.c_type),
             "Bitwise operators require an integer right operand");
      TypeRef result_type = PromoteNumericOperands(left, right, self);
      return self.builder.CreateBinaryOp(op_type, left, right, result_type);
    }

    if (op_type == BinaryOpType::LShift || op_type == BinaryOpType::RShift) {
      ASSERT(IsIntegerType(left.c_type),
             "Shift operators require an integer left operand");
      ASSERT(IsIntegerType(right.c_type),
             "Shift operators require an integer right operand");
      left = TryCastTo(PromoteInteger(left.c_type, self.context.Types()), left,
                       self.builder);
      right = TryCastTo(PromoteInteger(right.c_type, self.context.Types()),
                        right, self.builder);
      if (left.c_type != right.c_type) {
        right = TryCastTo(left.c_type, right, self.builder);
      }
      return self.builder.CreateBinaryOp(op_type, left, right, left.c_type);
    }

    if (left.c_type->Is<PointerType>()) {
      ASSERT(IsIntegerType(right.c_type),
             "Pointer arithmetic only supports integer type");
      ASSERT(op_type == BinaryOpType::Add || op_type == BinaryOpType::Sub,
             "Pointer arithmetic only supports addition and subtraction");
      if (op_type == BinaryOpType::Sub) {
        right = self.builder.CreateInv(right);
      }
      LValue subscripted = self.builder.CreateSubscript(left, right);
      return RValue{left.c_type, subscripted.address};
    }

    TypeRef result_type = PromoteNumericOperands(left, right, self);
    bool is_comparison =
        op_type == BinaryOpType::Eq || op_type == BinaryOpType::Ne ||
        op_type == BinaryOpType::Lt || op_type == BinaryOpType::Gt ||
        op_type == BinaryOpType::Le || op_type == BinaryOpType::Ge;
    TypeRef out_type =
        is_comparison
            ? self.context.Types().Builtin(BuiltinKind::I1, TypeTag::Unsigned)
            : result_type;
    return self.builder.CreateBinaryOp(op_type, left, right, out_type);
  }
};

static BinaryOpType OpAssignToOp(BinaryOpType op_assign_op) {
  switch (op_assign_op) {
  case BinaryOpType::AddAssign:
    return BinaryOpType::Add;
  case BinaryOpType::SubAssign:
    return BinaryOpType::Sub;
  case BinaryOpType::MulAssign:
    return BinaryOpType::Mul;
  case BinaryOpType::DivAssign:
    return BinaryOpType::Div;
  case BinaryOpType::ModAssign:
    return BinaryOpType::Mod;
  case BinaryOpType::BitAndAssign:
    return BinaryOpType::BitAnd;
  case BinaryOpType::BitOrAssign:
    return BinaryOpType::BitOr;
  case BinaryOpType::BitXorAssign:
    return BinaryOpType::BitXor;
  case BinaryOpType::LShiftAssign:
    return BinaryOpType::LShift;
  case BinaryOpType::RShiftAssign:
    return BinaryOpType::RShift;
  default:
    RAISE_ERROR("Unsupported assignment operator");
  }
}

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
    if (!operand.address) {
      return {};
    }
    return builder.GetElementPtr(operand, get_field->field_name);
  }
  if (auto *ptr_get_field = node->DynCast<PtrGetField>()) {
    RValue ptr = EmitRValue(ptr_get_field->operand.get());
    return builder.GetPtrElementPtr(ptr, ptr_get_field->field_name);
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
    return RValue{
        type, context.CreateStringConstant(
                  literal_str->value, literal_str->value.view().size() + 1)};
  }
  if (auto *var_decl = node->DynCast<VarDecl>()) {
    return detail::LoadAsRValue(detail::GenVarDecl(var_decl, *this), *this);
  }
  if (auto *get_field = node->DynCast<GetField>()) {
    LValue operand_lhs = EmitLValue(get_field->operand.get());
    if (operand_lhs.address) {
      LValue field_lhs =
          builder.GetElementPtr(operand_lhs, get_field->field_name);
      return detail::LoadAsRValue(field_lhs, *this);
    }
    RValue operand_rhs = EmitRValue(get_field->operand.get());
    return builder.CreateExtractValue(operand_rhs, get_field->field_name);
  }
  if (auto *bin_expr = node->DynCast<BinaryOp>()) {
    switch (bin_expr->op_type) {
    case BinaryOpType::Assignment: {
      LValue left = EmitLValue(bin_expr->left.get());
      ASSERT(left.address, "Assignment requires an lvalue");
      RValue right = EmitRValue(bin_expr->right.get());
      CheckConstAssignment(left.c_type, right.c_type);
      right = TryCastTo(left.c_type, right, builder);
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
    case BinaryOpType::Mod:
    case BinaryOpType::BitAnd:
    case BinaryOpType::BitOr:
    case BinaryOpType::BitXor:
    case BinaryOpType::LShift:
    case BinaryOpType::RShift: {
      RValue left = EmitRValue(bin_expr->left.get());
      RValue right = EmitRValue(bin_expr->right.get());
      return detail::EvalBinaryOp(bin_expr->op_type, left, right, *this);
    }
    case BinaryOpType::AddAssign:
    case BinaryOpType::SubAssign:
    case BinaryOpType::MulAssign:
    case BinaryOpType::DivAssign:
    case BinaryOpType::ModAssign:
    case BinaryOpType::BitAndAssign:
    case BinaryOpType::BitOrAssign:
    case BinaryOpType::BitXorAssign:
    case BinaryOpType::LShiftAssign:
    case BinaryOpType::RShiftAssign: {
      LValue left_lhs = EmitLValue(bin_expr->left.get());
      ASSERT(left_lhs.address, "Assignment requires an lvalue");
      RValue left = detail::LoadAsRValue(left_lhs, *this);
      RValue right = EmitRValue(bin_expr->right.get());

      RValue res = detail::EvalBinaryOp(OpAssignToOp(bin_expr->op_type), left,
                                        right, *this);
      CheckConstAssignment(left_lhs.c_type, res.c_type);
      if (res.c_type != left_lhs.c_type) {
        res = builder.CreateNumericCast(res, left_lhs.c_type);
      }
      builder.CreateStore(res, left_lhs);
      return res;
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
  if (auto *va_start = node->DynCast<VaStart>()) {
    LValue va_list = EmitLValue(va_start->va_list.get());
    builder.CreateVaStart(va_list);
    return RValue{context.Types().Builtin(BuiltinKind::Void), nullptr};
  }
  if (auto *va_arg = node->DynCast<VaArg>()) {
    LValue va_list = EmitLValue(va_arg->va_list.get());
    return builder.CreateVaArg(va_list, va_arg->type);
  }
  if (auto *va_end = node->DynCast<VaEnd>()) {
    LValue va_list = EmitLValue(va_end->va_list.get());
    builder.CreateVaEnd(va_list);
    return RValue{context.Types().Builtin(BuiltinKind::Void), nullptr};
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
    case UnaryOpType::UnaryPlus: {
      RValue operand = EmitRValue(unary_expr->operand.get());
      ASSERT(IsNumericType(operand.c_type),
             "Unary plus requires a numeric type");
      if (IsIntegerType(operand.c_type)) {
        operand = TryCastTo(PromoteInteger(operand.c_type, context.Types()),
                            operand, builder);
      }
      return operand;
    }
    case UnaryOpType::UnaryMinus: {
      RValue operand = EmitRValue(unary_expr->operand.get());
      ASSERT(IsNumericType(operand.c_type),
             "Unary minus requires a numeric type");
      if (IsIntegerType(operand.c_type)) {
        operand = TryCastTo(PromoteInteger(operand.c_type, context.Types()),
                            operand, builder);
      }
      return builder.CreateInv(operand);
    }
    case UnaryOpType::BitNot: {
      RValue operand = EmitRValue(unary_expr->operand.get());
      ASSERT(IsIntegerType(operand.c_type),
             "Bitwise NOT requires an integer type");
      operand = TryCastTo(PromoteInteger(operand.c_type, context.Types()),
                          operand, builder);
      return builder.CreateBitNot(operand);
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
    case UnaryOpType::Not: {
      RValue operand = EmitRValue(unary_expr->operand.get());
      ASSERT(IsNumericType(operand.c_type) || operand.c_type->Is<PointerType>(),
             "Logical NOT requires a numeric or pointer type");
      return builder.CreateInvBoolCond(operand);
    }
    default:
      break;
    }
  }

  try {
    LValue lhs = EmitLValue(node);
    if (lhs.address) {
      return detail::LoadAsRValue(lhs, *this);
    }
  } catch (...) {
    if (auto var_ref = node->DynCast<VarRef>()) {
      auto enum_val = context.FindEnumValue(var_ref->name);
      if (enum_val.has_value()) {
        return enum_val.value();
      }
    }
    throw;
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

  if (node->DynCast<EmptyStmt>()) {
    return;
  }
  if (auto *decl_stmt = node->DynCast<DeclStmt>()) {
    for (const auto &decl : decl_stmt->decls) {
      EmitStmt(decl.get());
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
  if (auto *while_stmt = node->DynCast<WhileStmt>()) {
    size_t label_id = context.counter();
    auto *start_block = llvm::BasicBlock::Create(
        context.LLVMContext(), std::format("while.start.{}", label_id),
        context.current_function);
    auto *body_block = llvm::BasicBlock::Create(
        context.LLVMContext(), std::format("while.body.{}", label_id),
        context.current_function);
    auto *end_block = llvm::BasicBlock::Create(
        context.LLVMContext(), std::format("while.end.{}", label_id),
        context.current_function);

    GenContextLoopLayerGuard loop_guard(context);
    context.CurrLoopLayer().continue_block = start_block;
    context.CurrLoopLayer().break_block = end_block;

    builder.CreateBr(start_block);
    builder.SetInsertPoint(start_block);
    RValue condition = EmitRValue(while_stmt->condition.get());
    builder.CreateCondBr(condition, body_block, end_block);
    builder.SetInsertPoint(body_block);
    {
      GenContextLayerGuard guard(context);
      EmitStmt(while_stmt->body.get());
    }
    builder.CreateBr(start_block);
    builder.SetInsertPoint(end_block);
    return;
  }
  if (auto *for_stmt = node->DynCast<ForStmt>()) {
    GenContextLayerGuard guard(context);
    size_t label_id = context.counter();
    if (for_stmt->init) {
      EmitStmt(for_stmt->init.get());
    }
    auto *start_block = llvm::BasicBlock::Create(
        context.LLVMContext(), std::format("for.start.{}", label_id),
        context.current_function);
    auto *body_block = llvm::BasicBlock::Create(
        context.LLVMContext(), std::format("for.body.{}", label_id),
        context.current_function);
    auto *inc_block = llvm::BasicBlock::Create(
        context.LLVMContext(), std::format("for.inc.{}", label_id),
        context.current_function);
    auto *end_block = llvm::BasicBlock::Create(
        context.LLVMContext(), std::format("for.end.{}", label_id),
        context.current_function);

    GenContextLoopLayerGuard loop_guard(context);
    context.CurrLoopLayer().continue_block = inc_block;
    context.CurrLoopLayer().break_block = end_block;

    builder.CreateBr(start_block);
    builder.SetInsertPoint(start_block);
    if (for_stmt->condition) {
      RValue condition = EmitRValue(for_stmt->condition.get());
      builder.CreateCondBr(condition, body_block, end_block);
    } else {
      builder.CreateBr(body_block);
    }
    builder.SetInsertPoint(body_block);
    {
      GenContextLayerGuard guard(context);
      EmitStmt(for_stmt->body.get());
      builder.CreateBr(inc_block);
      builder.SetInsertPoint(inc_block);
      if (for_stmt->increment) {
        EmitStmt(for_stmt->increment.get());
      }
    }
    builder.CreateBr(start_block);
    builder.SetInsertPoint(end_block);
    return;
  }
  if (node->DynCast<BreakStmt>()) {
    ASSERT(context.loop_layers.size(), "Expect break in a loop statement");
    builder.CreateBr(context.CurrLoopLayer().break_block);
    return;
  }
  if (node->DynCast<ContinueStmt>()) {
    ASSERT(context.loop_layers.size(), "Expect continue in a loop statement");
    builder.CreateBr(context.CurrLoopLayer().continue_block);
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
  if (node->DynCast<TypedefDecl>()) {
    return;
  }
  EmitRValue(node);
}
