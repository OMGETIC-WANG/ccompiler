#include "TypeContext.h"

#include "../Exception.h"

#include <array>
#include <functional>

namespace {

struct BuiltinTypeInfo {
  BuiltinKind kind;
  std::string_view name;
  std::string_view ir_name;
  std::optional<size_t> size;
  size_t align;
};

#define MakeBuiltinTypeInfo(kind, type, ir_name)                               \
  BuiltinTypeInfo {                                                            \
    BuiltinKind::kind, #type, ir_name, sizeof(type), alignof(type)             \
  }

// "i1" is only used in IR generation
constexpr std::array BUILTIN_TYPES = {
    BuiltinTypeInfo{BuiltinKind::Void, "void", "void", std::nullopt, 1},
    MakeBuiltinTypeInfo(Int, int, "i32"),
    MakeBuiltinTypeInfo(Char, char, "i8"),
    MakeBuiltinTypeInfo(Float, float, "float"),
    MakeBuiltinTypeInfo(Double, double, "double"),
    MakeBuiltinTypeInfo(Long, long, "i64"),
    BuiltinTypeInfo{BuiltinKind::I1, "i1", "i1", 1, 1}};

size_t HashCombine(size_t seed, size_t value) {
  return seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

size_t HashType(TypeRef type) { return std::hash<TypeRef>{}(type); }

size_t HashString(std::string_view value) {
  return std::hash<std::string_view>{}(value);
}

} // namespace

size_t
type_context_detail::ArrayKeyHash::operator()(const ArrayKey &key) const {
  size_t result = HashType(key.element);
  if (key.size) {
    result = HashCombine(result, std::hash<size_t>{}(*key.size) + 1);
  }
  return result;
}

size_t
type_context_detail::FunctionKeyHash::operator()(const FunctionKey &key) const {
  size_t result = HashType(key.return_type);
  for (TypeRef parameter : key.parameters) {
    result = HashCombine(result, HashType(parameter));
  }
  return result;
}

size_t
type_context_detail::NominalKeyHash::operator()(const NominalKey &key) const {
  return HashCombine(std::hash<ScopeId>{}(key.scope_id), HashString(key.name));
}

TypeContext::TypeContext() {
  for (const auto &info : BUILTIN_TYPES) {
    Make<BuiltinType>(info.kind, info.name, info.ir_name, info.size,
                      info.align);
  }
}

TypeRef TypeContext::Builtin(std::string_view name) const {
  for (const auto &info : BUILTIN_TYPES) {
    if (info.name == name) {
      return Builtin(info.kind);
    }
  }
  RAISE_ERROR("Unknown builtin type ", name);
}

TypeRef TypeContext::Builtin(BuiltinKind kind, TypeTag tag) const {
  for (const auto &storage : storage_) {
    auto *type = storage->DynCast<BuiltinType>();
    if (type && type->Kind() == kind) {
      return TypeRef(type, (TypeRef::TagType)tag);
    }
  }
  RAISE_ERROR("Builtin type is not registered: ", kind);
}

TypeRef TypeContext::Pointer(TypeRef pointee, TypeTag tag) {
  ASSERT(pointee != nullptr, "Pointer pointee must not be null");
  if (auto found = pointers_.find(pointee); found != pointers_.end()) {
    return TypeRef(found->second, (TypeRef::TagType)tag);
  }
  auto *pointer = Make<PointerType>();
  pointer->pointee_type = pointee;
  pointers_.emplace(pointee, pointer);
  return TypeRef(pointer, (TypeRef::TagType)tag);
}

TypeRef TypeContext::Array(TypeRef element, std::optional<size_t> size,
                           TypeTag tag) {
  ASSERT(element != nullptr, "Array element type must not be null");
  type_context_detail::ArrayKey key{.element = element, .size = size};
  if (auto found = arrays_.find(key); found != arrays_.end()) {
    return TypeRef(found->second, (TypeRef::TagType)tag);
  }
  auto *array = Make<ArrayType>();
  array->element_type = element;
  array->size = size;
  arrays_.emplace(key, array);
  return TypeRef(array, (TypeRef::TagType)tag);
}

TypeRef TypeContext::Function(TypeRef return_type,
                              std::span<const TypeRef> parameters,
                              TypeTag tag) {
  ASSERT(return_type != nullptr, "Function return type must not be null");
  for (TypeRef parameter : parameters) {
    ASSERT(parameter != nullptr, "Function parameter type must not be null");
  }
  type_context_detail::FunctionKey key{
      .return_type = return_type,
      .parameters = std::vector<TypeRef>(parameters.begin(), parameters.end()),
  };
  if (auto found = functions_.find(key); found != functions_.end()) {
    return TypeRef(found->second, (TypeRef::TagType)tag);
  }
  auto *function = Make<FunctionType>();
  function->return_type = return_type;
  function->parameters.assign(parameters.begin(), parameters.end());
  functions_.emplace(key, function);
  return TypeRef(function, (TypeRef::TagType)tag);
}

TypeRef TypeContext::GetOrCreateStruct(ScopeId scope_id, std::string_view name,
                                       TypeTag tag) {
  ASSERT(!name.empty(), "Struct type name must not be empty");
  type_context_detail::NominalKey key{.scope_id = scope_id,
                                      .name = std::string(name)};
  if (auto found = structs_.find(key); found != structs_.end()) {
    return TypeRef(found->second, (TypeRef::TagType)tag);
  }
  auto *type = Make<StructType>(scope_id, name);
  structs_.emplace(key, type);
  return TypeRef(type, (TypeRef::TagType)tag);
}

TypeRef TypeContext::GetOrCreateEnum(ScopeId scope_id, std::string_view name,
                                     TypeTag tag) {
  ASSERT(!name.empty(), "Enum type name must not be empty");
  type_context_detail::NominalKey key{.scope_id = scope_id,
                                      .name = std::string(name)};
  if (auto found = enums_.find(key); found != enums_.end()) {
    return TypeRef(found->second, (TypeRef::TagType)tag);
  }
  auto *type = Make<EnumType>(scope_id, name);
  enums_.emplace(key, type);
  return TypeRef(type, (TypeRef::TagType)tag);
}

void TypeContext::CompleteStruct(TypeRef type,
                                 std::span<const StructField> fields) {
  auto *struct_type = Mutable<StructType>(type);
  ASSERT(!struct_type->is_complete,
         "Struct type is already complete: ", struct_type->type_name);
  for (const auto &field : fields) {
    ASSERT(field.type != nullptr, "Struct field type must not be null");
  }
  struct_type->fields.assign(fields.begin(), fields.end());
  struct_type->is_complete = true;
}

void TypeContext::CompleteEnum(TypeRef type, TypeRef underlying_type,
                               std::span<const EnumValue> values) {
  auto *enum_type = Mutable<EnumType>(type);
  ASSERT(!enum_type->is_complete,
         "Enum type is already complete: ", enum_type->enum_name);
  ASSERT(underlying_type != nullptr, "Enum underlying type must not be null");
  enum_type->underlying_type = underlying_type;
  enum_type->enumerators.assign(values.begin(), values.end());
  enum_type->is_complete = true;
}
