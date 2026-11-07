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

TypeRef GetOrCreateBaseType(const TypeSpecifier &specifier,
                            CompileContext &context) {
  TypeContext &types = context.Types();
  TypeRef type;
  if (specifier.is_builtin) {
    type = types.Builtin(specifier.type_name);
    if (specifier.is_unsigned) {
      const auto *builtin = type->As<BuiltinType>();
      ASSERT(IsIntegerKind(builtin->Kind()),
             "unsigned is only valid for integer types");
      type = ApplyTypeTag(type, TypeTag::Unsigned);
    }
  } else if (specifier.is_struct) {
    ASSERT(!specifier.is_unsigned, "unsigned cannot qualify a struct type");
    type = types.GetOrCreateStruct(specifier.scope_id, specifier.type_name);
  } else if (specifier.is_union) {
    ASSERT(!specifier.is_unsigned, "unsigned cannot qualify a union type");
    type = types.GetOrCreateUnion(specifier.scope_id, specifier.type_name);
  } else if (specifier.is_enum) {
    ASSERT(!specifier.is_unsigned, "unsigned cannot qualify an enum type");
    type = types.GetOrCreateEnum(specifier.scope_id, specifier.type_name);
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

  for (auto suffix_it = direct->suffixes.rbegin();
       suffix_it != direct->suffixes.rend(); ++suffix_it) {
    const auto &suffix = *suffix_it;
    switch (suffix->GetType()) {
    case DeclType::ParamList: {
      const auto *param_list = suffix->As<ParamList>();
      std::vector<TypeRef> parameter_types;
      std::vector<std::string_view> parameter_names;
      parameter_types.reserve(param_list->params.size());
      parameter_names.reserve(param_list->params.size());

      for (const auto &param : param_list->params) {
        TypeRef param_type = context.GetType(param.specifier.type_spec);
        if (!param_type) {
          RAISE_ERROR("Unknown struct/union/enum: ",
                      param.specifier.type_spec.type_name);
        }
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

int IntegerConversionRank(BuiltinKind kind) {
  switch (kind) {
  case BuiltinKind::I1:
    return 0;
  case BuiltinKind::Char:
    return 1;
  case BuiltinKind::Short:
    return 2;
  case BuiltinKind::Int:
    return 3;
  case BuiltinKind::Long:
    return 4;
  case BuiltinKind::LongLong:
    return 5;
  default:
    RAISE_ERROR("Type does not have an integer conversion rank: ", kind);
  }
}

TypeRef PromoteInteger(TypeRef type, TypeContext &types) {
  ASSERT(IsIntegerType(type), "Integer promotion requires an integer type");
  type = StripTypeTag(type, TypeTag::Const);

  const auto *builtin = type->As<BuiltinType>();
  const BuiltinKind kind = builtin->Kind();
  if (kind != BuiltinKind::Char && kind != BuiltinKind::Short &&
      kind != BuiltinKind::I1) {
    return type;
  }

  const TypeRef int_type = types.Builtin(BuiltinKind::Int);
  if (HasTypeTag(type, TypeTag::Unsigned) && kind != BuiltinKind::I1 &&
      type->GetSize() >= int_type->GetSize()) {
    return ApplyTypeTag(types.Builtin(BuiltinKind::Int), TypeTag::Unsigned);
  }
  return int_type;
}

TypeRef UsualArithmeticType(TypeRef left, TypeRef right, TypeContext &types) {
  ASSERT(IsNumericType(left) && IsNumericType(right),
         "Usual arithmetic conversions require numeric operands");
  left = StripTypeTag(left, TypeTag::Const);
  right = StripTypeTag(right, TypeTag::Const);

  const auto *left_builtin = left->As<BuiltinType>();
  const auto *right_builtin = right->As<BuiltinType>();
  if (IsFloatingType(left) || IsFloatingType(right)) {
    if (left_builtin->Kind() == BuiltinKind::Double ||
        right_builtin->Kind() == BuiltinKind::Double) {
      return types.Builtin(BuiltinKind::Double);
    }
    return types.Builtin(BuiltinKind::Float);
  }

  left = PromoteInteger(left, types);
  right = PromoteInteger(right, types);
  if (left == right) {
    return left;
  }

  const auto *promoted_left = left->As<BuiltinType>();
  const auto *promoted_right = right->As<BuiltinType>();
  const int left_rank = IntegerConversionRank(promoted_left->Kind());
  const int right_rank = IntegerConversionRank(promoted_right->Kind());
  const bool left_unsigned = HasTypeTag(left, TypeTag::Unsigned);
  const bool right_unsigned = HasTypeTag(right, TypeTag::Unsigned);

  if (left_unsigned == right_unsigned) {
    return left_rank >= right_rank ? left : right;
  }

  TypeRef signed_type = left_unsigned ? right : left;
  TypeRef unsigned_type = left_unsigned ? left : right;
  const int signed_rank =
      IntegerConversionRank(signed_type->As<BuiltinType>()->Kind());
  const int unsigned_rank =
      IntegerConversionRank(unsigned_type->As<BuiltinType>()->Kind());

  if (unsigned_rank >= signed_rank) {
    return unsigned_type;
  }
  if (signed_type->GetSize() > unsigned_type->GetSize()) {
    return signed_type;
  }
  return ApplyTypeTag(types.Builtin(signed_type->As<BuiltinType>()->Kind()),
                      TypeTag::Unsigned);
}

TypeRef ResolveTypeSpecifier(const TypeSpecifier &specifier,
                             CompileContext &context) {
  return GetOrCreateBaseType(specifier, context);
}

std::vector<ParsedType> ParseType(const Declaration *decl,
                                  CompileContext &context) {
  std::vector<ParsedType> results;
  TypeRef base_type = context.GetType(decl->declaration_specifier.type_spec);

  if (!base_type) {
    RAISE_ERROR("Unknown struct/union/enum: ",
                decl->declaration_specifier.type_spec.type_name);
  }

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

TypeRef ParseType(FilteredLexer &lexer, CompileContext &context) {
  auto decl = ParseDeclaration(lexer, context);
  TypeRef base_type = context.GetType(decl->declaration_specifier.type_spec);
  if (!base_type) {
    RAISE_ERROR("Unknown struct/union/enum: ",
                decl->declaration_specifier.type_spec.type_name);
  }
  if (decl->init_declarators.size()) {
    ASSERT(decl->init_declarators.size() == 1, "Expected a single declarator");
    return ParseInitDeclarator(base_type, decl->init_declarators[0].get(),
                               context)
        .type;
  } else {
    return base_type;
  }
}

std::string_view TypeBase::GetNodeTypeName() const {
  return EnumRefl<TypeBaseType>::GetName(GetType());
}
