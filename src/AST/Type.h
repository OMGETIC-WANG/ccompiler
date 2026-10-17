#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "../Util/EnumRefl.h"
#include "../Util/Tree.h"

struct TypeSpecifier {
  std::string_view type;
  bool is_struct = false;
  bool is_typedef = false;
  bool is_builtin = false;
  bool is_enum = false;
  bool is_static = false;
};

// clang-format off
#define TYPE_BASE_TYPE_VALUES(x)\
  x(RawType)\
  x(FunctionType)\
  x(PointerType)\
  x(ArrayType)
// clang-format on

MAKE_ENUM(TypeBaseType, TYPE_BASE_TYPE_VALUES)
REGISTER_ENUM(TypeBaseType, TYPE_BASE_TYPE_VALUES)

class TypeBase : public TreeNode {
public:
  virtual ~TypeBase() = default;

  virtual TypeBaseType GetType() const = 0;

  std::string_view GetNodeTypeName() const override;

  std::string_view identifier;
};

#define TypeNodeImpl(type_enum)                                                \
public:                                                                        \
  TypeBaseType GetType() const override { return TypeBaseType::type_enum; }    \
  constexpr static TypeBaseType __type = TypeBaseType::type_enum

class RawType : public TypeBase {
  TypeNodeImpl(RawType);
  TreeNodePrintFields(identifier);

public:
  std::string ToString() const override { return std::string(specifier.type); }

  TypeSpecifier specifier;
};

class FunctionType : public TypeBase {
  TypeNodeImpl(FunctionType);
  TreeNodePrintFields(identifier, return_type, parameters);

public:
  std::unique_ptr<TypeBase> return_type;
  std::vector<std::unique_ptr<TypeBase>> parameters;
};

class PointerType : public TypeBase {
  TypeNodeImpl(PointerType);
  TreeNodePrintFields(identifier, pointee_type);

public:
  std::unique_ptr<TypeBase> pointee_type;
};

class ArrayType : public TypeBase {
  TypeNodeImpl(ArrayType);
  TreeNodePrintFields(identifier, element_type);

public:
  std::string ToString() const override {
    return size == (size_t)-1 ? "[]" : "[" + std::to_string(size) + "]";
  }

  std::unique_ptr<TypeBase> element_type;
  size_t size = (size_t)-1; // default undecleared
};

class Declaration;

std::vector<std::unique_ptr<TypeBase>> ParseType(const Declaration *decl);
