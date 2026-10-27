#pragma once

#include "../../AST/Type.h"
#include "../../Util/StringVariant.h"

inline StringVariant GetTypeIRName(TypeRef type) {
  switch (type->GetType()) {
  case TypeBaseType::PointerType:
  case TypeBaseType::FunctionType:
    return "ptr";
  case TypeBaseType::ArrayType: {
    const auto *array_type = type->As<ArrayType>();
    if (array_type->size.has_value()) {
      return std::format("[{} x {}]", array_type->size.value(),
                         GetTypeIRName(array_type->element_type));
    }
    return "ptr";
  }
  case TypeBaseType::BuiltinType:
    return type->As<BuiltinType>()->IRName();
  case TypeBaseType::StructType:
    return type->As<StructType>()->IRName();
  case TypeBaseType::EnumType: {
    const auto *enum_type = type->As<EnumType>();
    if (enum_type->underlying_type) {
      return GetTypeIRName(enum_type->underlying_type);
    }
    return "i32";
  }
  default:
    RAISE_ERROR("Unexpected TypeBaseType");
  }
}
