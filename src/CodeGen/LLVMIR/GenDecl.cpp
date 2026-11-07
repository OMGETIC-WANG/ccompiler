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

void GenUnionDecl(UnionDecl *decl, GenContext &context) {
  if (!decl->fields) {
    return;
  }

  ASSERT(decl->type != nullptr && decl->type->Is<UnionType>(),
         "Union declaration has no canonical type");
  auto *type = decl->type->As<UnionType>();
  ASSERT(type->is_complete, "Defined union must be complete");

  context.LLVMUnionType(decl->type);
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
      if (std::holds_alternative<long double>(val)) {
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
  context.CurrLayer().enums[decl->name] = decl->type->As<EnumType>();
}

static bool IsLiteralStringInit(const ArrayType *type,
                                const Initializer *initializer) {
  return type->element_type->Is<BuiltinType>() &&
         type->element_type->As<BuiltinType>()->Kind() == BuiltinKind::Char &&
         initializer->Is<ValueInitializer>() &&
         initializer->As<ValueInitializer>()->value->Is<LiteralString>();
}

llvm::Constant *GenConstInitializer(TypeRef type, Initializer *initializer,
                                    GenContext &context) {
  if (!initializer) {
    return llvm::Constant::getNullValue(context.LLVMType(type));
  }

  if (auto *array_type = type->DynCast<ArrayType>()) {
    if (auto *array_init = initializer->DynCast<ArrayInitializer>()) {
      size_t array_size =
          array_type->size.value_or(array_init->initializers.size());
      ASSERT(array_init->initializers.size() <= array_size,
             "Array initializer has more elements than the array size");

      std::vector<llvm::Constant *> elements;
      elements.reserve(array_size);
      for (size_t i = 0; i < array_size; ++i) {
        if (i < array_init->initializers.size()) {
          elements.push_back(
              GenConstInitializer(array_type->element_type,
                                  array_init->initializers[i].get(), context));
        } else {
          elements.push_back(llvm::Constant::getNullValue(
              context.LLVMType(array_type->element_type)));
        }
      }
      return llvm::ConstantArray::get(context.LLVMArrayType(type, array_size),
                                      elements);
    }
    if (IsLiteralStringInit(array_type, initializer)) {
      auto *literal_str =
          initializer->As<ValueInitializer>()->value->As<LiteralString>();
      ASSERT(array_type->size.value() >= literal_str->value.view().size() + 1,
             "Array size is smaller than the literal string size");
      return context.CreateLiteralConstant(literal_str->value.view(),
                                           array_type->size.value());
    }
  }

  if (auto *struct_type = type->DynCast<StructType>()) {
    if (auto *field_init = initializer->DynCast<FieldInitializer>()) {
      std::vector<llvm::Constant *> fields(struct_type->fields.size(), nullptr);
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
      for (size_t i = 0; i < fields.size(); i++) {
        if (!fields[i]) {
          fields[i] = llvm::Constant::getNullValue(
              context.LLVMType(struct_type->fields[i].type));
        }
      }
      return llvm::ConstantStruct::get(context.LLVMStructType(type), fields);
    }
    if (auto *array_init = initializer->DynCast<ArrayInitializer>()) {
      ASSERT(array_init->initializers.size() <= struct_type->fields.size(),
             "Struct initializer has more elements than the struct fields");
      std::vector<llvm::Constant *> fields;
      fields.reserve(struct_type->fields.size());
      for (size_t i = 0; i < struct_type->fields.size(); ++i) {
        if (i < array_init->initializers.size()) {
          fields.push_back(
              GenConstInitializer(struct_type->fields[i].type,
                                  array_init->initializers[i].get(), context));
        } else {
          fields.push_back(llvm::Constant::getNullValue(
              context.LLVMType(struct_type->fields[i].type)));
        }
      }
      return llvm::ConstantStruct::get(context.LLVMStructType(type), fields);
    }
  }

  if (auto *builtin_type = type->DynCast<BuiltinType>()) {
    ASSERT(IsNumericType(builtin_type), "Builtin type must be numeric");
    auto *val_init = initializer->DynCast<ValueInitializer>();
    ASSERT(val_init, "Builtin type requires ValueInitializer");
    return EvalNumericConstant(type, val_init->value.get(), context);
  }

  if (auto *ptr_type = type->DynCast<PointerType>()) {
    auto *val_init = initializer->DynCast<ValueInitializer>();
    ASSERT(val_init, "Pointer type requires ValueInitializer");
    auto *intptr =
        EvalNumericConstant(context.Types().Builtin(BuiltinKind::Int),
                            val_init->value.get(), context);
    return llvm::ConstantExpr::getIntToPtr(intptr, context.LLVMType(ptr_type));
  }

  RAISE_ERROR("Unexpected initializer type");
}

LValue GenGlobalVarDecl(VarDecl *decl, GenContext &context) {
  auto *llvm_type = context.LLVMType(decl->type);
  if (!IsConstantFoldable(decl->initializer.get())) {
    RAISE_ERROR("Global variable initializer must be constant foldable");
  }
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
