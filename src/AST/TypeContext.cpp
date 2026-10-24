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

// "i1" is only used in IR generation
constexpr std::array BUILTIN_TYPES = {
    BuiltinTypeInfo{BuiltinKind::Void, "void", "void", std::nullopt, 1},
    BuiltinTypeInfo{BuiltinKind::Int, "int", "i32", 4, alignof(int)},
    BuiltinTypeInfo{BuiltinKind::Char, "char", "i8", 1, alignof(char)},
    BuiltinTypeInfo{BuiltinKind::Float, "float", "float", 4, alignof(float)},
    BuiltinTypeInfo{BuiltinKind::Double, "double", "double", 8,
                    alignof(double)},
    BuiltinTypeInfo{BuiltinKind::Long, "long", "i64", 8, alignof(long)},
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

TypeRef TypeContext::Builtin(BuiltinKind kind) const {
  for (const auto &storage : storage_) {
    auto *type = storage->DynCast<BuiltinType>();
    if (type && type->Kind() == kind) {
      return type;
    }
  }
  RAISE_ERROR("Builtin type is not registered: ", kind);
}

TypeRef TypeContext::Pointer(TypeRef pointee) {
  ASSERT(pointee != nullptr, "Pointer pointee must not be null");
  if (auto found = pointers_.find(pointee); found != pointers_.end()) {
    return found->second;
  }
  auto *pointer = Make<PointerType>();
  pointer->pointee_type = pointee;
  pointers_.emplace(pointee, pointer);
  return pointer;
}

TypeRef TypeContext::Array(TypeRef element, std::optional<size_t> size) {
  ASSERT(element != nullptr, "Array element type must not be null");
  type_context_detail::ArrayKey key{.element = element, .size = size};
  if (auto found = arrays_.find(key); found != arrays_.end()) {
    return found->second;
  }
  auto *array = Make<ArrayType>();
  array->element_type = element;
  array->size = size;
  arrays_.emplace(key, array);
  return array;
}

TypeRef TypeContext::Function(TypeRef return_type,
                              std::span<const TypeRef> parameters) {
  ASSERT(return_type != nullptr, "Function return type must not be null");
  for (TypeRef parameter : parameters) {
    ASSERT(parameter != nullptr, "Function parameter type must not be null");
  }
  type_context_detail::FunctionKey key{
      .return_type = return_type,
      .parameters = std::vector<TypeRef>(parameters.begin(), parameters.end()),
  };
  if (auto found = functions_.find(key); found != functions_.end()) {
    return found->second;
  }
  auto *function = Make<FunctionType>();
  function->return_type = return_type;
  function->parameters.assign(parameters.begin(), parameters.end());
  functions_.emplace(key, function);
  return function;
}

TypeRef TypeContext::GetOrCreateStruct(ScopeId scope_id,
                                       std::string_view name) {
  ASSERT(!name.empty(), "Struct type name must not be empty");
  type_context_detail::NominalKey key{.scope_id = scope_id,
                                      .name = std::string(name)};
  if (auto found = structs_.find(key); found != structs_.end()) {
    return found->second;
  }
  auto *type = Make<StructType>(scope_id, name);
  structs_.emplace(key, type);
  return type;
}

TypeRef TypeContext::GetOrCreateEnum(ScopeId scope_id, std::string_view name) {
  ASSERT(!name.empty(), "Enum type name must not be empty");
  type_context_detail::NominalKey key{.scope_id = scope_id,
                                      .name = std::string(name)};
  if (auto found = enums_.find(key); found != enums_.end()) {
    return found->second;
  }
  auto *type = Make<EnumType>(scope_id, name);
  enums_.emplace(key, type);
  return type;
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
