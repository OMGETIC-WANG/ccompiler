#include "Type.h"
#include "../Exception.h"
#include "Declaration.h"
#include <array>

struct DeclaratorParseContext {
  std::vector<std::pair<const TypeBase *, std::vector<std::string_view>>>
      function_parameter_names;

  void
  SetFunctionParameterNames(const TypeBase *type,
                            std::vector<std::string_view> parameter_names) {
    function_parameter_names.emplace_back(type, std::move(parameter_names));
  }

  std::vector<std::string_view>
  GetFunctionParameterNames(const TypeBase *type) const {
    for (auto it = function_parameter_names.rbegin();
         it != function_parameter_names.rend(); ++it) {
      if (it->first == type) {
        return it->second;
      }
    }
    return {};
  }
};

struct BuiltinTypeInfo {
  std::string_view name;
  std::string_view ir_name;
  std::optional<size_t> size;
  size_t align;
};

static constexpr std::array BUILTIN_TYPES = {
    BuiltinTypeInfo{"void", "void", std::nullopt, (size_t)-1},
    BuiltinTypeInfo{"int", "i32", 4, alignof(int)},
    BuiltinTypeInfo{"char", "i8", 1, alignof(char)},
    BuiltinTypeInfo{"float", "float", 4, alignof(float)},
    BuiltinTypeInfo{"double", "double", 8, alignof(double)},
    BuiltinTypeInfo{"long", "i64", 8, alignof(long)},
};

std::unique_ptr<BuiltinType> BuiltinType::Create(std::string_view name) {
  for (const auto &info : BUILTIN_TYPES) {
    if (info.name == name) {
      return std::make_unique<BuiltinType>(info.name, info.ir_name, info.size,
                                           info.align);
    }
  }
  RAISE_ERROR("Unknown builtin type ", name);
}

static std::unique_ptr<TypeBase>
CreateBaseType(const TypeSpecifier &specifier) {
  if (specifier.is_builtin) {
    return BuiltinType::Create(specifier.type);
  }

  OpaqueTypeKind kind = OpaqueTypeKind::Named;
  if (specifier.is_struct) {
    kind = OpaqueTypeKind::StructTag;
  } else if (specifier.is_enum) {
    kind = OpaqueTypeKind::EnumTag;
  }
  return std::make_unique<OpaqueType>(specifier.type, kind);
}

static ParsedType ParseDeclarator(std::unique_ptr<TypeBase> curr_type,
                                  const Declarator *D,
                                  DeclaratorParseContext &context) {
  size_t pointer_count = D->pointer_count;
  for (size_t i = 0; i < pointer_count; i++) {
    auto pointer = std::make_unique<PointerType>();
    pointer->pointee_type = std::move(curr_type);
    curr_type = std::move(pointer);
  }

  const DirectDeclarator *DD = D->direct_declarator.get();
  if (!DD) {
    return {
        .type = std::move(curr_type),
        .declarator_name = {},
        .parameter_names = {},
    };
  }

  for (const auto &suffix : DD->suffixes) {
    switch (suffix->GetType()) {
    case DeclType::ParamList: {
      const ParamList *param_list = (const ParamList *)suffix.get();

      auto func_type = std::make_unique<FunctionType>();
      func_type->return_type = std::move(curr_type);
      func_type->parameters.reserve(param_list->params.size());
      std::vector<std::string_view> parameter_names;
      parameter_names.reserve(param_list->params.size());
      for (const auto &param : param_list->params) {
        auto param_type = CreateBaseType(param.specifier.type_spec);
        if (!param.declarator) { // anonymous parameter
          func_type->parameters.push_back(std::move(param_type));
          parameter_names.push_back({});
          continue;
        }
        auto parsed_param = ParseDeclarator(std::move(param_type),
                                            param.declarator.get(), context);
        func_type->parameters.push_back(std::move(parsed_param.type));
        parameter_names.push_back(parsed_param.declarator_name);
      }
      context.SetFunctionParameterNames(func_type.get(),
                                        std::move(parameter_names));
      curr_type = std::move(func_type);
      break;
    }
    case DeclType::ArraySuffix: {
      const ArraySuffix *array_suffix = (const ArraySuffix *)suffix.get();

      auto array_type = std::make_unique<ArrayType>();
      array_type->element_type = std::move(curr_type);
      if (array_suffix->size != (size_t)-1) {
        array_type->size = array_suffix->size;
      }
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
    std::vector<std::string_view> parameter_names;
    if (curr_type->GetType() == TypeBaseType::FunctionType) {
      parameter_names = context.GetFunctionParameterNames(curr_type.get());
    }
    return {
        .type = std::move(curr_type),
        .declarator_name = ((const IdentifierDeclarator *)DD_D)->identifier,
        .parameter_names = std::move(parameter_names),
    };
  } else if (DD_D->GetType() == DeclType::Declarator) {
    return ParseDeclarator(std::move(curr_type), (const Declarator *)DD_D,
                           context);
  } else {
    RAISE_ERROR("Unexpected declarator type");
  }
}

static ParsedType ParseInitDeclarator(std::unique_ptr<TypeBase> curr_type,
                                      const InitDeclarator *init_declarator) {
  const Declarator *D = init_declarator->declarator.get();
  // TODO: add initializer parsing
  DeclaratorParseContext context;
  return ParseDeclarator(std::move(curr_type), D, context);
}

std::vector<ParsedType> ParseType(const Declaration *decl) {
  std::vector<ParsedType> results;
  if (decl->init_declarators.size()) {
    results.reserve(decl->init_declarators.size());
    for (const auto &init_declarator : decl->init_declarators) {
      results.push_back(ParseInitDeclarator(
          CreateBaseType(decl->declaration_specifier.type_spec),
          init_declarator.get()));
    }
  } else {
    results.reserve(1);
    results.push_back({
        .type = CreateBaseType(decl->declaration_specifier.type_spec),
        .declarator_name = {},
        .parameter_names = {},
    });
  }
  return results;
}

std::string_view TypeBase::GetNodeTypeName() const {
  return EnumRefl<TypeBaseType>::GetName(GetType());
}
