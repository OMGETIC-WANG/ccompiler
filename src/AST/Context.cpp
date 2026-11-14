#include "Context.h"

#include "AST.h"

#include <algorithm>

CompileContext::CompileContext(TypeContext &types,
                               SourceManager &source_manager,
                               DiagnosticContext &diagnostics)
    : _types(types), _source_manager(source_manager), _diagnostics(diagnostics),
      _next_scope_id(1) {}

CompileContext::~CompileContext() = default;

const CompileContextSymbol *
CompileContext::FindOrdinary(std::string_view name) const {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    auto found = it->ordinary_decls.find(name);
    if (found != it->ordinary_decls.end()) {
      return &found->second;
    }
  }
  return nullptr;
}

const CompileContextSymbol *
CompileContext::FindTag(std::string_view name) const {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    auto found = it->tag_decls.find(name);
    if (found != it->tag_decls.end()) {
      return &found->second;
    }
  }
  return nullptr;
}

void CompileContext::AddFunction(FunctionDecl *decl) {
  layers.back().ordinary_decls[decl->name] = {ContextItemType::Function, decl};
}

void CompileContext::AddTypedef(TypedefDecl *decl) {
  layers.back().ordinary_decls[decl->name] = {ContextItemType::Typedef, decl};
}

void CompileContext::AddStruct(StructDecl *decl) {
  layers.back().tag_decls[decl->name] = {ContextItemType::Struct, decl};
}

void CompileContext::AddUnion(UnionDecl *decl) {
  layers.back().tag_decls[decl->name] = {ContextItemType::Union, decl};
}

void CompileContext::AddVariable(VarDecl *decl) {
  layers.back().ordinary_decls[decl->name] = {ContextItemType::Variable, decl};
}

void CompileContext::AddParameter(ParamDecl *decl) {
  if (decl->name.empty()) {
    return;
  }
  layers.back().ordinary_decls[decl->name] = {ContextItemType::Parameter, decl};
}

void CompileContext::AddEnumerator(EnumElemDecl *decl) {
  layers.back().ordinary_decls[decl->name] = {ContextItemType::Enumerator,
                                              decl};
}

void CompileContext::AddEnum(EnumDecl *decl) {
  layers.back().tag_decls[decl->name] = {ContextItemType::Enum, decl};
}

void CompileContext::AddSyntheticTypedef(std::string_view name, TypeRef type) {
  auto decl = std::make_unique<TypedefDecl>();
  decl->name = name;
  decl->type = type;
  AddTypedef(decl.get());
  _synthetic_typedefs.push_back(std::move(decl));
}

FunctionDecl *CompileContext::FindFunction(std::string_view name) const {
  auto *symbol = FindOrdinary(name);
  if (!symbol || symbol->type != ContextItemType::Function) {
    return nullptr;
  }
  return static_cast<FunctionDecl *>(symbol->decl);
}

TypedefDecl *CompileContext::FindTypedef(std::string_view name) const {
  auto *symbol = FindOrdinary(name);
  if (!symbol || symbol->type != ContextItemType::Typedef) {
    return nullptr;
  }
  return static_cast<TypedefDecl *>(symbol->decl);
}

StructDecl *CompileContext::FindStruct(std::string_view name) const {
  auto *symbol = FindTag(name);
  if (!symbol || symbol->type != ContextItemType::Struct) {
    return nullptr;
  }
  return static_cast<StructDecl *>(symbol->decl);
}

UnionDecl *CompileContext::FindUnion(std::string_view name) const {
  auto *symbol = FindTag(name);
  if (!symbol || symbol->type != ContextItemType::Union) {
    return nullptr;
  }
  return static_cast<UnionDecl *>(symbol->decl);
}

VarDecl *CompileContext::FindVariable(std::string_view name) const {
  auto *symbol = FindOrdinary(name);
  if (!symbol || symbol->type != ContextItemType::Variable) {
    return nullptr;
  }
  return static_cast<VarDecl *>(symbol->decl);
}

ParamDecl *CompileContext::FindParameter(std::string_view name) const {
  auto *symbol = FindOrdinary(name);
  if (!symbol || symbol->type != ContextItemType::Parameter) {
    return nullptr;
  }
  return static_cast<ParamDecl *>(symbol->decl);
}

EnumElemDecl *CompileContext::FindEnumerator(std::string_view name) const {
  auto *symbol = FindOrdinary(name);
  if (!symbol || symbol->type != ContextItemType::Enumerator) {
    return nullptr;
  }
  return static_cast<EnumElemDecl *>(symbol->decl);
}

EnumDecl *CompileContext::FindEnum(std::string_view name) const {
  auto *symbol = FindTag(name);
  if (!symbol || symbol->type != ContextItemType::Enum) {
    return nullptr;
  }
  return static_cast<EnumDecl *>(symbol->decl);
}

ValueDecl *CompileContext::FindValue(std::string_view name) const {
  auto *symbol = FindOrdinary(name);
  if (!symbol) {
    return nullptr;
  }
  switch (symbol->type) {
  case ContextItemType::Function:
  case ContextItemType::Variable:
  case ContextItemType::Parameter:
  case ContextItemType::Enumerator:
    return static_cast<ValueDecl *>(symbol->decl);
  default:
    return nullptr;
  }
}

ContextItemType CompileContext::GetTypeOf(std::string_view name) const {
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    if (auto found = it->ordinary_decls.find(name);
        found != it->ordinary_decls.end()) {
      return found->second.type;
    }
    if (auto found = it->tag_decls.find(name); found != it->tag_decls.end()) {
      return found->second.type;
    }
    if (std::find(it->labels.begin(), it->labels.end(), name) !=
        it->labels.end()) {
      return ContextItemType::Label;
    }
  }
  return ContextItemType::Unknown;
}

bool CompileContext::Is(std::string_view name, ContextItemType type) const {
  switch (type) {
  case ContextItemType::Function:
    return FindFunction(name) != nullptr;
  case ContextItemType::Typedef:
    return FindTypedef(name) != nullptr;
  case ContextItemType::Variable:
    return FindVariable(name) != nullptr;
  case ContextItemType::Parameter:
    return FindParameter(name) != nullptr;
  case ContextItemType::Enumerator:
    return FindEnumerator(name) != nullptr;
  case ContextItemType::Struct:
    return FindStruct(name) != nullptr;
  case ContextItemType::Union:
    return FindUnion(name) != nullptr;
  case ContextItemType::Enum:
    return FindEnum(name) != nullptr;
  case ContextItemType::Label:
    return GetTypeOf(name) == ContextItemType::Label;
  case ContextItemType::Unknown:
    return GetTypeOf(name) == ContextItemType::Unknown;
  }
  return false;
}

TypeRef CompileContext::GetType(const TypeSpecifier &spec) const {
  TypeRef result;
  if (spec.is_builtin) {
    result = Types().Builtin(spec.type_name);
  } else if (spec.is_struct) {
    if (auto *decl = FindStruct(spec.type_name)) {
      result = decl->type;
    }
  } else if (spec.is_union) {
    if (auto *decl = FindUnion(spec.type_name)) {
      result = decl->type;
    }
  } else if (spec.is_enum) {
    if (auto *decl = FindEnum(spec.type_name)) {
      result = decl->type;
    }
  } else if (spec.is_named_type) {
    if (auto *decl = FindTypedef(spec.type_name)) {
      result = decl->type;
    } else if (auto *decl = FindEnum(spec.type_name)) {
      result = decl->type;
    }
  }

  if (!result) {
    return nullptr;
  }
  if (spec.is_unsigned) {
    result = ApplyTypeTag(result, TypeTag::Unsigned);
  }
  if (spec.is_const) {
    result = ApplyTypeTag(result, TypeTag::Const);
  }
  return result;
}
