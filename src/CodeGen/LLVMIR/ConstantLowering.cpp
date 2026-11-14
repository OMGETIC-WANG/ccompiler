#include "ConstantLowering.h"

#include "../../AST/AST.h"
#include "../../Exception.h"

#include <llvm/IR/Constants.h>

llvm::Constant *LowerNumericConstant(TypeRef type, ASTNode *node,
                                     GenContext &context) {
  auto *expr = dynamic_cast<Expr *>(node);
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
      return llvm::ConstantFP::get(llvm_type, std::get<long double>(value));
    }
    if (std::holds_alternative<int64_t>(value)) {
      return llvm::ConstantFP::get(
          llvm_type, static_cast<long double>(std::get<int64_t>(value)));
    }
    return llvm::ConstantFP::get(
        llvm_type, static_cast<long double>(std::get<uint64_t>(value)));
  }
  RAISE_ERROR("Numeric constant cannot be lowered to the target type");
}
