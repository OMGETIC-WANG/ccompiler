#pragma once

#include "Type.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace type_context_detail {

struct ArrayKey {
  TypeRef element;
  std::optional<size_t> size;

  bool operator==(const ArrayKey &) const = default;
};

struct ArrayKeyHash {
  size_t operator()(const ArrayKey &key) const;
};

struct FunctionKey {
  TypeRef return_type;
  std::vector<TypeRef> parameters;

  bool operator==(const FunctionKey &) const = default;
};

struct FunctionKeyHash {
  size_t operator()(const FunctionKey &key) const;
};

struct NominalKey {
  ScopeId scope_id;
  std::string name;

  bool operator==(const NominalKey &) const = default;
};

struct NominalKeyHash {
  size_t operator()(const NominalKey &key) const;
};

} // namespace type_context_detail

// Owns and interns every type in one compilation unit. It must outlive all
// TypeRef values stored in the AST and code generator.
class TypeContext {
public:
  TypeContext();

  TypeContext(const TypeContext &) = delete;
  TypeContext &operator=(const TypeContext &) = delete;

  TypeRef Builtin(std::string_view name) const;
  TypeRef Builtin(BuiltinKind kind, TypeTag tag = TypeTag::None) const;

  TypeRef Pointer(TypeRef pointee, TypeTag tag = TypeTag::None);
  TypeRef Array(TypeRef element, std::optional<size_t> size,
                TypeTag tag = TypeTag::None);
  TypeRef Function(TypeRef return_type, std::span<const TypeRef> parameters,
                   TypeTag tag = TypeTag::None);

  TypeRef GetOrCreateStruct(ScopeId scope_id, std::string_view name,
                            TypeTag tag = TypeTag::None);
  TypeRef GetOrCreateUnion(ScopeId scope_id, std::string_view name,
                           TypeTag tag = TypeTag::None);
  TypeRef GetOrCreateEnum(ScopeId scope_id, std::string_view name,
                          TypeTag tag = TypeTag::None);

  void CompleteStruct(TypeRef type, std::span<const StructField> fields);
  void CompleteUnion(TypeRef type, std::span<const UnionField> fields);
  void CompleteEnum(TypeRef type, TypeRef underlying_type,
                    std::span<const EnumValue> values);

  size_t TypeCount() const { return storage_.size(); }

private:
  struct detail;

  template <typename T, typename... Args> T *Make(Args &&...args) {
    std::unique_ptr<T> storage(new T(std::forward<Args>(args)...));
    T *result = storage.get();
    storage_.push_back(std::move(storage));
    return result;
  }

  template <typename T> T *Mutable(TypeRef type) const {
    ASSERT(type != nullptr, "Cannot mutate a null type");
    ASSERT(type.get_tag() == 0, "Cannot mutate a tagged type reference");
    ASSERT(type->Is<T>(), "TypeContext mutation used with the wrong type");
    return const_cast<T *>(type->As<T>());
  }

  std::vector<std::unique_ptr<TypeBase>> storage_;

  std::unordered_map<TypeRef, PointerType *> pointers_;
  std::unordered_map<type_context_detail::ArrayKey, ArrayType *,
                     type_context_detail::ArrayKeyHash>
      arrays_;
  std::unordered_map<type_context_detail::FunctionKey, FunctionType *,
                     type_context_detail::FunctionKeyHash>
      functions_;
  std::unordered_map<type_context_detail::NominalKey, StructType *,
                     type_context_detail::NominalKeyHash>
      structs_;
  std::unordered_map<type_context_detail::NominalKey, EnumType *,
                     type_context_detail::NominalKeyHash>
      enums_;

  std::unordered_map<type_context_detail::NominalKey, UnionType *,
                     type_context_detail::NominalKeyHash>
      unions_;
};
