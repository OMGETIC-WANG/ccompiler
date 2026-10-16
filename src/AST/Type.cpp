#include "Type.h"
#include "../Exception.h"
#include "../Util/Tree.h"

std::unique_ptr<TypeBase> ParseDeclarator(std::unique_ptr<TypeBase> curr_type,
                                          const Declarator *D) {
  size_t pointer_count = D->pointer_count;
  for (size_t i = 0; i < pointer_count; i++) {
    auto pointer = std::make_unique<PointerType>();
    pointer->pointee_type = std::move(curr_type);
    curr_type = std::move(pointer);
  }

  const DirectDeclarator *DD = D->direct_declarator.get();
  if (!DD) {
    return curr_type;
  }

  for (const auto &suffix : DD->suffixes) {
    switch (suffix->GetType()) {
    case DeclType::ParamList: {
      const ParamList *param_list = (const ParamList *)suffix.get();

      auto func_type = std::make_unique<FunctionType>();
      func_type->return_type = std::move(curr_type);
      func_type->parameters.reserve(param_list->params.size());
      for (const auto &param : param_list->params) {
        auto param_D = std::make_unique<RawType>();
        param_D->specifier = param.specifier.type_spec;
        if (!param.declarator) { // anonymous parameter
          func_type->parameters.push_back(std::move(param_D));
          continue;
        }
        func_type->parameters.push_back(
            ParseDeclarator(std::move(param_D), param.declarator.get()));
      }
      curr_type = std::move(func_type);
      break;
    }
    case DeclType::ArraySuffix: {
      const ArraySuffix *array_suffix = (const ArraySuffix *)suffix.get();

      auto array_type = std::make_unique<ArrayType>();
      array_type->element_type = std::move(curr_type);
      array_type->size = array_suffix->size;
      curr_type = std::move(array_type);
      break;
    }
    default: {
      RAISE_ERROR("Unexpected suffix");
    }
    }
  }

  const DeclBase *DD_D = DD->declarator.get();
  if (DD_D->GetType() == DeclType::IdentifierDeclarator) {
    curr_type->identifier = ((const IdentifierDeclarator *)DD_D)->identifier;
    return curr_type;
  } else if (DD_D->GetType() == DeclType::Declarator) {
    return ParseDeclarator(std::move(curr_type), (const Declarator *)DD_D);
  } else {
    RAISE_ERROR("Unexpected declarator type");
  }
}

std::unique_ptr<TypeBase>
ParseInitDeclarator(std::unique_ptr<TypeBase> curr_type,
                    const InitDeclarator *init_declarator) {
  const Declarator *D = init_declarator->declarator.get();
  // TODO: add initializer parsing
  return ParseDeclarator(std::move(curr_type), D);
}

std::vector<std::unique_ptr<TypeBase>> ParseType(const Declaration *decl) {
  std::vector<std::unique_ptr<TypeBase>> results;
  if (decl->init_declarators.size()) {
    results.reserve(decl->init_declarators.size());
    for (const auto &init_declarator : decl->init_declarators) {
      auto curr_type = std::make_unique<RawType>();
      curr_type->specifier = decl->declaration_specifier.type_spec;
      results.push_back(
          ParseInitDeclarator(std::move(curr_type), init_declarator.get()));
    }
  } else {
    results.reserve(1);
    auto curr_type = std::make_unique<RawType>();
    curr_type->specifier = decl->declaration_specifier.type_spec;
    results.push_back(std::move(curr_type));
  }
  return results;
}

static size_t GetChildCount(const TypeBase *node) {
  switch (node->GetType()) {
  case TypeBaseType::RawType:
    return 0;
  case TypeBaseType::FunctionType: {
    const auto *function = static_cast<const FunctionType *>(node);
    return (function->return_type ? 1 : 0) + function->parameters.size();
  }
  case TypeBaseType::PointerType:
    return static_cast<const PointerType *>(node)->pointee_type ? 1 : 0;
  case TypeBaseType::ArrayType:
    return static_cast<const ArrayType *>(node)->element_type ? 1 : 0;
  }
  return 0;
}

static const TypeBase *GetChild(const TypeBase *node, size_t index) {
  switch (node->GetType()) {
  case TypeBaseType::RawType:
    return nullptr;
  case TypeBaseType::FunctionType: {
    const auto *function = static_cast<const FunctionType *>(node);
    if (function->return_type) {
      if (index == 0)
        return function->return_type.get();
      --index;
    }
    return function->parameters[index].get();
  }
  case TypeBaseType::PointerType:
    return static_cast<const PointerType *>(node)->pointee_type.get();
  case TypeBaseType::ArrayType:
    return static_cast<const ArrayType *>(node)->element_type.get();
  }
  return nullptr;
}

static std::string GetString(const TypeBase *node) {
  std::string result(EnumRefl<TypeBaseType>::GetName(node->GetType()));

  switch (node->GetType()) {
  case TypeBaseType::RawType:
    result += ": ";
    result += static_cast<const RawType *>(node)->specifier.type;
    break;
  case TypeBaseType::ArrayType: {
    const auto *array = static_cast<const ArrayType *>(node);
    result += ": ";
    result += array->size == (size_t)-1
                  ? "[]"
                  : "[" + std::to_string(array->size) + "]";
    break;
  }
  case TypeBaseType::FunctionType:
  case TypeBaseType::PointerType:
    break;
  }

  if (!node->identifier.empty()) {
    result += '\'';
    result += node->identifier;
    result += '\'';
  }
  return result;
}

void TypeBase::Print(std::ostream &out) const {
  PrintTree(this, GetChildCount, GetChild, GetString, out);
}
