#include "GenExpr.h"

#include "../../AST/AST.h"
#include "../../AST/Intrinsic.h"
#include "../../AST/Operations.h"
#include "../../AST/Type.h"
#include "../../Util/Diagnostic.h"
#include "ConstantLowering.h"
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

static const EnumElemDecl *AsEnumElemDecl(const ValueDecl *decl) {
  if (!decl || decl->GetType() != ASTNodeType::EnumElemDecl) {
    return nullptr;
  }
  return decl->As<EnumElemDecl>();
}

static RValue CreateIndex(size_t index, GenContext &context) {
  return RValue{
      context.Types().getInt(),
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
               "Sema accepted an oversized array initializer");

        RValue array_base = LoadAsRValue(value, self);
        for (size_t i = 0; i < array_size; ++i) {
          if (i < array_init->initializers.size()) {
            GenVarInitializer(self.builder.CreateSubscript(
                                  array_base, CreateIndex(i, self.context),
                                  array_init->location),
                              array_type->element_type,
                              array_init->initializers[i].get(), self);
          } else {
            self.builder.CreateStore(
                CreateNullValue(array_type->element_type, self.context),
                self.builder.CreateSubscript(array_base,
                                             CreateIndex(i, self.context),
                                             array_init->location));
          }
        }
      }
      if (IsLiteralStringInit(array_type, initializer)) {
        auto *literal_str =
            initializer->As<ValueInitializer>()->value->As<LiteralString>();
        ASSERT(array_type->size.value() >= literal_str->value.view().size() + 1,
               "Sema accepted an oversized string initializer");
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
          ASSERT(field.has_value(), "Sema accepted an unknown field");
          size_t field_index = 0;
          bool found = false;
          for (size_t i = 0; i < struct_type->fields.size(); ++i) {
            if (struct_type->fields[i].name == name) {
              field_index = i;
              found = true;
              break;
            }
          }
          ASSERT(found, "Sema accepted an unknown field");
          fields_inited[field_index] = true;
          GenVarInitializer(self.builder.GetElementPtr(value, field.value(),
                                                       field_init->location),
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
               "Sema accepted an oversized struct initializer");
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
      if (auto *value_init = initializer->DynCast<ValueInitializer>()) {
        RValue val = self.EmitRValue(value_init->value.get());
        self.builder.CreateStore(val, value);
      }
      return;
    }

    if (auto *val_init = initializer->DynCast<ValueInitializer>()) {
      RValue val = self.EmitRValue(val_init->value.get());
      self.builder.CreateStore(val, value);
      return;
    }

    RAISE_ERROR("Sema accepted an unsupported initializer");
  }

  static LValue GenVarDecl(VarDecl *decl, ExprEmitter &self) {
    ASSERT(decl->storage_class == StorageClass::None,
           "Sema accepted unsupported local static storage");
    TypeRef type = decl->type;
    LValue value = self.builder.CreateAlloca(type);
    self.context.AddLValue(decl, value);
    if (decl->initializer) {
      GenVarInitializer(value, type, decl->initializer.get(), self);
    }
    return value;
  }

  static RValue LoadAsRValue(const LValue &value, ExprEmitter &self) {
    if (value.c_type->Is<ArrayType>() || value.c_type->Is<FunctionType>()) {
      return RValue{value.c_type, value.address};
    }
    return self.builder.CreateLoad(value.c_type, value);
  }

  static RValue EmitCast(TypeRef target_type, const RValue &value,
                         ExprEmitter &self) {
    if (target_type == value.c_type ||
        self.context.LLVMType(target_type) ==
            self.context.LLVMType(value.c_type)) {
      return RValue{target_type, value.ir_value};
    }
    if (target_type->Is<BuiltinType>() &&
        target_type->As<BuiltinType>()->Kind() == BuiltinKind::I1) {
      ASSERT(IsNumericType(value.c_type) || value.c_type->Is<PointerType>() ||
                 value.c_type->Is<EnumType>(),
             "Sema approved a non-scalar boolean conversion");
      return self.builder.CreateBoolCond(value);
    }
    if (target_type->Is<PointerType>() &&
        (value.c_type->Is<PointerType>() || value.c_type->Is<ArrayType>() ||
         value.c_type->Is<FunctionType>())) {
      return RValue{target_type, value.ir_value};
    }
    if (IsNumericType(target_type) && IsNumericType(value.c_type)) {
      return self.builder.CreateNumericCast(value, target_type);
    }
    RAISE_ERROR("Sema approved an unsupported implicit conversion");
  }

  enum class EvalAndOr { And, Or };

  template <EvalAndOr eval_kind>
  static RValue EvalAndOr(ASTNode *left, ASTNode *right, ExprEmitter &self) {
    TypeRef bool_type =
        ApplyTypeTag(self.context.Types().getI1(), TypeTag::Unsigned);
    RValue left_value = self.EmitRValue(left);
    ASSERT(left_value.c_type == bool_type,
           "Sema left a non-boolean logical left operand");
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
    ASSERT(right_value.c_type == bool_type,
           "Sema left a non-boolean logical right operand");
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

  static RValue EvalBinaryOp(BinaryOpType op_type, RValue left, RValue right,
                             SourceLocation location, ExprEmitter &self) {
    if (op_type == BinaryOpType::BitAnd || op_type == BinaryOpType::BitOr ||
        op_type == BinaryOpType::BitXor) {
      ASSERT(IsIntegerType(left.c_type) && IsIntegerType(right.c_type),
             "Sema accepted non-integer bitwise operands");
      return self.builder.CreateBinaryOp(op_type, left, right, location);
    }

    if (op_type == BinaryOpType::LShift || op_type == BinaryOpType::RShift) {
      ASSERT(IsIntegerType(left.c_type) && IsIntegerType(right.c_type),
             "Sema accepted non-integer shift operands");
      return self.builder.CreateBinaryOp(op_type, left, right, location);
    }

    if (left.c_type->Is<PointerType>()) {
      ASSERT(IsIntegerType(right.c_type),
             "Sema accepted a non-integer pointer offset");
      ASSERT(op_type == BinaryOpType::Add || op_type == BinaryOpType::Sub,
             "Sema accepted an unsupported pointer operation");
      if (op_type == BinaryOpType::Sub) {
        right = self.builder.CreateInv(right);
      }
      LValue subscripted = self.builder.CreateSubscript(left, right, location);
      return RValue{left.c_type, subscripted.address};
    }

    return self.builder.CreateBinaryOp(op_type, left, right, location);
  }

  static LValue GetUnionFieldLValue(const LValue &value,
                                    std::string_view field_name,
                                    SourceLocation location,
                                    ExprEmitter &self) {
    ASSERT(value.c_type->Is<UnionType>());
    auto union_type = value.c_type->As<UnionType>();
    auto field_ = union_type->GetField(field_name);
    ASSERT(field_.has_value(), "Sema accepted an unknown union field");
    auto &field = field_.value();
    return LValue{field.type, value.address};
  }

  static RValue GetUnionFieldRValue(const RValue &value,
                                    std::string_view field_name,
                                    SourceLocation location,
                                    ExprEmitter &self) {
    LValue union_lval = self.builder.CreateAlloca(value.c_type);
    self.builder.CreateStore(value, union_lval);
    LValue field_lval =
        GetUnionFieldLValue(union_lval, field_name, location, self);
    return LoadAsRValue(field_lval, self);
  }
};

LValue ExprEmitter::EmitLValue(ASTNode *node) {
  if (auto *var_ref = node->DynCast<VarRef>()) {
    if (auto *value = context.FindLValue(var_ref->decl)) {
      return *value;
    }
    if (AsEnumElemDecl(var_ref->decl)) {
      return {};
    }
    RAISE_ERROR("Sema accepted a reference without a generated value: ",
                var_ref->decl->name);
  }
  if (auto *var_decl = node->DynCast<VarDecl>()) {
    return detail::GenVarDecl(var_decl, *this);
  }
  if (auto *get_field = node->DynCast<GetField>()) {
    LValue operand = EmitLValue(get_field->operand.get());
    if (!operand.address) {
      return {};
    }
    if (operand.c_type->Is<StructType>()) {
      return builder.GetElementPtr(operand, get_field->field_name,
                                   get_field->location);
    } else if (operand.c_type->Is<UnionType>()) {
      return detail::GetUnionFieldLValue(operand, get_field->field_name,
                                         get_field->location, *this);
    } else {
      RAISE_ERROR("Sema accepted field access on a non-aggregate");
    }
  }
  if (auto *ptr_get_field = node->DynCast<PtrGetField>()) {
    RValue ptr = EmitRValue(ptr_get_field->operand.get());
    if (auto ptr_t = ptr.c_type->DynCast<PointerType>()) {
      if (ptr_t->pointee_type->Is<StructType>()) {
        return builder.GetPtrElementPtr(ptr, ptr_get_field->field_name,
                                        ptr_get_field->location);
      } else if (ptr_t->pointee_type->Is<UnionType>()) {
        return detail::GetUnionFieldLValue(
            LValue{ptr_t->pointee_type, ptr.ir_value},
            ptr_get_field->field_name, ptr_get_field->location, *this);
      } else {
        RAISE_ERROR("Sema accepted pointer field access on a non-aggregate");
      }
    } else {
      RAISE_ERROR("Sema accepted pointer field access on a non-pointer");
    }
  }
  if (auto *binary = node->DynCast<BinaryOp>()) {
    if (binary->op_type == BinaryOpType::Subscript) {
      RValue array = EmitRValue(binary->left.get());
      RValue index = EmitRValue(binary->right.get());
      return builder.CreateSubscript(array, index, binary->location);
    }
    RAISE_ERROR("Sema accepted an unsupported lvalue binary operation");
  }
  if (auto *unary = node->DynCast<UnaryOp>()) {
    if (unary->op_type == UnaryOpType::Deref) {
      RValue ptr = EmitRValue(unary->operand.get());
      ASSERT(ptr.c_type->Is<PointerType>(),
             "Sema accepted dereference of a non-pointer");
      return LValue{ptr.c_type->As<PointerType>()->pointee_type, ptr.ir_value};
    }
  }
  return {};
}

RValue ExprEmitter::EmitRValue(ASTNode *node) {
  if (auto *var_ref = node->DynCast<VarRef>()) {
    if (AsEnumElemDecl(var_ref->decl)) {
      if (var_ref->constant_value.has_value()) {
        llvm::ConstantInt *value;
        if (std::holds_alternative<int64_t>(*var_ref->constant_value)) {
          value = llvm::cast<llvm::ConstantInt>(llvm::ConstantInt::get(
              context.LLVMType(var_ref->type),
              std::get<int64_t>(*var_ref->constant_value),
              !HasTypeTag(var_ref->type, TypeTag::Unsigned)));
        } else if (std::holds_alternative<uint64_t>(*var_ref->constant_value)) {
          value = llvm::cast<llvm::ConstantInt>(llvm::ConstantInt::get(
              context.LLVMType(var_ref->type),
              std::get<uint64_t>(*var_ref->constant_value),
              !HasTypeTag(var_ref->type, TypeTag::Unsigned)));
        } else {
          RAISE_ERROR("Enum constant has a floating point value");
        }
        return RValue{var_ref->type, value};
      }
      RAISE_ERROR("Sema did not cache an enum constant value");
    }
    if (auto *value = context.FindLValue(var_ref->decl)) {
      return detail::LoadAsRValue(*value, *this);
    }
    RAISE_ERROR("Sema accepted a reference without a generated value: ",
                var_ref->decl->name);
  }
  if (auto *literal_num = node->DynCast<LiteralNum>()) {
    if (std::holds_alternative<int64_t>(literal_num->value)) {
      return RValue{
          literal_num->type,
          llvm::ConstantInt::get(context.LLVMType(literal_num->type),
                                 std::get<int64_t>(literal_num->value), true)};
    }
    if (std::holds_alternative<uint64_t>(literal_num->value)) {
      return RValue{literal_num->type,
                    llvm::ConstantInt::get(
                        context.LLVMType(literal_num->type),
                        std::get<uint64_t>(literal_num->value), false)};
    }
    if (std::holds_alternative<long double>(literal_num->value)) {
      return RValue{
          literal_num->type,
          llvm::ConstantFP::get(context.LLVMType(literal_num->type),
                                std::get<long double>(literal_num->value))};
    }
  }
  if (auto *literal_str = node->DynCast<LiteralString>()) {
    TypeRef type = literal_str->type;
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
      if (operand_lhs.c_type->Is<StructType>()) {
        LValue field_lhs = builder.GetElementPtr(
            operand_lhs, get_field->field_name, get_field->location);
        return detail::LoadAsRValue(field_lhs, *this);
      } else if (operand_lhs.c_type->Is<UnionType>()) {
        LValue field_lhs = detail::GetUnionFieldLValue(
            operand_lhs, get_field->field_name, get_field->location, *this);
        return detail::LoadAsRValue(field_lhs, *this);
      } else {
        RAISE_ERROR("Sema accepted field access on a non-aggregate");
      }
    }
    RValue operand_rhs = EmitRValue(get_field->operand.get());
    if (operand_rhs.c_type->Is<StructType>()) {
      return builder.CreateExtractValue(operand_rhs, get_field->field_name,
                                        get_field->location);
    } else if (operand_rhs.c_type->Is<UnionType>()) {
      return detail::GetUnionFieldRValue(operand_rhs, get_field->field_name,
                                         get_field->location, *this);
    } else {
      RAISE_ERROR("Sema accepted field access on a non-aggregate");
    }
  }
  if (auto *bin_expr = node->DynCast<BinaryOp>()) {
    switch (bin_expr->op_type) {
    case BinaryOpType::Assignment: {
      LValue left_lhs = EmitLValue(bin_expr->left.get());
      ASSERT(left_lhs.address, "Sema accepted non-lvalue assignment");
      if (!bin_expr->compound_op) {
        RValue right = EmitRValue(bin_expr->right.get());
        builder.CreateStore(right, left_lhs);
        return RValue{bin_expr->type, right.ir_value};
      }

      RValue left = detail::LoadAsRValue(left_lhs, *this);
      if (bin_expr->left_value_cast_type) {
        left = detail::EmitCast(*bin_expr->left_value_cast_type, left, *this);
      } else {
        ASSERT(left.c_type == bin_expr->operation_type,
               "Sema omitted a compound-assignment input conversion");
      }
      RValue right = EmitRValue(bin_expr->right.get());
      ASSERT(bin_expr->operation_type != nullptr,
             "Sema omitted a compound-assignment operation type");
      RValue res = detail::EvalBinaryOp(*bin_expr->compound_op, left, right,
                                        bin_expr->location, *this);
      if (bin_expr->result_cast_type) {
        res = detail::EmitCast(*bin_expr->result_cast_type, res, *this);
      } else {
        ASSERT(res.c_type == bin_expr->type,
               "Sema omitted a compound-assignment result conversion");
      }
      builder.CreateStore(res, left_lhs);
      return res;
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
      RValue result = detail::EvalBinaryOp(bin_expr->op_type, left, right,
                                           bin_expr->location, *this);
      ASSERT(result.c_type == bin_expr->type,
             "Sema left a binary expression with a mismatched result type");
      return result;
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
    return builder.CreateFuncInvoke(function, AsSpan(args),
                                    func_invoke->location);
  }
  if (auto *intrinsic_call = node->DynCast<IntrinsicCall>()) {
    ASSERT(intrinsic_call->desc != nullptr,
           "Intrinsic call is missing its descriptor");
    ASSERT(intrinsic_call->desc->evaluator != nullptr,
           "Intrinsic has no evaluator: ", intrinsic_call->desc->name);
    return intrinsic_call->desc->evaluator(intrinsic_call->args, this);
  }
  if (auto *implicit_cast = node->DynCast<ImplicitCast>()) {
    RValue operand = EmitRValue(implicit_cast->operand.get());
    return detail::EmitCast(implicit_cast->target_type, operand, *this);
  }
  if (auto *type_cast = node->DynCast<TypeCast>()) {
    RValue operand = EmitRValue(type_cast->operand.get());
    return detail::EmitCast(type_cast->target_type, operand, *this);
  }
  if (auto *unary_expr = node->DynCast<UnaryOp>()) {
    switch (unary_expr->op_type) {
    case UnaryOpType::GetAddress: {
      LValue operand = EmitLValue(unary_expr->operand.get());
      ASSERT(operand.address, "Sema accepted address-of on a non-lvalue");
      return RValue{context.Types().Pointer(operand.c_type), operand.address};
    }
    case UnaryOpType::UnaryPlus: {
      RValue operand = EmitRValue(unary_expr->operand.get());
      ASSERT(operand.c_type == unary_expr->type,
             "Sema left unary plus with a mismatched result type");
      return operand;
    }
    case UnaryOpType::UnaryMinus: {
      RValue operand = EmitRValue(unary_expr->operand.get());
      ASSERT(operand.c_type == unary_expr->type,
             "Sema left unary minus with a mismatched result type");
      return builder.CreateInv(operand);
    }
    case UnaryOpType::BitNot: {
      RValue operand = EmitRValue(unary_expr->operand.get());
      ASSERT(operand.c_type == unary_expr->type,
             "Sema left bitwise NOT with a mismatched result type");
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
      ASSERT(operand.address,
             "Sema accepted increment/decrement on a non-lvalue");
      RValue old_value = builder.CreateLoad(operand.c_type, operand);

      if (operand.c_type->Is<PointerType>()) {
        RValue one{context.Types().getInt(),
                   llvm::ConstantInt::get(
                       llvm::Type::getInt32Ty(context.LLVMContext()), 1)};
        LValue subscripted = builder.CreateSubscript(
            old_value, is_inc ? one : builder.CreateInv(one),
            unary_expr->location);
        RValue updated{operand.c_type, subscripted.address};
        builder.CreateStore(updated, operand);
        return is_before_use ? updated : old_value;
      } else {
        RValue one{operand.c_type, nullptr};
        if (IsFloatingType(operand.c_type)) {
          one.ir_value =
              llvm::ConstantFP::get(context.LLVMType(operand.c_type), 1.0);
        } else {
          one.ir_value =
              llvm::ConstantInt::get(context.LLVMType(operand.c_type), 1);
        }
        RValue updated = builder.CreateBinaryOp(
            is_inc ? BinaryOpType::Add : BinaryOpType::Sub, old_value, one,
            unary_expr->location);
        builder.CreateStore(updated, operand);
        return is_before_use ? updated : old_value;
      }
    }
    case UnaryOpType::Not: {
      RValue operand = EmitRValue(unary_expr->operand.get());
      ASSERT(operand.c_type == unary_expr->type,
             "Sema left logical NOT with a mismatched operand type");
      return builder.CreateBitNot(operand);
    }
    default:
      break;
    }
  }
  if (auto cond_expr = node->DynCast<ConditionalExpr>()) {
    size_t label_id = context.counter();
    llvm::BasicBlock *true_block = llvm::BasicBlock::Create(
        context.LLVMContext(), std::format("cond_expr.true.{}", label_id),
        context.current_function);
    llvm::BasicBlock *false_block = llvm::BasicBlock::Create(
        context.LLVMContext(), std::format("cond_expr.false.{}", label_id),
        context.current_function);
    llvm::BasicBlock *end_block = llvm::BasicBlock::Create(
        context.LLVMContext(), std::format("cond_expr.end.{}", label_id),
        context.current_function);

    RValue cond = EmitRValue(cond_expr->condition.get());
    builder.CreateCondBr(cond, true_block, false_block);

    builder.SetInsertPoint(true_block);
    RValue true_value = EmitRValue(cond_expr->true_expr.get());

    builder.SetInsertPoint(false_block);
    RValue false_value = EmitRValue(cond_expr->false_expr.get());

    builder.SetInsertPoint(true_block);
    builder.CreateBr(end_block);

    builder.SetInsertPoint(false_block);
    builder.CreateBr(end_block);

    builder.SetInsertPoint(end_block);
    return builder.CreatePhi(true_value, true_block, false_value, false_block);
  }

  LValue lhs = EmitLValue(node);
  if (lhs.address) {
    return detail::LoadAsRValue(lhs, *this);
  }
  RAISE_ERROR("Sema accepted an unsupported expression node: ",
              node->GetType());
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

static bool IsValidSwitchValue(const TypeBase *type) {
  if (auto builtin = type->DynCast<BuiltinType>()) {
    BuiltinKind kind = builtin->Kind();
    return IsIntegerKind(kind) || kind == BuiltinKind::Char;
  }
  return type->Is<EnumType>();
}

void ExprEmitter::EmitStmt(ASTNode *node) {
  if (builder.IsTerminated()) {
    if (auto *label_stmt = node->DynCast<LabelStmt>()) {
      builder.SetInsertPoint(GetOrCreateLabelBlock(label_stmt->name));
      return;
    }
    if (auto *compound = node->DynCast<CompoundStmt>()) {
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
      EmitStmt(while_stmt->body.get());
    }
    builder.CreateBr(start_block);
    builder.SetInsertPoint(end_block);
    return;
  }
  if (auto *dowhile_stmt = node->DynCast<DoWhileStmt>()) {
    size_t label_id = context.counter();
    auto *body_block = llvm::BasicBlock::Create(
        context.LLVMContext(), std::format("dowhile.body.{}", label_id),
        context.current_function);
    auto *cond_block = llvm::BasicBlock::Create(
        context.LLVMContext(), std::format("dowhile.cond.{}", label_id),
        context.current_function);
    auto *end_block = llvm::BasicBlock::Create(
        context.LLVMContext(), std::format("dowhile.end.{}", label_id),
        context.current_function);

    GenContextLoopLayerGuard loop_guard(context);
    context.CurrLoopLayer().continue_block = cond_block;
    context.CurrLoopLayer().break_block = end_block;

    builder.CreateBr(body_block);
    builder.SetInsertPoint(body_block);
    {
      EmitStmt(dowhile_stmt->body.get());
    }
    builder.CreateBr(cond_block);
    builder.SetInsertPoint(cond_block);
    RValue condition = EmitRValue(dowhile_stmt->condition.get());
    builder.CreateCondBr(condition, body_block, end_block);
    builder.SetInsertPoint(end_block);
    return;
  }
  if (auto *for_stmt = node->DynCast<ForStmt>()) {
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
  if (auto *switch_stmt = node->DynCast<SwitchStmt>()) {
    size_t label_id = context.counter();
    auto curr_block = builder.GetInsertBlock();

    RValue switched_val = EmitRValue(switch_stmt->value.get());
    ASSERT(IsValidSwitchValue(switched_val.c_type.get()),
           "Sema accepted an invalid switch controlling expression");

    llvm::BasicBlock *default_block;
    llvm::BasicBlock *end_block = llvm::BasicBlock::Create(
        context.LLVMContext(), std::format("switch.end.{}", label_id),
        context.current_function);

    DefaultStmt *default_case = switch_stmt->DefaultBlock();
    if (default_case) {
      default_block = llvm::BasicBlock::Create(
          context.LLVMContext(), std::format("switch.default.{}", label_id),
          context.current_function);
      builder.SetInsertPoint(default_block);
      for (const auto &statement : default_case->body) {
        EmitStmt(statement.get());
      }
      builder.CreateBr(end_block);
    } else {
      default_block = end_block;
    }

    GenContextLoopLayerGuard loop_guard(context);
    context.CurrLoopLayer().break_block = end_block;

    std::vector<std::pair<llvm::ConstantInt *, llvm::BasicBlock *>> case_blocks;
    case_blocks.reserve(switch_stmt->cases.size() - (default_case ? 1 : 0));
    unsigned int num_cases = 0;
    for (const auto &stmt : switch_stmt->cases) {
      if (stmt->Is<DefaultStmt>()) {
        continue;
      }
      auto case_stmt = stmt->As<CaseStmt>();

      llvm::BasicBlock *body = llvm::BasicBlock::Create(
          context.LLVMContext(),
          std::format("switch.case.{}.{}", label_id, num_cases++),
          context.current_function);
      builder.SetInsertPoint(body);
      for (const auto &statement : case_stmt->body) {
        EmitStmt(statement.get());
      }
      builder.CreateBr(default_block);

      for (const auto &val : case_stmt->case_vals) {
        auto *case_expr = dynamic_cast<Expr *>(val.get());
        ASSERT(case_expr && case_expr->type == switched_val.c_type,
               "Sema left a switch case value with the wrong type");
        auto *folded =
            LowerNumericConstant(case_expr->type, val.get(), context);
        case_blocks.push_back({llvm::cast<llvm::ConstantInt>(folded), body});
      }
    }
    builder.SetInsertPoint(curr_block);
    builder.CreateSwitch(switched_val, case_blocks, default_block, num_cases);
    builder.SetInsertPoint(end_block);
    return;
  }
  if (node->DynCast<BreakStmt>()) {
    ASSERT(!context.loop_layers.empty(),
           "Sema accepted break outside a loop or switch");
    builder.CreateBr(context.CurrLoopLayer().break_block);
    return;
  }
  if (node->DynCast<ContinueStmt>()) {
    ASSERT(!context.loop_layers.empty(),
           "Sema accepted continue outside a loop");
    builder.CreateBr(context.CurrLoopLayer().continue_block);
    return;
  }
  if (auto *ret_stmt = node->DynCast<ReturnStmt>()) {
    if (ret_stmt->ret_value) {
      RValue ret = EmitRValue(ret_stmt->ret_value.get());
      builder.CreateRet(ret);
    } else {
      builder.CreateVoidRet();
    }
    return;
  }
  if (auto *compound = node->DynCast<CompoundStmt>()) {
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
