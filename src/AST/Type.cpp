#include "Type.h"
#include "../Exception.h"
#include "Context.h"
#include "Declaration.h"

namespace {

TypeRef ApplyConstQualifier(TypeRef type, TypeContext &types) {
  if (auto *array_type = type->DynCast<ArrayType>()) {
    TypeRef element_type = ApplyConstQualifier(array_type->element_type, types);
    return types.Array(element_type, array_type->size);
  }
  return ApplyTypeTag(type, TypeTag::Const);
}

TypeRef CreateBaseType(const TypeSpecifier &specifier, CompileContext &context,
                       bool is_tag_definition = false) {
  TypeContext &types = context.Types();
  TypeRef type;
  if (specifier.is_builtin) {
    type = types.Builtin(specifier.type_name);
    if (specifier.is_unsigned) {
      const auto *builtin = type->As<BuiltinType>();
      ASSERT(builtin->Kind() == BuiltinKind::Int ||
                 builtin->Kind() == BuiltinKind::Char ||
                 builtin->Kind() == BuiltinKind::Long,
             "unsigned is only valid for integer types");
      type = ApplyTypeTag(type, TypeTag::Unsigned);
    }
  } else if (specifier.is_struct) {
    ASSERT(!specifier.is_unsigned, "unsigned cannot qualify a struct type");
    if (is_tag_definition) {
      type = types.GetOrCreateStruct(specifier.scope_id, specifier.type_name);
    } else {
      if (auto existed = context.FindStruct(specifier.type_name)) {
        type = existed;
      } else {
        type = types.GetOrCreateStruct(specifier.scope_id, specifier.type_name);
      }
    }
  } else if (specifier.is_enum) {
    ASSERT(!specifier.is_unsigned, "unsigned cannot qualify an enum type");
    if (is_tag_definition) {
      type = types.GetOrCreateEnum(specifier.scope_id, specifier.type_name);
    } else {
      if (auto existed = context.FindEnum(specifier.type_name)) {
        type = existed;
      } else {
        type = types.GetOrCreateEnum(specifier.scope_id, specifier.type_name);
      }
    }
  } else if (specifier.is_named_type) {
    ASSERT(!specifier.is_unsigned,
           "unsigned cannot qualify a typedef in a type specifier");
    type = context.FindTypedef(specifier.type_name);
    if (type) {
      return type;
    }
    type = context.FindEnum(specifier.type_name);
    if (type) {
      return type;
    }
    ASSERT(type != nullptr, "Unknown type name ", specifier.type_name);
  } else {
    RAISE_ERROR("Unable to resolve type name ", specifier.type_name);
  }

  if (specifier.is_const) {
    type = ApplyConstQualifier(type, types);
  }
  return type;
}

static ParsedType
ParseDeclarator(TypeRef curr_type, const Declarator *declarator,
                CompileContext &context,
                std::vector<std::string_view> function_parameter_names = {}) {
  TypeContext &types = context.Types();

  if (!declarator->pointers.empty()) {
    for (const Declarator::PointerQualifier &pointer : declarator->pointers) {
      curr_type = types.Pointer(curr_type);
      if (pointer.is_const) {
        curr_type = ApplyTypeTag(curr_type, TypeTag::Const);
      }
    }
    function_parameter_names.clear();
  }

  const DirectDeclarator *direct = declarator->direct_declarator.get();
  if (!direct) {
    return {
        .type = curr_type,
        .declarator_name = {},
        .parameter_names = {},
    };
  }

  for (const auto &suffix : direct->suffixes) {
    switch (suffix->GetType()) {
    case DeclType::ParamList: {
      const auto *param_list = suffix->As<ParamList>();
      std::vector<TypeRef> parameter_types;
      std::vector<std::string_view> parameter_names;
      parameter_types.reserve(param_list->params.size());
      parameter_names.reserve(param_list->params.size());

      for (const auto &param : param_list->params) {
        TypeRef param_type = CreateBaseType(param.specifier.type_spec, context);
        if (!param.declarator) {
          parameter_types.push_back(param_type);
          parameter_names.push_back({});
          continue;
        }

        ParsedType parsed_param =
            ParseDeclarator(param_type, param.declarator.get(), context);
        parameter_types.push_back(parsed_param.type);
        parameter_names.push_back(parsed_param.declarator_name);
      }

      curr_type = types.Function(curr_type, parameter_types);
      curr_type->As<FunctionType>()->has_va_args = param_list->has_va_args;
      function_parameter_names = std::move(parameter_names);
      break;
    }
    case DeclType::ArraySuffix: {
      const auto *array_suffix = suffix->As<ArraySuffix>();
      std::optional<size_t> size;
      if (array_suffix->size != (size_t)-1) {
        size = array_suffix->size;
      }
      curr_type = types.Array(curr_type, size);
      function_parameter_names.clear();
      break;
    }
    default:
      RAISE_ERROR("Unexpected suffix");
    }
  }

  const DeclBase *direct_declarator = direct->declarator.get();
  if (!direct_declarator) {
    return {
        .type = curr_type,
        .declarator_name = {},
        .parameter_names = {},
    };
  }
  if (direct_declarator->GetType() == DeclType::IdentifierDeclarator) {
    const auto *identifier = direct_declarator->As<IdentifierDeclarator>();
    return {
        .type = curr_type,
        .declarator_name = identifier->identifier,
        .parameter_names = curr_type->Is<FunctionType>()
                               ? std::move(function_parameter_names)
                               : std::vector<std::string_view>{},
    };
  }
  if (direct_declarator->GetType() == DeclType::Declarator) {
    return ParseDeclarator(curr_type, direct_declarator->As<Declarator>(),
                           context, std::move(function_parameter_names));
  }
  RAISE_ERROR("Unexpected declarator type");
}

static ParsedType ParseInitDeclarator(TypeRef base_type,
                                      const InitDeclarator *init_declarator,
                                      CompileContext &context) {
  return ParseDeclarator(base_type, init_declarator->declarator.get(), context);
}

} // namespace

TypeRef ResolveTypeSpecifier(const TypeSpecifier &specifier,
                             CompileContext &context) {
  return CreateBaseType(specifier, context);
}

std::vector<ParsedType> ParseType(const Declaration *decl,
                                  CompileContext &context,
                                  bool is_tag_definition) {
  std::vector<ParsedType> results;
  TypeRef base_type = CreateBaseType(decl->declaration_specifier.type_spec,
                                     context, is_tag_definition);

  if (decl->init_declarators.size()) {
    results.reserve(decl->init_declarators.size());
    for (const auto &init_declarator : decl->init_declarators) {
      results.push_back(
          ParseInitDeclarator(base_type, init_declarator.get(), context));
    }
  } else {
    results.push_back({
        .type = base_type,
        .declarator_name = {},
        .parameter_names = {},
    });
  }
  return results;
}

std::string_view TypeBase::GetNodeTypeName() const {
  return EnumRefl<TypeBaseType>::GetName(GetType());
}
