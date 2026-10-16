#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "../Util/EnumRefl.h"

#include "Declaration.h"

// clang-format off
#define TYPE_BASE_TYPE_VALUES(x)\
  x(RawType)\
  x(FunctionType)\
  x(PointerType)\
  x(ArrayType)
// clang-format on

MAKE_ENUM(TypeBaseType, TYPE_BASE_TYPE_VALUES)
REGISTER_ENUM(TypeBaseType, TYPE_BASE_TYPE_VALUES)

class TypeBase {
public:
  virtual ~TypeBase() = default;

  virtual TypeBaseType GetType() const = 0;

  void Print(std::ostream &out) const;

  std::string_view identifier;
};

#define TypeNodeImpl(type_enum)                                                \
public:                                                                        \
  TypeBaseType GetType() const override { return TypeBaseType::type_enum; }    \
  constexpr static TypeBaseType __type = TypeBaseType::type_enum

class RawType : public TypeBase {
  TypeNodeImpl(RawType);

public:
  TypeSpecifier specifier;
};

class FunctionType : public TypeBase {
  TypeNodeImpl(FunctionType);

public:
  std::unique_ptr<TypeBase> return_type;
  std::vector<std::unique_ptr<TypeBase>> parameters;
};

class PointerType : public TypeBase {
  TypeNodeImpl(PointerType);

public:
  std::unique_ptr<TypeBase> pointee_type;
};

class ArrayType : public TypeBase {
  TypeNodeImpl(ArrayType);

public:
  std::unique_ptr<TypeBase> element_type;
  size_t size = (size_t)-1; // default undecleared
};

std::vector<std::unique_ptr<TypeBase>> ParseType(const Declaration *decl);
