#pragma once

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "../Exception.h"
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
  x(BuiltinType)\
  x(StructType)\
  x(EnumType)\
  x(FunctionType)\
  x(ArrayType)\
  x(PointerType)\
  x(OpaqueType)
// clang-format on

MAKE_ENUM(TypeBaseType, TYPE_BASE_TYPE_VALUES)
REGISTER_ENUM(TypeBaseType, TYPE_BASE_TYPE_VALUES)

enum class OpaqueTypeKind {
  Named,
  StructTag,
  EnumTag,
};

class TypeBase : public TreeNode {
public:
  virtual ~TypeBase() = default;

  virtual TypeBaseType GetType() const = 0;

  std::string_view GetNodeTypeName() const override;

  size_t GetSize() const {
    if (_size == (size_t)-1) {
      _size = EvalSize();
    }
    return _size;
  }

  size_t GetAlign() const {
    if (_size == (size_t)-1) {
      _align = EvalAlign();
    }
    return _align;
  }

  template <std::derived_from<TypeBase> T> T *As() {
#ifdef ENABLE_AS_TYPE_CHECK
    if (GetType() != T::__type) {
      RAISE_ERROR("Invalid ASTNode type cast from ", GetType(), " to ",
                  T::__type);
    }
#endif
    return static_cast<T *>(this);
  }

  template <std::derived_from<TypeBase> T> T *DynCast() {
    if (GetType() != T::__type) {
      return nullptr;
    }
    return static_cast<T *>(this);
  }

protected:
  virtual size_t EvalSize() const = 0;

  virtual size_t EvalAlign() const = 0;

private:
  mutable size_t _size = (size_t)-1;  // unevaluated
  mutable size_t _align = (size_t)-1; // unevaluated
};

#define TypeNodeImpl(type_enum)                                                \
public:                                                                        \
  TypeBaseType GetType() const override { return TypeBaseType::type_enum; }    \
  constexpr static TypeBaseType __type = TypeBaseType::type_enum

class BuiltinType : public TypeBase {
  TypeNodeImpl(BuiltinType);
  TreeNodeFieldAsString(type_name);

public:
  BuiltinType(std::string_view name, std::string_view ir_name,
              std::optional<size_t> size, size_t align)
      : type_name(name), _ir_name(ir_name), _size(size), _align(align) {
    ASSERT(!type_name.empty(), "BuiltinType name must not be empty");
  }

  static std::unique_ptr<BuiltinType> Create(std::string_view name);

  std::string_view IRName() const { return _ir_name; }

  std::string_view type_name;

protected:
  size_t EvalSize() const override {
    if (!_size) {
      RAISE_ERROR("BuiltinType ", type_name, " does not have a byte size");
    }
    return *_size;
  }

  size_t EvalAlign() const override { return _align; }

private:
  std::string_view _ir_name;
  std::optional<size_t> _size;
  size_t _align;
};

class StructType : public TypeBase {
  TypeNodeImpl(StructType);
  TreeNodeFieldAsString(type_name);

public:
  explicit StructType(std::string_view name) : type_name(name) {
    ASSERT(!type_name.empty(), "StructType name must not be empty");
  }

  std::string_view type_name;

protected:
  size_t EvalSize() const override {
    RAISE_ERROR("StructType size requires an external struct layout");
  }

  size_t EvalAlign() const override {
    RAISE_ERROR("StructType alignment requires an external struct layout");
  }
};

class EnumType : public TypeBase {
  TypeNodeImpl(EnumType);
  TreeNodeFieldAsString(enum_name);

public:
  explicit EnumType(std::string_view name) : enum_name(name) {
    ASSERT(!enum_name.empty(), "EnumType name must not be empty");
  }

  std::string_view enum_name;

  BuiltinType *underlying_type = nullptr;

protected:
  size_t EvalSize() const override {
    RAISE_ERROR("EnumType size requires an external enum layout");
  }

  size_t EvalAlign() const override {
    if (!underlying_type) {
      return alignof(int);
    }
    return underlying_type->GetAlign();
  }
};

class FunctionType : public TypeBase {
  TypeNodeImpl(FunctionType);
  TreeNodePrintFields(return_type, parameters);

protected:
  size_t EvalSize() const override { return sizeof(void *); }

  size_t EvalAlign() const override { return alignof(void *); }

public:
  std::unique_ptr<TypeBase> return_type;
  std::vector<std::unique_ptr<TypeBase>> parameters;
};

class PointerType : public TypeBase {
  TypeNodeImpl(PointerType);
  TreeNodePrintFields(pointee_type);

protected:
  size_t EvalSize() const override { return sizeof(void *); }

  size_t EvalAlign() const override { return alignof(void *); }

public:
  std::unique_ptr<TypeBase> pointee_type;
};

class ArrayType : public TypeBase {
  TypeNodeImpl(ArrayType);
  TreeNodePrintFields(element_type);

public:
  std::string ToString() const override {
    return size ? "[" + std::to_string(*size) + "]" : "[]";
  }

  std::unique_ptr<TypeBase> element_type;
  std::optional<size_t> size;

protected:
  size_t EvalSize() const override {
    if (!size) {
      return sizeof(void *);
    } else {
      return *size * element_type->GetSize();
    }
  }

  size_t EvalAlign() const override { return element_type->GetAlign(); }
};

class OpaqueType : public TypeBase {
  TypeNodeImpl(OpaqueType);

public:
  OpaqueType(std::string_view name, OpaqueTypeKind kind)
      : type_name(name), kind(kind) {
    ASSERT(!type_name.empty(), "OpaqueType name must not be empty");
  }

  std::string ToString() const override {
    std::string result(type_name);
    switch (kind) {
    case OpaqueTypeKind::Named:
      return result;
    case OpaqueTypeKind::StructTag:
      result += " [struct tag]";
      break;
    case OpaqueTypeKind::EnumTag:
      result += " [enum tag]";
      break;
    }
    return result;
  }

  std::string_view type_name;
  OpaqueTypeKind kind;

protected:
  size_t EvalSize() const override {
    RAISE_ERROR("OpaqueType size requires contextual type resolution");
  }

  size_t EvalAlign() const override {
    RAISE_ERROR("OpaqueType alignment requires contextual type resolution");
  }
};

class Declaration;

struct ParsedType {
  std::unique_ptr<TypeBase> type;
  std::string_view declarator_name;
  std::vector<std::string_view> parameter_names;
};

std::vector<ParsedType> ParseType(const Declaration *decl);
