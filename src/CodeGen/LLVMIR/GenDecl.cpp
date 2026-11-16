#include "GenDecl.h"

#include "../../AST/Operations.h"
#include "Value.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>

#include <string>
#include <vector>

#include "ConstantLowering.h"

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

void GenEnumDecl(EnumDecl *decl, GenContext &context) {
  (void)context;
  if (!decl->is_definition) {
    return;
  }

  ASSERT(decl->type != nullptr && decl->type->Is<EnumType>(),
         "Enum declaration has no canonical type");
  const auto *enum_type = decl->type->As<EnumType>();
  ASSERT(enum_type->is_complete, "Enum definition was not completed by Sema");
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
      ASSERT(array_type->size.has_value(),
             "Sema left a global array type incomplete");
      ASSERT(array_init->initializers.size() <= array_size,
             "Sema accepted an oversized global array initializer");

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
      return llvm::ConstantArray::get(context.LLVMArrayType(array_type),
                                      elements);
    }
    if (IsLiteralStringInit(array_type, initializer)) {
      auto *literal_str =
          initializer->As<ValueInitializer>()->value->As<LiteralString>();
      ASSERT(array_type->size.value() >= literal_str->value.view().size() + 1,
             "Sema accepted an oversized global string initializer");
      return context.CreateLiteralConstant(literal_str->value.view(),
                                           array_type->size.value());
    }
  }

  if (auto *struct_type = type->DynCast<StructType>()) {
    if (auto *field_init = initializer->DynCast<FieldInitializer>()) {
      std::vector<llvm::Constant *> fields(struct_type->fields.size(), nullptr);
      for (const auto &[name, init] : field_init->field_initializers) {
        auto field = struct_type->GetField(name);
        ASSERT(field.has_value(), "Sema accepted an unknown field");
        size_t field_index = 0;
        bool found = false;
        for (size_t i = 0; i < struct_type->fields.size(); ++i) {
          if (struct_type->fields[i].name == name) {
            field_index = i;
            found = true;
            break;
          }
        }
        ASSERT(found, "Sema accepted an unknown field");
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
             "Sema accepted an oversized struct initializer");
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
    ASSERT(IsNumericType(builtin_type),
           "Sema accepted a non-numeric builtin initializer");
    auto *val_init = initializer->DynCast<ValueInitializer>();
    ASSERT(val_init, "Sema accepted an invalid builtin initializer");
    auto *value = dynamic_cast<Expr *>(val_init->value.get());
    ASSERT(value && value->type == builtin_type,
           "Sema left an initializer with the wrong type");
    return LowerNumericConstant(value->type, value, context);
  }

  if (auto *ptr_type = type->DynCast<PointerType>()) {
    auto *val_init = initializer->DynCast<ValueInitializer>();
    ASSERT(val_init, "Sema accepted an invalid pointer initializer");
    auto *cast = val_init->value->DynCast<ImplicitCast>();
    ASSERT(cast && cast->target_type == ptr_type,
           "Sema omitted the global pointer initializer conversion");
    auto *source = dynamic_cast<Expr *>(cast->operand.get());
    ASSERT(source && source->constant_value.has_value(),
           "Sema did not retain the pointer initializer constant");
    auto *intptr = LowerNumericConstant(source->type, source, context);
    return llvm::ConstantExpr::getIntToPtr(intptr,
                                           context.LLVMType(cast->target_type));
  }

  RAISE_ERROR("Sema accepted an unsupported global initializer");
}

LValue GenGlobalVarDecl(VarDecl *decl, GenContext &context) {
  TypeRef type = decl->type;
  auto *llvm_type = context.LLVMType(type);
  const bool is_definition =
      decl->initializer || decl->storage_class != StorageClass::Extern;
  llvm::Constant *initializer =
      is_definition
          ? GenConstInitializer(type, decl->initializer.get(), context)
          : nullptr;
  auto *global = context.Module().getGlobalVariable(decl->name, true);
  if (global) {
    ASSERT(global->getValueType() == llvm_type,
           "Sema accepted conflicting global variable types");
    if (decl->initializer || (is_definition && global->isDeclaration())) {
      global->setInitializer(initializer);
    }
  } else {
    auto linkage = decl->storage_class == StorageClass::Static
                       ? llvm::GlobalValue::InternalLinkage
                       : llvm::GlobalValue::ExternalLinkage;
    global = new llvm::GlobalVariable(context.Module(), llvm_type,
                                      HasTypeTag(type, TypeTag::Const), linkage,
                                      initializer, decl->name);
  }
  global->setDSOLocal(true);
  global->setAlignment(llvm::Align(type->GetAlign()));

  LValue value{type, global};
  context.AddLValue(decl, value);
  return value;
}

LValue GenExternVarDecl(VarDecl *decl, GenContext &context) {
  TypeRef type = decl->type;
  auto *llvm_type = context.LLVMType(type);
  auto *global = context.Module().getGlobalVariable(decl->name, true);
  if (!global) {
    global = new llvm::GlobalVariable(
        context.Module(), llvm_type, HasTypeTag(type, TypeTag::Const),
        llvm::GlobalValue::ExternalLinkage, nullptr, decl->name);
    global->setDSOLocal(true);
    global->setAlignment(llvm::Align(type->GetAlign()));
  } else {
    ASSERT(global->getValueType() == llvm_type,
           "Sema accepted conflicting extern variable types");
  }

  LValue value{type, global};
  context.AddLValue(decl, value);
  return value;
}
