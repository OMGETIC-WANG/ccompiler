#include "ConstantLowering.h"

#include "../../AST/AST.h"
#include "../../AST/Operations.h"
#include "../../Exception.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

#include <array>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <system_error>

namespace {

bool IsIntegerOrEnumType(TypeRef type) {
  return IsIntegerType(type) || type->Is<EnumType>();
}

bool IsUnsignedIntegerType(TypeRef type) {
  return HasTypeTag(type, TypeTag::Unsigned) ||
         (type->Is<BuiltinType>() &&
          type->As<BuiltinType>()->Kind() == BuiltinKind::I1);
}

llvm::Constant *GetAddressConstant(const ValueDecl *decl, GenContext &context) {
  const LValue *value = context.FindLValue(decl);
  ASSERT(value != nullptr && value->address != nullptr,
         "Constant expression references an ungenerated declaration");
  auto *constant = llvm::dyn_cast<llvm::Constant>(value->address);
  ASSERT(constant != nullptr,
         "Constant expression references a non-constant address");
  return constant;
}

llvm::Constant *LowerIntegerToPointerConstant(llvm::Constant *source,
                                              TypeRef source_type,
                                              TypeRef target_type,
                                              GenContext &context) {
  llvm::IRBuilder<> builder(context.LLVMContext());
  auto *pointer_integer_type =
      builder.getIntPtrTy(context.Module().getDataLayout());
  auto *source_integer_type = llvm::cast<llvm::IntegerType>(source->getType());
  const unsigned source_width = source_integer_type->getBitWidth();
  const unsigned pointer_width = pointer_integer_type->getBitWidth();
  llvm::Value *normalized = source;
  if (source_width > pointer_width) {
    normalized = builder.CreateTrunc(normalized, pointer_integer_type);
  } else if (source_width < pointer_width) {
    normalized = IsUnsignedIntegerType(source_type)
                     ? builder.CreateZExt(normalized, pointer_integer_type)
                     : builder.CreateSExt(normalized, pointer_integer_type);
  }
  return llvm::cast<llvm::Constant>(
      builder.CreateIntToPtr(normalized, context.LLVMType(target_type)));
}

llvm::Constant *LowerPointerToIntegerConstant(llvm::Constant *source,
                                              TypeRef target_type,
                                              GenContext &context) {
  if (target_type->Is<BuiltinType>() &&
      target_type->As<BuiltinType>()->Kind() == BuiltinKind::I1) {
    auto *null_pointer = llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(source->getType()));
    llvm::IRBuilder<> builder(context.LLVMContext());
    return llvm::cast<llvm::Constant>(
        builder.CreateICmpNE(source, null_pointer));
  }
  return llvm::ConstantExpr::getPtrToInt(source, context.LLVMType(target_type));
}

llvm::Constant *LowerCastConstant(TypeRef target_type, TypeRef source_type,
                                  ASTNode *operand, ASTNode *cast_node,
                                  GenContext &context) {
  if (target_type->Is<PointerType>()) {
    if (source_type->Is<PointerType>() || source_type->Is<ArrayType>() ||
        source_type->Is<FunctionType>()) {
      return LowerConstantExpression(source_type, operand, context);
    }
    if (IsIntegerOrEnumType(source_type)) {
      auto *source = LowerConstantExpression(source_type, operand, context);
      return LowerIntegerToPointerConstant(source, source_type, target_type,
                                           context);
    }
  }

  if (IsIntegerOrEnumType(target_type) &&
      (source_type->Is<PointerType>() || source_type->Is<ArrayType>() ||
       source_type->Is<FunctionType>())) {
    auto *source = LowerConstantExpression(source_type, operand, context);
    return LowerPointerToIntegerConstant(source, target_type, context);
  }

  if ((IsNumericType(target_type) || target_type->Is<EnumType>()) &&
      (IsNumericType(source_type) || source_type->Is<EnumType>())) {
    return LowerNumericConstant(target_type, cast_node, context);
  }

  RAISE_ERROR("Unsupported constant cast from ", source_type, " to ",
              target_type);
}

} // namespace

llvm::Constant *LowerFloatingConstant(llvm::Type *llvm_type,
                                      long double value) {
  std::array<char, 128> buffer;
  auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                              value, std::chars_format::hex);
  ASSERT(result.ec == std::errc{} &&
             result.ptr != buffer.data() + buffer.size(),
         "Failed to format a long double constant");
  std::string_view formatted(buffer.data(), result.ptr - buffer.data());
  if (!std::isfinite(value)) {
    return llvm::ConstantFP::get(llvm_type, formatted);
  }
  std::string hexadecimal;
  hexadecimal.reserve(formatted.size() + 2);
  if (formatted.front() == '-') {
    hexadecimal.append("-0x");
    formatted.remove_prefix(1);
  } else {
    hexadecimal.append("0x");
  }
  hexadecimal.append(formatted);
  return llvm::ConstantFP::get(llvm_type, hexadecimal);
}

llvm::Constant *LowerNumericConstant(TypeRef type, ASTNode *node,
                                     GenContext &context) {
  auto *expr = node ? node->DynCast<Expr>() : nullptr;
  ASSERT(expr != nullptr && expr->constant_value.has_value(),
         "Sema did not cache a required constant value");

  const NumericValue &value = *expr->constant_value;
  llvm::Type *llvm_type = context.LLVMType(type);
  if (llvm_type->isIntegerTy()) {
    if (std::holds_alternative<int64_t>(value)) {
      return llvm::ConstantInt::get(llvm_type, std::get<int64_t>(value), true);
    }
    if (std::holds_alternative<uint64_t>(value)) {
      return llvm::ConstantInt::get(llvm_type, std::get<uint64_t>(value),
                                    false);
    }
    RAISE_ERROR("Floating point constant used for an integer initializer");
  }
  if (llvm_type->isFloatingPointTy()) {
    if (std::holds_alternative<long double>(value)) {
      return LowerFloatingConstant(llvm_type, std::get<long double>(value));
    }
    if (std::holds_alternative<int64_t>(value)) {
      return LowerFloatingConstant(
          llvm_type, static_cast<long double>(std::get<int64_t>(value)));
    }
    return LowerFloatingConstant(
        llvm_type, static_cast<long double>(std::get<uint64_t>(value)));
  }
  RAISE_ERROR("Numeric constant cannot be lowered to the target type");
}

llvm::Constant *LowerConstantExpression(TypeRef type, ASTNode *node,
                                        GenContext &context) {
  auto *expr = node ? node->DynCast<Expr>() : nullptr;
  ASSERT(expr != nullptr, "Constant expression has no expression node");

  if (auto *implicit_cast = expr->DynCast<ImplicitCast>()) {
    auto *operand = implicit_cast->operand->DynCast<Expr>();
    ASSERT(operand != nullptr, "Implicit cast has no expression operand");
    return LowerCastConstant(implicit_cast->target_type, operand->type,
                             implicit_cast->operand.get(), implicit_cast,
                             context);
  }
  if (auto *type_cast = expr->DynCast<TypeCast>()) {
    auto *operand = type_cast->operand->DynCast<Expr>();
    ASSERT(operand != nullptr, "Type cast has no expression operand");
    return LowerCastConstant(type_cast->target_type, operand->type,
                             type_cast->operand.get(), type_cast, context);
  }
  if (auto *unary = expr->DynCast<UnaryOp>();
      unary && unary->op_type == UnaryOpType::GetAddress) {
    auto *var_ref = unary->operand->DynCast<VarRef>();
    ASSERT(var_ref != nullptr,
           "Only declaration addresses are constant addresses");
    return GetAddressConstant(var_ref->decl, context);
  }
  if (auto *var_ref = expr->DynCast<VarRef>()) {
    if (var_ref->decl->DynCast<FunctionDecl>() ||
        (var_ref->decl->DynCast<VarDecl>() &&
         var_ref->decl->As<VarDecl>()->type->Is<ArrayType>())) {
      return GetAddressConstant(var_ref->decl, context);
    }
  }
  if (auto *literal = expr->DynCast<LiteralString>()) {
    return llvm::cast<llvm::Constant>(context.CreateStringConstant(
        literal->value.view(), literal->value.view().size() + 1));
  }
  if (expr->constant_value.has_value()) {
    return LowerNumericConstant(type, expr, context);
  }

  RAISE_ERROR("Unsupported constant expression: ", expr->GetType());
}
