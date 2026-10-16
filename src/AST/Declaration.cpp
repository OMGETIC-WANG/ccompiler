#include "Declaration.h"

#include "../Util/Tree.h"

static size_t GetChildCount(const DeclBase *node) {
  switch (node->GetType()) {
  case DeclType::Declaration:
    return static_cast<const Declaration *>(node)->init_declarators.size();
  case DeclType::InitDeclarator:
    return (static_cast<const InitDeclarator *>(node)->declarator ? 1 : 0) +
           (static_cast<const InitDeclarator *>(node)->initializer ? 1 : 0);
  case DeclType::Declarator:
    return static_cast<const Declarator *>(node)->direct_declarator ? 1 : 0;
  case DeclType::DirectDeclarator: {
    const auto *declarator = static_cast<const DirectDeclarator *>(node);
    return (declarator->declarator ? 1 : 0) + declarator->suffixes.size();
  }
  case DeclType::ParamList:
    return static_cast<const ParamList *>(node)->params.size();
  case DeclType::FieldInitializer: {
    const auto *initializer = static_cast<const FieldInitializer *>(node);
    return initializer->field_initializers.size();
  }
  case DeclType::Initializer:
    return 0;
  case DeclType::ValueInitializer:
    return 0;
  case DeclType::Pointer:
  case DeclType::IdentifierDeclarator:
  case DeclType::ArraySuffix:
    return 0;
  }
  return 0;
}

static const DeclBase *GetChild(const DeclBase *node, size_t index) {
  switch (node->GetType()) {
  case DeclType::Declaration:
    return static_cast<const Declaration *>(node)
        ->init_declarators[index]
        .get();
  case DeclType::InitDeclarator:
    if (const auto *init_declarator = static_cast<const InitDeclarator *>(node);
        init_declarator->declarator) {
      if (index == 0)
        return init_declarator->declarator.get();
      --index;
    }
    return static_cast<const InitDeclarator *>(node)->initializer.get();
  case DeclType::Declarator:
    return static_cast<const Declarator *>(node)->direct_declarator.get();
  case DeclType::DirectDeclarator: {
    const auto *declarator = static_cast<const DirectDeclarator *>(node);
    if (declarator->declarator) {
      if (index == 0)
        return declarator->declarator.get();
      --index;
    }
    return declarator->suffixes[index].get();
  }
  case DeclType::ParamList:
    return static_cast<const ParamList *>(node)->params[index].declarator.get();
  case DeclType::FieldInitializer:
    return static_cast<const FieldInitializer *>(node)
        ->field_initializers[index]
        .second.get();
  case DeclType::Initializer:
    return nullptr;
  case DeclType::ValueInitializer:
    return nullptr;
  case DeclType::Pointer:
  case DeclType::IdentifierDeclarator:
  case DeclType::ArraySuffix:
    return nullptr;
  }
  return nullptr;
}

static std::string GetString(const DeclBase *node) {
  const auto type = EnumRefl<DeclType>::GetName(node->GetType());
  switch (node->GetType()) {
  case DeclType::Declaration:
    return std::string(type) + ": " +
           std::string(static_cast<const Declaration *>(node)
                           ->declaration_specifier.type_spec.type);
  case DeclType::Declarator:
    return std::string(type) + ": pointers=" +
           std::to_string(static_cast<const Declarator *>(node)->pointer_count);
  case DeclType::IdentifierDeclarator:
    return std::string(type) + ": " +
           std::string(
               static_cast<const IdentifierDeclarator *>(node)->identifier);
  case DeclType::ArraySuffix: {
    const auto *suffix = static_cast<const ArraySuffix *>(node);
    return std::string(type) + ": " +
           (suffix->size == (size_t)-1
                ? "[]"
                : "[" + std::to_string(suffix->size) + "]");
  }
  case DeclType::ValueInitializer: {
    const auto *initializer = static_cast<const ValueInitializer *>(node);
    if (!initializer->value)
      return std::string(type);
    return std::string(type) + ": " + initializer->value->ToString();
  }
  case DeclType::Pointer:
  case DeclType::InitDeclarator:
  case DeclType::DirectDeclarator:
  case DeclType::Initializer:
  case DeclType::FieldInitializer:
    return std::string(type);
  case DeclType::ParamList: {
    std::string s(type);
    s += " [";
    for (const auto &spec : static_cast<const ParamList *>(node)->params) {
      s += spec.specifier.type_spec.type;
      s += " ";
    }
    s += "]";
    return s;
  }
  }
  return std::string(type);
}

void DeclBase::Print(std::ostream &out) const {
  PrintTree(this, GetChildCount, GetChild, GetString, out);
}
