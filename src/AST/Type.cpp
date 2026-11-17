#include "Type.h"
#include "../Exception.h"
#include "AST.h"
#include "Context.h"
#include "Declaration.h"
#include "Diagnostics/Errors.generated.h"

namespace {

TypeRef ApplyCVQualifier(TypeRef type, TypeTag qualifier, TypeContext &types) {
  if (auto *array_type = type->DynCast<ArrayType>()) {
    TypeRef element_type =
        ApplyCVQualifier(array_type->element_type, qualifier, types);
    return types.Array(element_type, array_type->size);
  }
  return ApplyTypeTag(type, qualifier);
}

TypeRef GetOrCreateBaseType(const TypeSpecifier &specifier,
                            CompileContext &context) {
  TypeContext &types = context.Types();
  TypeRef type;
  if (specifier.is_builtin) {
    type = types.Builtin(specifier.type_name);
    if (specifier.is_unsigned) {
      const auto *builtin = type->As<BuiltinType>();
      if (!IsIntegerKind(builtin->Kind())) {
        context.Diagnostics().Fail(specifier.location,
                                   errs::UnsignedRequiresIntegerType{});
      }
      type = ApplyTypeTag(type, TypeTag::Unsigned);
    }
  } else if (specifier.is_struct) {
    if (specifier.is_unsigned) {
      context.Diagnostics().Fail(specifier.location,
                                 errs::UnsignedCannotQualifyStruct{});
    }
    type = types.GetOrCreateStruct(specifier.scope_id, specifier.type_name);
  } else if (specifier.is_union) {
    if (specifier.is_unsigned) {
      context.Diagnostics().Fail(specifier.location,
                                 errs::UnsignedCannotQualifyUnion{});
    }
    type = types.GetOrCreateUnion(specifier.scope_id, specifier.type_name);
  } else if (specifier.is_enum) {
    if (specifier.is_unsigned) {
      context.Diagnostics().Fail(specifier.location,
                                 errs::UnsignedCannotQualifyEnum{});
    }
    type = types.GetOrCreateEnum(specifier.scope_id, specifier.type_name);
  } else if (specifier.is_named_type) {
    if (specifier.is_unsigned) {
      context.Diagnostics().Fail(specifier.location,
                                 errs::UnsignedCannotQualifyTypedef{});
    }
    if (auto *decl = context.FindTypedef(specifier.type_name)) {
      type = decl->type;
    } else if (auto *decl = context.FindEnum(specifier.type_name)) {
      type = decl->type;
    } else {
      context.Diagnostics().Fail(specifier.location,
                                 errs::UnknownTypeName{specifier.type_name});
    }
  } else {
    context.Diagnostics().Fail(
        specifier.location, errs::UnableToResolveTypeName{specifier.type_name});
  }

  return ApplyTypeSpecifierQualifiers(type, specifier, types,
                                      context.Diagnostics());
}

static ParsedType
ParseDeclarator(TypeRef curr_type, const Declarator *declarator,
                CompileContext &context,
                std::vector<ParsedParameter> function_parameters = {}) {
  TypeContext &types = context.Types();

  if (!declarator->pointers.empty()) {
    for (const Declarator::PointerQualifier &pointer : declarator->pointers) {
      curr_type = types.Pointer(curr_type);
      if (pointer.is_const) {
        curr_type =
            ApplyTypeQualifier(curr_type, TypeTag::Const, types,
                               context.Diagnostics(), declarator->location);
      }
      if (pointer.is_volatile) {
        curr_type =
            ApplyTypeQualifier(curr_type, TypeTag::Volatile, types,
                               context.Diagnostics(), declarator->location);
      }
      if (pointer.is_restrict) {
        curr_type =
            ApplyTypeQualifier(curr_type, TypeTag::Restrict, types,
                               context.Diagnostics(), declarator->location);
      }
    }
    function_parameters.clear();
  }

  const DirectDeclarator *direct = declarator->direct_declarator.get();
  if (!direct) {
    return {
        .type = curr_type,
        .location = declarator->location,
        .declarator_name = {},
        .parameters = {},
    };
  }

  for (auto suffix_it = direct->suffixes.rbegin();
       suffix_it != direct->suffixes.rend(); ++suffix_it) {
    const auto &suffix = *suffix_it;
    switch (suffix->GetType()) {
    case DeclType::ParamList: {
      const auto *param_list = suffix->As<ParamList>();
      std::vector<TypeRef> parameter_types;
      std::vector<ParsedParameter> parameters;
      parameter_types.reserve(param_list->params.size());
      parameters.reserve(param_list->params.size());

      for (const auto &param : param_list->params) {
        TypeRef param_type = context.GetType(param.specifier.type_spec);
        if (!param_type) {
          context.Diagnostics().Fail(
              param.specifier.type_spec.location,
              errs::UnknownTagName{param.specifier.type_spec.type_name});
        }
        if (!param.declarator) {
          parameter_types.push_back(param_type);
          parameters.push_back({
              .type = param_type,
              .location = param.specifier.type_spec.location,
              .name = {},
          });
          continue;
        }

        ParsedType parsed_param =
            ParseDeclarator(param_type, param.declarator.get(), context);
        parameter_types.push_back(parsed_param.type);
        parameters.push_back({
            .type = parsed_param.type,
            .location = parsed_param.location,
            .name = parsed_param.declarator_name,
        });
      }

      curr_type = types.Function(curr_type, parameter_types);
      curr_type->As<FunctionType>()->has_va_args = param_list->has_va_args;
      function_parameters = std::move(parameters);
      break;
    }
    case DeclType::ArraySuffix: {
      const auto *array_suffix = suffix->As<ArraySuffix>();
      std::optional<size_t> size;
      if (array_suffix->size != (size_t)-1) {
        size = array_suffix->size;
      }
      curr_type = types.Array(curr_type, size);
      function_parameters.clear();
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
        .location = declarator->location,
        .declarator_name = {},
        .parameters = {},
    };
  }
  if (direct_declarator->GetType() == DeclType::IdentifierDeclarator) {
    const auto *identifier = direct_declarator->As<IdentifierDeclarator>();
    return {
        .type = curr_type,
        .location = declarator->location,
        .declarator_name = identifier->identifier,
        .parameters = curr_type->Is<FunctionType>()
                          ? std::move(function_parameters)
                          : std::vector<ParsedParameter>{},
    };
  }
  if (direct_declarator->GetType() == DeclType::Declarator) {
    return ParseDeclarator(curr_type, direct_declarator->As<Declarator>(),
                           context, std::move(function_parameters));
  }
  RAISE_ERROR("Unexpected declarator type");
}

static ParsedType ParseInitDeclarator(TypeRef base_type,
                                      const InitDeclarator *init_declarator,
                                      CompileContext &context) {
  return ParseDeclarator(base_type, init_declarator->declarator.get(), context);
}

} // namespace

TypeRef ApplyTypeQualifier(TypeRef type, TypeTag qualifier, TypeContext &types,
                           DiagnosticContext &diagnostics,
                           SourceLocation location) {
  if (qualifier == TypeTag::Const || qualifier == TypeTag::Volatile) {
    return ApplyCVQualifier(type, qualifier, types);
  }
  if (qualifier != TypeTag::Restrict) {
    RAISE_ERROR("Unexpected type qualifier");
  }
  if (!type->Is<PointerType>() ||
      type->As<PointerType>()->pointee_type->Is<FunctionType>()) {
    diagnostics.Fail(location, errs::RestrictRequiresObjectPointer{});
  }
  return ApplyTypeTag(type, TypeTag::Restrict);
}

TypeRef ApplyTypeSpecifierQualifiers(TypeRef type,
                                     const TypeSpecifier &specifier,
                                     TypeContext &types,
                                     DiagnosticContext &diagnostics) {
  if (specifier.is_const) {
    type = ApplyTypeQualifier(type, TypeTag::Const, types, diagnostics,
                              specifier.location);
  }
  if (specifier.is_volatile) {
    type = ApplyTypeQualifier(type, TypeTag::Volatile, types, diagnostics,
                              specifier.location);
  }
  if (specifier.is_restrict) {
    type = ApplyTypeQualifier(type, TypeTag::Restrict, types, diagnostics,
                              specifier.location);
  }
  return type;
}

TypeRef ApplyAggregateMemberQualifiers(TypeRef member_type,
                                       TypeRef aggregate_type,
                                       TypeContext &types) {
  if (HasTypeTag(aggregate_type, TypeTag::Const)) {
    member_type = ApplyCVQualifier(member_type, TypeTag::Const, types);
  }
  if (HasTypeTag(aggregate_type, TypeTag::Volatile)) {
    member_type = ApplyCVQualifier(member_type, TypeTag::Volatile, types);
  }
  return member_type;
}

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

int FloatingConversionRank(BuiltinKind kind) {
  switch (kind) {
  case BuiltinKind::Float:
    return 0;
  case BuiltinKind::Double:
    return 1;
  case BuiltinKind::LongDouble:
    return 2;
  default:
    RAISE_ERROR("Type does not have a floating conversion rank: ", kind);
  }
}

TypeRef PromoteInteger(TypeRef type, TypeContext &types) {
  ASSERT(IsIntegerType(type), "Integer promotion requires an integer type");
  type = StripTypeQualifiers(type);

  const auto *builtin = type->As<BuiltinType>();
  const BuiltinKind kind = builtin->Kind();
  if (kind != BuiltinKind::Char && kind != BuiltinKind::Short &&
      kind != BuiltinKind::I1) {
    return type;
  }

  const TypeRef int_type = types.getInt();
  if (HasTypeTag(type, TypeTag::Unsigned) && kind != BuiltinKind::I1 &&
      type->GetSize() >= int_type->GetSize()) {
    return ApplyTypeTag(types.getInt(), TypeTag::Unsigned);
  }
  return int_type;
}

TypeRef UsualArithmeticType(TypeRef left, TypeRef right, TypeContext &types) {
  ASSERT(IsNumericType(left) && IsNumericType(right),
         "Usual arithmetic conversions require numeric operands");
  left = StripTypeQualifiers(left);
  right = StripTypeQualifiers(right);

  const auto *left_builtin = left->As<BuiltinType>();
  const auto *right_builtin = right->As<BuiltinType>();
  if (IsFloatingType(left) || IsFloatingType(right)) {
    BuiltinKind left_kind = left_builtin->Kind();
    BuiltinKind right_kind = right_builtin->Kind();
    if (!IsFloatingKind(left_kind)) {
      return right;
    }
    if (!IsFloatingKind(right_kind)) {
      return left;
    }
    BuiltinKind common =
        FloatingConversionRank(left_kind) >= FloatingConversionRank(right_kind)
            ? left_kind
            : right_kind;
    return types.Builtin(common);
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
    context.Diagnostics().Fail(
        decl->declaration_specifier.type_spec.location,
        errs::UnknownTagName{decl->declaration_specifier.type_spec.type_name});
  }

  if (decl->init_declarators.size()) {
    results.reserve(decl->init_declarators.size());
    for (const auto &init_declarator : decl->init_declarators) {
      ParsedType parsed =
          ParseInitDeclarator(base_type, init_declarator.get(), context);
      SourceLocation declaration_start{
          .source_id = decl->location.source_id,
          .begin = decl->location.begin,
          .end = decl->location.begin,
      };
      parsed.location =
          CombineLocation(declaration_start, init_declarator->location);
      results.push_back(std::move(parsed));
    }
  } else {
    results.push_back({
        .type = base_type,
        .location = decl->location,
        .declarator_name = {},
        .parameters = {},
    });
  }
  return results;
}

TypeRef ParseType(Lexer &lexer, CompileContext &context) {
  auto decl = ParseDeclaration(lexer, context);
  TypeRef base_type = context.GetType(decl->declaration_specifier.type_spec);
  if (!base_type) {
    context.Diagnostics().Fail(
        decl->declaration_specifier.type_spec.location,
        errs::UnknownTagName{decl->declaration_specifier.type_spec.type_name});
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

std::string PointerType::ToString() const {
  return std::format("Ptr<{}>", pointee_type->ToString());
}

std::string ArrayType::ToString() const {
  if (size.has_value()) {
    return std::format("Array<{}, {}>", element_type->ToString(), *size);
  } else {
    return std::format("Array<{}, unknown>", element_type->ToString());
  }
}

std::string FunctionType::ToString() const {
  std::string result = std::format("Func<{}", return_type->ToString());
  for (const auto &param : parameters) {
    result += std::format(",{}", param->ToString());
  }
  if (has_va_args) {
    result += ",...";
  }
  result += ">";
  return result;
}

std::string StructType::ToString() const {
  return std::format("struct {}", type_name);
}

std::string UnionType::ToString() const {
  return std::format("union {}", type_name);
}

std::string EnumType::ToString() const {
  return std::format("enum {}", enum_name);
}
