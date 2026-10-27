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

    type->IRName() = std::format("%{}.{}", decl->name, context.counter());
  }

  std::string result = std::format("{}=type{{", type->IRName());
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

static std::string GenGlobalVarZeroInit(TypeBase *type, GenContext &context) {
  if (type->Is<BuiltinType>() || type->Is<PointerType>()) {
    return std::format("{} 0", GetTypeIRName(type));
  }
  if (auto struct_t = type->DynCast<StructType>()) {
    std::string res = std::format("{} {{", GetTypeIRName(struct_t));
    for (size_t i = 0; i < struct_t->fields.size(); ++i) {
      if (i > 0) {
        res += ",";
      }
      res += GenGlobalVarZeroInit(struct_t->fields[i].type.Get(), context);
    }
    res += "}";
    return res;
  }
  RAISE_ERROR("Unexpected type ", type->GetType());
}

std::string GenGlobalVarInit(TypeBase *type, Initializer *initializer,
                             GenContext &context) {
  if (!initializer) {
    return GenGlobalVarZeroInit(type, context);
  }
  if (auto val_init = initializer->DynCast<ValueInitializer>()) {
    if (auto literal_num = val_init->value->DynCast<LiteralNum>()) {
      return std::format(
          "{} {}", GetTypeIRName(type),
          std::visit([](auto &&arg) { return std::to_string(arg); },
                     literal_num->value));
    }
    if (auto literal_str = val_init->value->DynCast<LiteralString>()) {
      std::string symbol =
          std::format("@.literal_string_{}", context.counter());
      context.literal_strings.emplace_back(symbol, literal_str->value);
      return std::format("ptr {}", symbol);
    }
    RAISE_ERROR("Global variable init don't support non-literal value");
  }
  if (auto field_init = initializer->DynCast<FieldInitializer>()) {
    auto struct_t = type->DynCast<StructType>();
    ASSERT(struct_t, "FieldInitializer only support struct type");
    std::vector<std::pair<int, std::string>> field_inits;
    for (const auto &[name, init] : field_init->field_initializers) {
      auto field = struct_t->GetField(name);
      ASSERT(field.has_value(), "Unknown field name: ", name);
      std::string field_init_ir =
          GenGlobalVarInit(field->type.Get(), init.get(), context);
      field_inits.emplace_back(field->index, field_init_ir);
    }
    auto get_init_ir = [&](int index) -> std::string {
      for (const auto &[field_index, field_init_ir] : field_inits) {
        if (field_index == index) {
          return field_init_ir;
        }
      }
      return GenGlobalVarZeroInit(struct_t->fields[index].type.Get(), context);
    };
    std::string res = std::format("{} {{", GetTypeIRName(struct_t));
    for (size_t i = 0; i < struct_t->fields.size(); ++i) {
      if (i > 0) {
        res += ",";
      }
      res += get_init_ir(i);
    }
    res += "}";
    return res;
  }
  RAISE_ERROR("Unexpected type", initializer->GetType());
}
