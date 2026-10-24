#include "GenDecl.h"
#include "Naming.h"

std::string GenVariableDecl(VarDecl *decl, GenContext &context) {
  TypeRef resolved = decl->type;
  std::string_view name = decl->name;

  IRValue val = IRValue::LocalReg(resolved, context.counter());
  context.CurrLayer().variables[name] = val;

  auto type_s = GetTypeIRName(resolved);
  size_t align = resolved->GetAlign();

  return std::format("{}=alloca {},align {}\n", val.name, type_s, align);
}

std::string GenStructDecl(StructDecl *decl, GenContext &context) {
  if (!decl->fields) {
    return {};
  }

  ASSERT(decl->type != nullptr && decl->type->Is<StructType>(),
         "Struct declaration has no canonical type");
  auto *type = decl->type->As<StructType>();
  ASSERT(type->is_complete, "Defined struct must be complete");

  {
    std::unordered_map<TypeRef, size_t> map;
    for (auto &field : type->fields) {
      field.index = map[field.type]++;
    }
  }

  std::string result = std::format("%{}=type{{", decl->name);
  for (size_t i = 0; i < type->fields.size(); ++i) {
    if (i > 0) {
      result += ",";
    }
    result += GetTypeIRName(type->fields[i].type);
  }
  result += "}\n";
  return result;
}

int64_t GetEnumValue(decltype(LiteralNum::value) val) {
  if (std::holds_alternative<int64_t>(val)) {
    return std::get<int64_t>(val);
  }
  return std::get<uint64_t>(val);
}

std::string GenEnumDecl(EnumDecl *decl, GenContext &context) {
  if (!decl->is_definition) {
    return {};
  }

  ASSERT(decl->type != nullptr && decl->type->Is<EnumType>(),
         "Enum declaration has no canonical type");
  const auto *enum_type = decl->type->As<EnumType>();
  if (enum_type->is_complete) {
    return {};
  }

  std::vector<EnumValue> values;
  values.reserve(decl->values.size());
  for (const auto &element : decl->values) {
    ASSERT_EQ(element->value->GetType(), ASTNodeType::LiteralNum,
              "Only support literal num enum value yet");
    values.push_back(
        {.name = std::string(element->name),
         .value = GetEnumValue(element->value->As<LiteralNum>()->value)});
  }

  TypeRef underlying = context.Types().Builtin(BuiltinKind::Int);
  context.Types().CompleteEnum(decl->type, underlying, values);
  return {};
}
