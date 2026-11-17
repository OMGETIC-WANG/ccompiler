#include "ConstantLowering.h"

#include "../../AST/AST.h"
#include "../../Exception.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Constants.h>

#include <array>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <system_error>

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
