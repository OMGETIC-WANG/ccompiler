#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../Exception.h"
#include "../Lex/Lexer.h"
#include "../Util/EnumRefl.h"
#include "../Util/TaggedPointer.h"
#include "../Util/Tree.h"

using ScopeId = uint64_t;

class TypeBase;
class TypeContext;
class DiagnosticContext;

struct TypeSpecifier {
  SourceLocation location;
  std::string_view type_name;
  ScopeId scope_id = 0;
  bool is_struct = false;
  bool is_union = false;
  bool is_typedef = false;
  bool is_builtin = false;
  bool is_enum = false;
  bool is_named_type = false;
  bool is_const = false;
  bool is_volatile = false;
  bool is_restrict = false;
  bool is_unsigned = false;
};

// clang-format off
#define BUILTIN_KIND_VALUES(x)\
  x(Void)\
  x(Int)\
  x(Char)\
  x(Short)\
  x(Float)\
  x(Double)\
  x(LongDouble)\
  x(Long)\
  x(LongLong)\
  x(I1)
// clang-format on

MAKE_ENUM(BuiltinKind, BUILTIN_KIND_VALUES)
REGISTER_ENUM(BuiltinKind, BUILTIN_KIND_VALUES)

// clang-format off
#define TYPE_BASE_TYPE_VALUES(x)\
  x(BuiltinType)\
  x(StructType)\
  x(UnionType)\
  x(EnumType)\
  x(FunctionType)\
  x(ArrayType)\
  x(PointerType)
// clang-format on

MAKE_ENUM(TypeBaseType, TYPE_BASE_TYPE_VALUES)
REGISTER_ENUM(TypeBaseType, TYPE_BASE_TYPE_VALUES)

class alignas(16) TypeBase : public TreeNode {
public:
  virtual ~TypeBase() = default;

  virtual TypeBaseType GetType() const = 0;

  std::string_view GetNodeTypeName() const override;

  size_t GetSize() const {
    if (!_cached_size) {
      _cached_size = EvalSize();
    }
    return *_cached_size;
  }

  size_t GetAlign() const {
    if (!_cached_align) {
      _cached_align = EvalAlign();
    }
    return *_cached_align;
  }

  template <std::derived_from<TypeBase> T> const T *As() const {
#ifdef ENABLE_AS_TYPE_CHECK
    if (GetType() != T::__type) {
      RAISE_ERROR("Invalid TypeBase type cast from ", GetType(), " to ",
                  T::__type);
    }
#endif
    return static_cast<const T *>(this);
  }

  template <std::derived_from<TypeBase> T> T *As() {
    return const_cast<T *>(std::as_const(*this).template As<T>());
  }

  template <std::derived_from<TypeBase> T> const T *DynCast() const {
    if (GetType() != T::__type) {
      return nullptr;
    }
    return static_cast<const T *>(this);
  }

  template <std::derived_from<TypeBase> T> T *DynCast() {
    return const_cast<T *>(std::as_const(*this).template DynCast<T>());
  }

  template <std::derived_from<TypeBase> T> bool Is() const {
    return GetType() == T::__type;
  }

protected:
  virtual size_t EvalSize() const = 0;

  virtual size_t EvalAlign() const = 0;

private:
  mutable std::optional<size_t> _cached_size;
  mutable std::optional<size_t> _cached_align;
};

// Non-owning. TypeContext owns the referenced object. The tag stores type
// attributes without duplicating TypeBase nodes.
using TypeRef = TaggedPointer<TypeBase, 16>;

enum class TypeTag : TypeRef::TagType {
  None = 0,
  Const = 1 << 0,
  Unsigned = 1 << 1,
  Volatile = 1 << 2,
  Restrict = 1 << 3,
};

constexpr TypeRef::TagType TypeTagValue(TypeTag tag) {
  return static_cast<TypeRef::TagType>(tag);
}

constexpr TypeRef::TagType SupportedTypeTags =
    TypeTagValue(TypeTag::Const) | TypeTagValue(TypeTag::Unsigned) |
    TypeTagValue(TypeTag::Volatile) | TypeTagValue(TypeTag::Restrict);

constexpr TypeRef::TagType TypeQualifierTags = TypeTagValue(TypeTag::Const) |
                                               TypeTagValue(TypeTag::Volatile) |
                                               TypeTagValue(TypeTag::Restrict);

static_assert(SupportedTypeTags <= TypeRef::tag_mask,
              "TypeTag does not fit in TypeRef");

inline TypeRef ApplyTypeTag(TypeRef type, TypeTag tag) {
  const TypeRef::TagType value = TypeTagValue(tag);
  ASSERT((value & static_cast<TypeRef::TagType>(~SupportedTypeTags)) == 0,
         "TypeTag uses reserved bits");
  ASSERT(type != nullptr || value == 0, "A null TypeRef cannot carry a tag");
  type.set_tag(static_cast<TypeRef::TagType>(type.get_tag() | value));
  return type;
}

inline TypeRef StripTypeTag(TypeRef type, TypeTag tag) {
  const TypeRef::TagType value = TypeTagValue(tag);
  ASSERT((value & static_cast<TypeRef::TagType>(~SupportedTypeTags)) == 0,
         "TypeTag uses reserved bits");
  type.set_tag(static_cast<TypeRef::TagType>(type.get_tag() & ~value));
  return type;
}

inline bool HasTypeTag(TypeRef type, TypeTag tag) {
  const TypeRef::TagType value = TypeTagValue(tag);
  return (type.get_tag() & value) == value;
}

inline TypeRef StripTypeQualifiers(TypeRef type) {
  type.set_tag(
      static_cast<TypeRef::TagType>(type.get_tag() & ~TypeQualifierTags));
  return type;
}

#define TypeNodeImpl(type_enum)                                                \
public:                                                                        \
  TypeBaseType GetType() const override { return TypeBaseType::type_enum; }    \
  constexpr static TypeBaseType __type = TypeBaseType::type_enum

class BuiltinType : public TypeBase {
  TypeNodeImpl(BuiltinType);

  std::string ToString() const override { return std::string(type_name); }

private:
  friend class TypeContext;

  BuiltinType(BuiltinKind kind, std::string_view name, std::string_view ir_name,
              std::optional<size_t> size, size_t align)
      : type_name(name), _kind(kind), _ir_name(ir_name), _size(size),
        _align(align) {
    ASSERT(!type_name.empty(), "BuiltinType name must not be empty");
  }

public:
  BuiltinType(const BuiltinType &) = delete;
  BuiltinType &operator=(const BuiltinType &) = delete;

  std::string_view IRName() const { return _ir_name; }
  BuiltinKind Kind() const { return _kind; }

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
  BuiltinKind _kind;
  std::string_view _ir_name;
  std::optional<size_t> _size;
  size_t _align;
};

inline bool IsIntegerKind(BuiltinKind kind) {
  return kind == BuiltinKind::Char || kind == BuiltinKind::Short ||
         kind == BuiltinKind::Int || kind == BuiltinKind::Long ||
         kind == BuiltinKind::LongLong || kind == BuiltinKind::I1;
}

inline bool IsIntegerType(TypeRef type) {
  if (auto builtin = type->DynCast<BuiltinType>()) {
    BuiltinKind kind = builtin->Kind();
    return IsIntegerKind(kind);
  }
  return false;
}

inline bool IsFloatingKind(BuiltinKind kind) {
  return kind == BuiltinKind::Float || kind == BuiltinKind::Double ||
         kind == BuiltinKind::LongDouble;
}

inline bool IsFloatingType(TypeRef type) {
  if (auto builtin = type->DynCast<BuiltinType>()) {
    BuiltinKind kind = builtin->Kind();
    return IsFloatingKind(kind);
  }
  return false;
}

inline bool IsNumericType(TypeRef type) {
  return IsIntegerType(type) || IsFloatingType(type);
}

int IntegerConversionRank(BuiltinKind kind);

int FloatingConversionRank(BuiltinKind kind);

TypeRef PromoteInteger(TypeRef type, TypeContext &types);

TypeRef UsualArithmeticType(TypeRef left, TypeRef right, TypeContext &types);

struct StructField {
  std::string name;
  TypeRef type;
  size_t index; // <index>th field of type <type>, assigned on IR building stage
};

class StructType : public TypeBase {
  TypeNodeImpl(StructType);

  std::string ToString() const override;

private:
  friend class TypeContext;

  StructType(ScopeId scope_id, std::string_view name)
      : type_name(name), scope_id(scope_id) {
    ASSERT(!type_name.empty(), "StructType name must not be empty");
  }

public:
  StructType(const StructType &) = delete;
  StructType &operator=(const StructType &) = delete;

  std::string type_name;
  ScopeId scope_id;

  std::vector<StructField> fields;
  bool is_complete = false;

  std::optional<StructField> GetField(std::string_view name) const {
    for (const auto &field : fields) {
      if (field.name == name) {
        return field;
      }
    }
    return std::nullopt;
  }

  std::string &IRName() { return _ir_name; }

protected:
  size_t EvalSize() const override {
    if (!is_complete) {
      RAISE_ERROR("Incomplete struct type has no size: ", type_name);
    }

    size_t align = GetAlign();
    size_t size = 0;
    for (const auto &field : fields) {
      size_t field_align = field.type->GetAlign();
      size = (size + field_align - 1) / field_align * field_align;
      size += field.type->GetSize();
    }
    if (align > 0) {
      size = (size + align - 1) / align * align;
    }
    return size;
  }

  size_t EvalAlign() const override {
    if (!is_complete) {
      RAISE_ERROR("Incomplete struct type has no alignment: ", type_name);
    }

    size_t max_valid_align = 1;
    for (const auto &field : fields) {
      max_valid_align = std::max(field.type->GetAlign(), max_valid_align);
    }
    return max_valid_align;
  }

private:
  std::string _ir_name;
};

struct UnionField {
  std::string_view name;
  TypeRef type;
};

class UnionType : public TypeBase {
  TypeNodeImpl(UnionType);

  std::string ToString() const override;

private:
  friend class TypeContext;

  UnionType(ScopeId scope_id, std::string_view name)
      : type_name(name), scope_id(scope_id) {
    ASSERT(!type_name.empty(), "StructType name must not be empty");
  }

public:
  std::string type_name;
  size_t scope_id;

  std::vector<UnionField> fields;
  bool is_complete = false;

  std::optional<UnionField> GetField(std::string_view name) const {
    for (const auto &field : fields) {
      if (field.name == name) {
        return field;
      }
    }
    return std::nullopt;
  }

  std::optional<UnionField> GetMaxSizeField() const {
    std::optional<UnionField> res;
    for (const auto &field : fields) {
      if (res.has_value()) {
        if (field.type->GetSize() > res.value().type->GetSize()) {
          res = field;
        }
      } else {
        res = field;
      }
    }
    return res;
  }

  std::string &IRName() { return _ir_name; }

protected:
  size_t EvalSize() const override {
    if (!is_complete) {
      RAISE_ERROR("Incomplete union type has no size: ", type_name);
    }

    size_t max_size = 0;
    for (const auto &field : fields) {
      max_size = std::max(field.type->GetSize(), max_size);
    }
    size_t align = GetAlign();
    return (max_size + align - 1) / align * align;
  }

  size_t EvalAlign() const override {
    if (!is_complete) {
      RAISE_ERROR("Incomplete union type has no alignment: ", type_name);
    }

    size_t max_align = 1;
    for (const auto &field : fields) {
      max_align = std::max(field.type->GetAlign(), max_align);
    }
    return max_align;
  }

private:
  std::string _ir_name;
};

struct EnumValue {
  std::string name;
  std::variant<int64_t, uint64_t> value;
};

class EnumType : public TypeBase {
  TypeNodeImpl(EnumType);

  std::string ToString() const override;

private:
  friend class TypeContext;

  EnumType(ScopeId scope_id, std::string_view name)
      : enum_name(name), scope_id(scope_id) {
    ASSERT(!enum_name.empty(), "EnumType name must not be empty");
  }

public:
  EnumType(const EnumType &) = delete;
  EnumType &operator=(const EnumType &) = delete;

  std::string enum_name;
  ScopeId scope_id;

  TypeRef underlying_type = nullptr;

  std::vector<EnumValue> enumerators;
  bool is_complete = false;

protected:
  size_t EvalSize() const override {
    if (!underlying_type) {
      return sizeof(int);
    }
    return underlying_type->GetSize();
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

  std::string ToString() const override;

protected:
  size_t EvalSize() const override { return sizeof(void *); }

  size_t EvalAlign() const override { return alignof(void *); }

public:
  TypeRef return_type = nullptr;
  std::vector<TypeRef> parameters;
  bool has_va_args = false;

private:
  friend class TypeContext;
  FunctionType() = default;
};

class PointerType : public TypeBase {
  TypeNodeImpl(PointerType);

  std::string ToString() const override;

protected:
  size_t EvalSize() const override { return sizeof(void *); }

  size_t EvalAlign() const override { return alignof(void *); }

public:
  TypeRef pointee_type = nullptr;

private:
  friend class TypeContext;
  PointerType() = default;
};

class ArrayType : public TypeBase {
  TypeNodeImpl(ArrayType);

  std::string ToString() const override;

public:
  TypeRef element_type = nullptr;
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

private:
  friend class TypeContext;
  ArrayType() = default;
};

class Declarator;

struct ParsedParameter {
  TypeRef type = nullptr;
  SourceLocation location;
  std::string_view name;
};

struct ParsedType {
  TypeRef type = nullptr;
  SourceLocation location;
  std::string_view declarator_name;
  std::vector<ParsedParameter> parameters;
};

class CompileContext;

TypeRef ApplyTypeQualifier(TypeRef type, TypeTag qualifier, TypeContext &types,
                           DiagnosticContext &diagnostics,
                           SourceLocation location);

TypeRef ApplyTypeSpecifierQualifiers(TypeRef type,
                                     const TypeSpecifier &specifier,
                                     TypeContext &types,
                                     DiagnosticContext &diagnostics);

TypeRef ApplyAggregateMemberQualifiers(TypeRef member_type,
                                       TypeRef aggregate_type,
                                       TypeContext &types);

TypeRef ResolveTypeSpecifier(const TypeSpecifier &specifier,
                             CompileContext &context);

ParsedType ParseDeclaratorType(const TypeSpecifier &spec,
                               const Declarator &declarator,
                               CompileContext &context);
