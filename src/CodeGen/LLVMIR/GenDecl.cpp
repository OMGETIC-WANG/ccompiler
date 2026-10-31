#include "GenDecl.h"

#include "Value.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>

#include <string>
#include <vector>

#include "ConstantEval.h"

void GenStructDecl(StructDecl *decl, GenContext &context) {
  if (!decl->fields) {
    return;
  }

  ASSERT(decl->type != nullptr && decl->type->Is<StructType>(),
         "Struct declaration has no canonical type");
  auto *type = decl->type->As<StructType>();
  ASSERT(type->is_complete, "Defined struct must be complete");

  context.LLVMStructType(decl->type);
}

int64_t GetEnumValue(decltype(LiteralNum::value) val) {
  if (std::holds_alternative<int64_t>(val)) {
    return std::get<int64_t>(val);
  }
  return std::get<uint64_t>(val);
}

void GenEnumDecl(EnumDecl *decl, GenContext &context) {
  if (!decl->is_definition) {
    return;
  }

  ASSERT(decl->type != nullptr && decl->type->Is<EnumType>(),
         "Enum declaration has no canonical type");
  const auto *enum_type = decl->type->As<EnumType>();
  if (enum_type->is_complete) {
    return;
  }

  std::vector<EnumValue> values;
  values.reserve(decl->values.size());
  for (size_t i = 0; i < decl->values.size(); i++) {
    const auto &element = decl->values[i];
    std::variant<int64_t, uint64_t> enum_val;
    if (element->value) {
      NumericValue val = EvalNumeric(element->value.get());
      if (std::holds_alternative<double>(val)) {
        RAISE_ERROR("Enum value cannot be a floating point number");
      }
      if (std::holds_alternative<int64_t>(val)) {
        enum_val = std::get<int64_t>(val);
      } else {
        enum_val = std::get<uint64_t>(val);
      }
    } else {
      if (i == 0) {
        enum_val = 0;
      } else {
        auto prev = values.back();
        enum_val = std::visit(
            [](auto arg) -> std::variant<int64_t, uint64_t> { return arg + 1; },
            prev.value);
      }
    }
    values.push_back({.name = std::string(element->name), .value = enum_val});
  }

  TypeRef underlying = context.Types().Builtin(BuiltinKind::Int);
  context.Types().CompleteEnum(decl->type, underlying, values);
}

llvm::Constant *GenConstInitializer(TypeRef type, Initializer *initializer,
                                    GenContext &context) {
  if (!initializer) {
    return llvm::Constant::getNullValue(context.LLVMType(type));
  }

  if (auto *value_init = initializer->DynCast<ValueInitializer>()) {
    if (auto *literal = value_init->value->DynCast<LiteralNum>()) {
      return EvalNumericConstant(type, literal, context);
    }
    if (auto *literal = value_init->value->DynCast<LiteralString>()) {
      ASSERT(type->Is<PointerType>(),
             "String initializer requires a pointer type");
      return llvm::cast<llvm::Constant>(
          context.CreateStringConstant(literal->value));
    }
    RAISE_ERROR("Global variable init doesn't support non-literal value");
  }

  if (auto *field_init = initializer->DynCast<FieldInitializer>()) {
    auto *struct_type = type->DynCast<StructType>();
    ASSERT(struct_type, "FieldInitializer only supports struct type");
    std::vector<llvm::Constant *> fields;
    fields.reserve(struct_type->fields.size());
    for (const auto &field : struct_type->fields) {
      fields.push_back(
          llvm::Constant::getNullValue(context.LLVMType(field.type)));
    }
    for (const auto &[name, init] : field_init->field_initializers) {
      auto field = struct_type->GetField(name);
      ASSERT(field.has_value(), "Unknown field name: ", name);
      size_t field_index = 0;
      bool found = false;
      for (size_t i = 0; i < struct_type->fields.size(); ++i) {
        if (struct_type->fields[i].name == name) {
          field_index = i;
          found = true;
          break;
        }
      }
      ASSERT(found, "Unknown field name: ", name);
      fields[field_index] =
          GenConstInitializer(field->type, init.get(), context);
    }
    return llvm::ConstantStruct::get(context.LLVMStructType(type), fields);
  }

  RAISE_ERROR("Unexpected initializer type");
}

LValue GenGlobalVarDecl(VarDecl *decl, GenContext &context) {
  auto *llvm_type = context.LLVMType(decl->type);
  llvm::Constant *initializer =
      GenConstInitializer(decl->type, decl->initializer.get(), context);
  auto *global = new llvm::GlobalVariable(
      context.Module(), llvm_type, HasTypeTag(decl->type, TypeTag::Const),
      llvm::GlobalValue::ExternalLinkage, initializer, decl->name);
  global->setDSOLocal(true);
  global->setAlignment(llvm::Align(decl->type->GetAlign()));

  LValue value{decl->type, global};
  context.CurrLayer().variables[decl->name] = value;
  return value;
}
