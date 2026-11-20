#include "SemanticAnalysis.h"

#include "../AST/AST.h"
#include "../AST/Intrinsic.h"
#include "../AST/Operations.h"
#include "../AST/TranslationUnit.h"
#include "../AST/TypeContext.h"
#include "../Exception.h"
#include "../Util/Diagnostic.h"
#include "../Util/Util.h"
#include "Diagnostics/Errors.generated.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct ExprInfo {
  TypeRef type = nullptr;
  ValueCategory value_category = ValueCategory::RValue;
};

struct FlowContext {
  bool allows_break = false;
  bool allows_continue = false;
};

enum class Linkage { Internal, External };
enum class LinkageEntityKind { Object, Function };

struct ObjectInfo {
  TypeRef type = nullptr;
  Linkage linkage = Linkage::External;
  bool has_initialized_definition = false;
};

struct FunctionInfo {
  TypeRef type = nullptr;
  Linkage linkage = Linkage::External;
  bool has_definition = false;
};

bool IsIntegerOrEnum(TypeRef type) {
  return IsIntegerType(type) || type->Is<EnumType>();
}

bool IsScalar(TypeRef type) {
  return IsNumericType(type) || type->Is<PointerType>() || type->Is<EnumType>();
}

TypeRef Unqualified(TypeRef type) { return StripTypeQualifiers(type); }

TypeRef AdjustedType(TypeRef type, TypeContext &types) {
  if (auto *array = type->DynCast<ArrayType>()) {
    return types.Pointer(array->element_type);
  }
  if (type->Is<FunctionType>()) {
    return types.Pointer(type);
  }
  return type;
}

bool CompatibleFunctionTypes(TypeRef left, TypeRef right, TypeContext &types) {
  if (left == right) {
    return true;
  }
  if (!left->Is<FunctionType>() || !right->Is<FunctionType>()) {
    return false;
  }

  const auto *left_function = left->As<FunctionType>();
  const auto *right_function = right->As<FunctionType>();
  if (left_function->return_type != right_function->return_type ||
      left_function->has_va_args != right_function->has_va_args ||
      left_function->parameters.size() != right_function->parameters.size()) {
    return false;
  }
  for (size_t i = 0; i < left_function->parameters.size(); ++i) {
    TypeRef left_parameter =
        Unqualified(AdjustedType(left_function->parameters[i], types));
    TypeRef right_parameter =
        Unqualified(AdjustedType(right_function->parameters[i], types));
    if (left_parameter != right_parameter) {
      return false;
    }
  }
  return true;
}

TypeRef RepresentationType(TypeRef type) {
  if (auto *enum_type = type->DynCast<EnumType>()) {
    return enum_type->underlying_type ? enum_type->underlying_type : type;
  }
  return type;
}

bool HaveSameRepresentation(TypeRef left, TypeRef right) {
  left = RepresentationType(left);
  right = RepresentationType(right);
  if (left == right) {
    return true;
  }
  if (!left->Is<BuiltinType>() || !right->Is<BuiltinType>()) {
    return false;
  }
  return left->GetSize() == right->GetSize() &&
         IsIntegerType(left) == IsIntegerType(right) &&
         IsFloatingType(left) == IsFloatingType(right);
}

bool LosesPointerQualifiers(TypeRef from, TypeRef to) {
  if (!from->Is<PointerType>() || !to->Is<PointerType>()) {
    return false;
  }

  TypeRef from_pointee = from->As<PointerType>()->pointee_type;
  TypeRef to_pointee = to->As<PointerType>()->pointee_type;
  for (TypeTag qualifier : {TypeTag::Const, TypeTag::Volatile}) {
    if (HasTypeTag(from_pointee, qualifier) &&
        !HasTypeTag(to_pointee, qualifier)) {
      return true;
    }
  }
  return LosesPointerQualifiers(from_pointee, to_pointee);
}

uint64_t TruncateToWidth(uint64_t value, unsigned width) {
  if (width >= 64) {
    return value;
  }
  const uint64_t mask = (uint64_t{1} << width) - 1;
  return value & mask;
}

int64_t SignExtend(uint64_t value, unsigned width) {
  if (width >= 64) {
    return static_cast<int64_t>(value);
  }
  const uint64_t sign_bit = uint64_t{1} << (width - 1);
  const uint64_t mask = (uint64_t{1} << width) - 1;
  value &= mask;
  if ((value & sign_bit) != 0) {
    value |= ~mask;
  }
  return static_cast<int64_t>(value);
}

NumericValue ConvertConstant(NumericValue value, TypeRef type) {
  type = RepresentationType(type);
  if (type->Is<EnumType>()) {
    type = type->As<EnumType>()->underlying_type;
  }
  if (!type || !type->Is<BuiltinType>()) {
    return value;
  }

  BuiltinKind kind = type->As<BuiltinType>()->Kind();
  if (IsFloatingKind(kind)) {
    long double converted;
    if (std::holds_alternative<long double>(value)) {
      converted = std::get<long double>(value);
    } else if (std::holds_alternative<uint64_t>(value)) {
      converted = static_cast<long double>(std::get<uint64_t>(value));
    } else {
      converted = static_cast<long double>(std::get<int64_t>(value));
    }
    if (kind == BuiltinKind::Float) {
      return static_cast<long double>(static_cast<float>(converted));
    }
    if (kind == BuiltinKind::Double) {
      return static_cast<long double>(static_cast<double>(converted));
    }
    return converted;
  }
  if (!IsIntegerKind(kind)) {
    return value;
  }

  if (kind == BuiltinKind::I1) {
    const bool truth = std::visit(
        [](auto constant) {
          return constant != static_cast<decltype(constant)>(0);
        },
        value);
    if (HasTypeTag(type, TypeTag::Unsigned)) {
      return uint64_t{truth};
    }
    return int64_t{truth};
  }

  const unsigned width = static_cast<unsigned>(type->GetSize() * 8);
  uint64_t raw = 0;
  if (std::holds_alternative<long double>(value)) {
    raw = static_cast<uint64_t>(std::get<long double>(value));
  } else if (std::holds_alternative<uint64_t>(value)) {
    raw = std::get<uint64_t>(value);
  } else {
    raw = static_cast<uint64_t>(std::get<int64_t>(value));
  }

  raw = TruncateToWidth(raw, width);
  if (HasTypeTag(type, TypeTag::Unsigned)) {
    return raw;
  }
  return SignExtend(raw, width);
}

class SemanticAnalyzer {
public:
  SemanticAnalyzer(TranslationUnit &translation_unit, TypeContext &types,
                   DiagnosticContext &diagnostics)
      : translation_unit(translation_unit), types(types),
        diagnostics(diagnostics) {}

  std::optional<std::monostate> Run() {
    ASSERT(!translation_unit.is_semantically_analyzed,
           "Translation unit has already been analyzed");
    for (const auto &decl : translation_unit.decls) {
      TRY(AnalyzeTopLevelDecl(decl.get()));
    }
    translation_unit.is_semantically_analyzed = true;
    return std::monostate{};
  }

private:
  Expr *AsExpr(ASTNode *node) {
    auto *expr = node ? node->DynCast<Expr>() : nullptr;
    ASSERT(expr != nullptr, "Expected an expression AST node");
    return expr;
  }

  TypeRef BooleanType() const {
    return ApplyTypeTag(types.getI1(), TypeTag::Unsigned);
  }

  static bool IsBooleanType(TypeRef type) {
    return type->Is<BuiltinType>() &&
           type->As<BuiltinType>()->Kind() == BuiltinKind::I1;
  }

  static bool IsComparison(BinaryOpType op) {
    switch (op) {
    case BinaryOpType::Eq:
    case BinaryOpType::Ne:
    case BinaryOpType::Lt:
    case BinaryOpType::Gt:
    case BinaryOpType::Le:
    case BinaryOpType::Ge:
      return true;
    default:
      return false;
    }
  }

  static bool HasBooleanOperationResult(ASTNode *node) {
    if (auto *binary = node->DynCast<BinaryOp>()) {
      return IsComparison(binary->op_type) ||
             binary->op_type == BinaryOpType::And ||
             binary->op_type == BinaryOpType::Or;
    }
    if (auto *unary = node->DynCast<UnaryOp>()) {
      return unary->op_type == UnaryOpType::Not;
    }
    return false;
  }

  std::optional<ExprInfo> AnalyzeExpr(std::unique_ptr<ASTNode> &node) {
    ASSERT(node != nullptr, "Cannot analyze a null expression");
    TRY_ASSIGN_NEW(result, AnalyzeExprNode(node.get()));
    Expr *expr = AsExpr(node.get());
    expr->type = result.type;
    expr->value_category = result.value_category;
    if (auto value = EvaluateConstant(expr)) {
      expr->constant_value = std::move(value);
    }
    if (HasBooleanOperationResult(node.get())) {
      ASSERT(IsBooleanType(result.type),
             "Logical operation did not produce a boolean result");
      InsertImplicitCast(node, types.getInt());
      result = ExprInfo{types.getInt(), ValueCategory::RValue};
    }
    return result;
  }

  std::optional<ExprInfo> AnalyzeExprNode(ASTNode *node) {
    if (auto *literal = node->DynCast<LiteralNum>()) {
      literal->constant_value = literal->value;
      return ExprInfo{literal->type, ValueCategory::RValue};
    }
    if (auto *literal = node->DynCast<LiteralString>()) {
      TypeRef type =
          types.Array(types.getChar(), literal->value.view().size() + 1);
      return ExprInfo{type, ValueCategory::LValue};
    }
    if (auto *var_ref = node->DynCast<VarRef>()) {
      return AnalyzeVarRef(var_ref);
    }
    if (auto *binary = node->DynCast<BinaryOp>()) {
      return AnalyzeBinary(binary);
    }
    if (auto *type_cast = node->DynCast<TypeCast>()) {
      return AnalyzeTypeCast(type_cast);
    }
    if (auto *implicit_cast = node->DynCast<ImplicitCast>()) {
      return ExprInfo{implicit_cast->target_type, ValueCategory::RValue};
    }
    if (auto *get_field = node->DynCast<GetField>()) {
      return AnalyzeGetField(get_field);
    }
    if (auto *ptr_get_field = node->DynCast<PtrGetField>()) {
      return AnalyzePtrGetField(ptr_get_field);
    }
    if (auto *unary = node->DynCast<UnaryOp>()) {
      return AnalyzeUnary(unary);
    }
    if (auto *call = node->DynCast<FunctionInvocation>()) {
      return AnalyzeFunctionInvocation(call);
    }
    if (auto *intrinsic = node->DynCast<IntrinsicCall>()) {
      return AnalyzeIntrinsic(intrinsic);
    }
    if (auto *conditional = node->DynCast<ConditionalExpr>()) {
      return AnalyzeConditional(conditional);
    }
    if (auto *sequence = node->DynCast<SequenceEval>()) {
      diagnostics.Error(sequence->location, errs::UnsupportedSequenceEval{});
      return std::nullopt;
    }
    diagnostics.Error(node->location,
                      errs::UnsupportedExpressionASTNode{node->GetType()});
    return std::nullopt;
  }

  std::optional<ExprInfo> AnalyzeVarRef(VarRef *var_ref) {
    ValueDecl *decl = var_ref->decl;
    if (auto *var = decl->DynCast<VarDecl>()) {
      return ExprInfo{var->type, ValueCategory::LValue};
    }
    if (auto *param = decl->DynCast<ParamDecl>()) {
      return ExprInfo{AdjustedType(param->type, types), ValueCategory::LValue};
    }
    if (auto *function = decl->DynCast<FunctionDecl>()) {
      return ExprInfo{function->function_type,
                      ValueCategory::FunctionDesignator};
    }
    if (auto *enumerator = decl->DynCast<EnumElemDecl>()) {
      ASSERT(enumerator->type != nullptr,
             "Enum constant is missing its type after analysis");
      return ExprInfo{enumerator->type, ValueCategory::RValue};
    }
    diagnostics.Error(var_ref->location,
                      errs::DeclarationHasNoGeneratedValue{decl->name});
    return std::nullopt;
  }

  ExprInfo Decay(std::unique_ptr<ASTNode> &node, ExprInfo info) {
    if (auto *array = info.type->DynCast<ArrayType>()) {
      TypeRef pointer = types.Pointer(array->element_type);
      InsertImplicitCast(node, pointer);
      return ExprInfo{pointer, ValueCategory::RValue};
    }
    if (info.type->Is<FunctionType>()) {
      TypeRef pointer = types.Pointer(info.type);
      InsertImplicitCast(node, pointer);
      return ExprInfo{pointer, ValueCategory::RValue};
    }
    return ExprInfo{Unqualified(info.type), ValueCategory::RValue};
  }

  void InsertImplicitCast(std::unique_ptr<ASTNode> &node, TypeRef target_type) {
    auto cast = std::make_unique<ImplicitCast>(target_type);
    cast->location = node->location;
    cast->operand = std::move(node);
    cast->type = target_type;
    cast->value_category = ValueCategory::RValue;
    node = std::move(cast);
    if (auto value = EvaluateConstant(AsExpr(node.get()))) {
      AsExpr(node.get())->constant_value = std::move(value);
    }
  }

  std::optional<ExprInfo> ConvertExpr(std::unique_ptr<ASTNode> &node,
                                      ExprInfo info, TypeRef target_type,
                                      SourceLocation location) {
    info = Decay(node, info);
    info.type = Unqualified(info.type);
    target_type = Unqualified(target_type);
    if (info.type == target_type) {
      return ExprInfo{target_type, ValueCategory::RValue};
    }
    if (IsBooleanType(target_type)) {
      if (!IsScalar(info.type)) {
        diagnostics.Error(location, errs::BooleanConversionRequiresScalar{});
        return std::nullopt;
      }
      InsertImplicitCast(node, target_type);
      return ExprInfo{target_type, ValueCategory::RValue};
    }
    if (info.type->Is<PointerType>() && target_type->Is<PointerType>()) {
      if (LosesPointerQualifiers(info.type, target_type)) {
        diagnostics.Error(location,
                          errs::PointerAssignmentDiscardsQualifiers{});
        return std::nullopt;
      }
      InsertImplicitCast(node, target_type);
      return ExprInfo{target_type, ValueCategory::RValue};
    }
    if (IsNumericType(info.type) && IsNumericType(target_type)) {
      InsertImplicitCast(node, target_type);
      return ExprInfo{target_type, ValueCategory::RValue};
    }
    if (HaveSameRepresentation(info.type, target_type)) {
      InsertImplicitCast(node, target_type);
      return ExprInfo{target_type, ValueCategory::RValue};
    }
    diagnostics.Error(location, errs::TypeMismatch{
                                    target_type,
                                    info.type,
                                });
    return std::nullopt;
  }

  template <typename Error>
  std::optional<ExprInfo> ConvertToBool(std::unique_ptr<ASTNode> &node,
                                        ExprInfo info, SourceLocation location,
                                        Error error) {
    info = Decay(node, info);
    if (!IsScalar(info.type)) {
      diagnostics.Error(location, std::move(error));
      return std::nullopt;
    }
    TypeRef bool_type = BooleanType();
    if (info.type != bool_type) {
      InsertImplicitCast(node, bool_type);
    }
    return ExprInfo{bool_type, ValueCategory::RValue};
  }

  std::optional<ExprInfo> AnalyzeBinary(BinaryOp *binary) {
    BinaryOpType op = binary->op_type;
    ASSERT(!binary->compound_op || op == BinaryOpType::Assignment,
           "Compound operation attached to a non-assignment expression");
    if (op == BinaryOpType::Assignment && !binary->compound_op) {
      TRY_ASSIGN_NEW(left_info, AnalyzeExpr(binary->left));
      if (left_info.value_category != ValueCategory::LValue) {
        diagnostics.Error(binary->location, errs::AssignmentRequiresLValue{});
        return std::nullopt;
      }
      if (HasTypeTag(left_info.type, TypeTag::Const)) {
        diagnostics.Error(binary->location, errs::AssignmentToConst{});
        return std::nullopt;
      }
      TRY_ASSIGN_NEW(right_info, AnalyzeExpr(binary->right));
      TRY(ConvertExpr(binary->right, right_info, left_info.type,
                      binary->location));
      return ExprInfo{Unqualified(left_info.type), ValueCategory::RValue};
    }

    if (op == BinaryOpType::Subscript) {
      TRY_ASSIGN_NEW(left_info, AnalyzeExpr(binary->left));
      left_info = Decay(binary->left, left_info);
      TRY_ASSIGN_NEW(right_info, AnalyzeExpr(binary->right));
      right_info = Decay(binary->right, right_info);
      if (!IsIntegerType(right_info.type)) {
        diagnostics.Error(binary->location,
                          errs::SubscriptRequiresIntegerIndex{});
        return std::nullopt;
      }
      if (auto *pointer = left_info.type->DynCast<PointerType>()) {
        return ExprInfo{pointer->pointee_type, ValueCategory::LValue};
      }
      diagnostics.Error(binary->location,
                        errs::SubscriptRequiresArrayOrPointer{});
      return std::nullopt;
    }

    if (op == BinaryOpType::And || op == BinaryOpType::Or) {
      TRY_ASSIGN_NEW(left_info, AnalyzeExpr(binary->left));
      TRY_ASSIGN_NEW(right_info, AnalyzeExpr(binary->right));
      TRY(ConvertToBool(binary->left, left_info, binary->location,
                        errs::LogicalOperationsRequireScalars{}));
      TRY(ConvertToBool(binary->right, right_info, binary->location,
                        errs::LogicalOperationsRequireScalars{}));
      binary->operation_type = BooleanType();
      return ExprInfo{BooleanType(), ValueCategory::RValue};
    }

    const bool is_compound = binary->compound_op.has_value();
    TypeRef compound_left_type = nullptr;
    TRY_ASSIGN_NEW(left_info, AnalyzeExpr(binary->left));
    if (is_compound) {
      if (left_info.value_category != ValueCategory::LValue) {
        diagnostics.Error(binary->location, errs::AssignmentRequiresLValue{});
        return std::nullopt;
      }
      if (HasTypeTag(left_info.type, TypeTag::Const)) {
        diagnostics.Error(binary->location, errs::ModifyConst{});
        return std::nullopt;
      }
      left_info.type = Unqualified(left_info.type);
      compound_left_type = left_info.type;
    } else {
      left_info = Decay(binary->left, left_info);
    }

    TRY_ASSIGN_NEW(right_info, AnalyzeExpr(binary->right));
    if (!is_compound) {
      right_info = Decay(binary->right, right_info);
    }

    BinaryOpType base_op = is_compound ? *binary->compound_op : op;
    if (!is_compound &&
        (base_op == BinaryOpType::BitAnd || base_op == BinaryOpType::BitOr ||
         base_op == BinaryOpType::BitXor)) {
      if (!IsIntegerType(left_info.type)) {
        diagnostics.Error(binary->location, errs::BitwiseRequiresIntegerLeft{});
        return std::nullopt;
      }
      if (!IsIntegerType(right_info.type)) {
        diagnostics.Error(binary->location,
                          errs::BitwiseRequiresIntegerRight{});
        return std::nullopt;
      }
      TypeRef common =
          UsualArithmeticType(left_info.type, right_info.type, types);
      TRY(ConvertExpr(binary->left, left_info, common, binary->location));
      TRY(ConvertExpr(binary->right, right_info, common, binary->location));
      binary->operation_type = common;
      return ExprInfo{common, ValueCategory::RValue};
    }

    if (!is_compound &&
        (base_op == BinaryOpType::LShift || base_op == BinaryOpType::RShift)) {
      if (!IsIntegerType(left_info.type)) {
        diagnostics.Error(binary->location, errs::ShiftRequiresIntegerLeft{});
        return std::nullopt;
      }
      if (!IsIntegerType(right_info.type)) {
        diagnostics.Error(binary->location, errs::ShiftRequiresIntegerRight{});
        return std::nullopt;
      }
      TypeRef promoted = PromoteInteger(left_info.type, types);
      TRY(ConvertExpr(binary->left, left_info, promoted, binary->location));
      TRY(ConvertExpr(binary->right, right_info, promoted, binary->location));
      binary->operation_type = promoted;
      return ExprInfo{promoted, ValueCategory::RValue};
    }

    if (!is_compound && left_info.type->Is<PointerType>()) {
      if (!IsIntegerType(right_info.type)) {
        diagnostics.Error(binary->location,
                          errs::PointerArithmeticRequiresIntegerOffset{});
        return std::nullopt;
      }
      if (base_op != BinaryOpType::Add && base_op != BinaryOpType::Sub) {
        diagnostics.Error(binary->location,
                          errs::PointerArithmeticRequiresAddSub{});
        return std::nullopt;
      }
      binary->operation_type = left_info.type;
      return ExprInfo{left_info.type, ValueCategory::RValue};
    }

    if (!IsNumericType(left_info.type) || !IsNumericType(right_info.type)) {
      diagnostics.Error(binary->location,
                        errs::ArithmeticRequiresNumericOperands{});
      return std::nullopt;
    }
    TypeRef common =
        UsualArithmeticType(left_info.type, right_info.type, types);
    if (!is_compound) {
      TRY(ConvertExpr(binary->left, left_info, common, binary->location));
    }
    TRY(ConvertExpr(binary->right, right_info, common, binary->location));
    binary->operation_type = common;
    if (is_compound) {
      if (left_info.type != common) {
        binary->left_value_cast_type = common;
      }
      if (common != compound_left_type) {
        binary->result_cast_type = compound_left_type;
      }
    }

    TypeRef result_type = common;
    if (IsComparison(base_op)) {
      result_type = BooleanType();
    }
    if (is_compound) {
      result_type = compound_left_type;
    }
    return ExprInfo{result_type, ValueCategory::RValue};
  }

  std::optional<ExprInfo> AnalyzeUnary(UnaryOp *unary) {
    TRY_ASSIGN_NEW(operand, AnalyzeExpr(unary->operand));
    switch (unary->op_type) {
    case UnaryOpType::GetAddress: {
      if (operand.value_category != ValueCategory::LValue &&
          operand.value_category != ValueCategory::FunctionDesignator) {
        diagnostics.Error(unary->location, errs::AddressOfRequiresLValue{});
        return std::nullopt;
      }
      return ExprInfo{types.Pointer(operand.type), ValueCategory::RValue};
    }
    case UnaryOpType::Deref: {
      operand = Decay(unary->operand, operand);
      if (auto *pointer = operand.type->DynCast<PointerType>()) {
        return ExprInfo{pointer->pointee_type, ValueCategory::LValue};
      }
      diagnostics.Error(unary->location, errs::DereferenceRequiresPointer{});
      return std::nullopt;
    }
    case UnaryOpType::IncBeforeUse:
    case UnaryOpType::IncAfterUse:
    case UnaryOpType::DecBeforeUse:
    case UnaryOpType::DecAfterUse: {
      if (operand.value_category != ValueCategory::LValue) {
        diagnostics.Error(unary->location, errs::IncrementRequiresLValue{});
        return std::nullopt;
      }
      if (HasTypeTag(operand.type, TypeTag::Const)) {
        diagnostics.Error(unary->location, errs::ModifyConst{});
        return std::nullopt;
      }
      if (!IsNumericType(operand.type) && !operand.type->Is<PointerType>()) {
        diagnostics.Error(unary->location, errs::IncrementRequiresScalar{});
        return std::nullopt;
      }
      return ExprInfo{Unqualified(operand.type), ValueCategory::RValue};
    }
    case UnaryOpType::UnaryPlus:
    case UnaryOpType::UnaryMinus:
    case UnaryOpType::BitNot: {
      operand = Decay(unary->operand, operand);
      if (unary->op_type == UnaryOpType::BitNot) {
        if (!IsIntegerType(operand.type)) {
          diagnostics.Error(unary->location, errs::BitNotRequiresInteger{});
          return std::nullopt;
        }
      } else if (!IsNumericType(operand.type)) {
        diagnostics.Error(unary->location,
                          errs::UnaryArithmeticRequiresNumeric{});
        return std::nullopt;
      }
      TypeRef result_type = operand.type;
      if (IsIntegerType(operand.type)) {
        result_type = PromoteInteger(operand.type, types);
      }
      TRY(ConvertExpr(unary->operand, operand, result_type, unary->location));
      return ExprInfo{result_type, ValueCategory::RValue};
    }
    case UnaryOpType::Not: {
      TRY(ConvertToBool(unary->operand, operand, unary->location,
                        errs::LogicalNotRequiresScalar{}));
      return ExprInfo{BooleanType(), ValueCategory::RValue};
    }
    default:
      diagnostics.Error(unary->location,
                        errs::UnsupportedUnaryOperation{unary->op_type});
      return std::nullopt;
    }
  }

  std::optional<ExprInfo> AnalyzeTypeCast(TypeCast *type_cast) {
    TRY_ASSIGN_NEW(operand, AnalyzeExpr(type_cast->operand));
    operand = Decay(type_cast->operand, operand);
    TypeRef target = type_cast->target_type;
    if (operand.type != target) {
      if (operand.type->Is<PointerType>() && target->Is<PointerType>()) {
        if (LosesPointerQualifiers(operand.type, target)) {
          diagnostics.Error(type_cast->location,
                            errs::PointerCastDiscardsQualifiers{});
          return std::nullopt;
        }
      } else if (!IsNumericType(operand.type) || !IsNumericType(target)) {
        diagnostics.Error(type_cast->location,
                          errs::UnsupportedTypeCast{operand.type, target});
        return std::nullopt;
      }
    }
    return ExprInfo{target, ValueCategory::RValue};
  }

  std::optional<TypeRef> GetFieldType(TypeRef type, std::string_view field_name,
                                      SourceLocation location) {
    if (auto *struct_type = type->DynCast<StructType>()) {
      auto field = struct_type->GetField(field_name);
      if (!field) {
        diagnostics.Error(location, errs::UnknownStructField{field_name});
        return std::nullopt;
      }
      return ApplyAggregateMemberQualifiers(field->type, type, types);
    }
    if (auto *union_type = type->DynCast<UnionType>()) {
      auto field = union_type->GetField(field_name);
      if (!field) {
        diagnostics.Error(location, errs::UnknownUnionField{field_name});
        return std::nullopt;
      }
      return ApplyAggregateMemberQualifiers(field->type, type, types);
    }
    diagnostics.Error(location, errs::FieldAccessRequiresAggregate{});
    return std::nullopt;
  }

  std::optional<ExprInfo> AnalyzeGetField(GetField *get_field) {
    TRY_ASSIGN_NEW(operand, AnalyzeExpr(get_field->operand));
    TRY_ASSIGN_NEW(field, GetFieldType(operand.type, get_field->field_name,
                                       get_field->location));
    return ExprInfo{
        field,
        operand.value_category == ValueCategory::LValue ? ValueCategory::LValue
                                                        : ValueCategory::RValue,
    };
  }

  std::optional<ExprInfo> AnalyzePtrGetField(PtrGetField *ptr_get_field) {
    TRY_ASSIGN_NEW(operand, AnalyzeExpr(ptr_get_field->operand));
    operand = Decay(ptr_get_field->operand, operand);
    if (auto *pointer = operand.type->DynCast<PointerType>()) {
      TRY_ASSIGN_NEW(field, GetFieldType(pointer->pointee_type,
                                         ptr_get_field->field_name,
                                         ptr_get_field->location));
      return ExprInfo{field, ValueCategory::LValue};
    }
    diagnostics.Error(ptr_get_field->location,
                      errs::PointerFieldAccessRequiresPointer{});
    return std::nullopt;
  }

  std::optional<ExprInfo> AnalyzeFunctionInvocation(FunctionInvocation *call) {
    TRY_ASSIGN_NEW(function, AnalyzeExpr(call->function));
    function = Decay(call->function, function);
    TypeRef function_type = function.type;
    if (auto *pointer = function_type->DynCast<PointerType>()) {
      function_type = pointer->pointee_type;
    }
    if (!function_type->Is<FunctionType>()) {
      diagnostics.Error(call->location, errs::InvocationRequiresFunction{});
      return std::nullopt;
    }

    const auto *signature = function_type->As<FunctionType>();
    if (signature->has_va_args) {
      if (call->arguments.size() < signature->parameters.size()) {
        diagnostics.Error(call->location, errs::VariadicArgumentCountMismatch{
                                              signature->parameters.size()});
        return std::nullopt;
      }
    } else if (call->arguments.size() != signature->parameters.size()) {
      diagnostics.Error(call->location, errs::ArgumentCountMismatch{
                                            signature->parameters.size()});
      return std::nullopt;
    }

    for (size_t i = 0; i < call->arguments.size(); ++i) {
      TRY_ASSIGN_NEW(argument, AnalyzeExpr(call->arguments[i]));
      if (i < signature->parameters.size()) {
        TypeRef parameter_type = AdjustedType(signature->parameters[i], types);
        TRY(ConvertExpr(call->arguments[i], argument, parameter_type,
                        call->location));
      } else {
        argument = Decay(call->arguments[i], argument);
        if (IsIntegerType(argument.type)) {
          TypeRef promoted = PromoteInteger(argument.type, types);
          TRY(ConvertExpr(call->arguments[i], argument, promoted,
                          call->location));
        } else if (IsFloatingType(argument.type) &&
                   argument.type->As<BuiltinType>()->Kind() ==
                       BuiltinKind::Float) {
          TRY(ConvertExpr(call->arguments[i], argument, types.getDouble(),
                          call->location));
        }
      }
    }
    return ExprInfo{signature->return_type, ValueCategory::RValue};
  }

  std::optional<ExprInfo> AnalyzeIntrinsic(IntrinsicCall *call) {
    ASSERT(call->desc != nullptr, "Intrinsic call is missing its descriptor");
    for (auto &arg : call->args) {
      if (auto *expression = std::get_if<std::unique_ptr<ASTNode>>(&arg)) {
        TRY(AnalyzeExpr(*expression));
      }
    }
    ASSERT(call->desc->type_evaluator != nullptr,
           "Intrinsic has no frontend type evaluator: ", call->desc->name);
    TypeRef result = call->desc->type_evaluator(call->args, types);
    if (call->desc->name == "sizeof") {
      TypeRef operand_type;
      if (auto *type = std::get_if<TypeRef>(&call->args[0])) {
        operand_type = *type;
      } else {
        auto *expression =
            std::get<std::unique_ptr<ASTNode>>(call->args[0]).get();
        operand_type = AsExpr(expression)->type;
      }
      bool complete = true;
      if (auto *array = operand_type->DynCast<ArrayType>()) {
        complete = array->size.has_value();
      } else if (auto *struct_type = operand_type->DynCast<StructType>()) {
        complete = struct_type->is_complete;
      } else if (auto *union_type = operand_type->DynCast<UnionType>()) {
        complete = union_type->is_complete;
      } else if (operand_type->Is<FunctionType>() ||
                 (operand_type->Is<BuiltinType>() &&
                  operand_type->As<BuiltinType>()->Kind() ==
                      BuiltinKind::Void)) {
        complete = false;
      }
      if (!complete) {
        diagnostics.Error(call->location, errs::SizeofRequiresCompleteObject{});
        return std::nullopt;
      }
    }
    return ExprInfo{result, ValueCategory::RValue};
  }

  std::optional<ExprInfo> AnalyzeConditional(ConditionalExpr *conditional) {
    TRY_ASSIGN_NEW(condition, AnalyzeExpr(conditional->condition));
    TRY(ConvertToBool(conditional->condition, condition, conditional->location,
                      errs::ConditionalRequiresScalarCondition{}));

    TRY_ASSIGN_NEW(true_info, AnalyzeExpr(conditional->true_expr));
    TRY_ASSIGN_NEW(false_info, AnalyzeExpr(conditional->false_expr));
    true_info = Decay(conditional->true_expr, true_info);
    false_info = Decay(conditional->false_expr, false_info);
    if (!IsNumericType(true_info.type) || !IsNumericType(false_info.type)) {
      diagnostics.Error(conditional->location,
                        errs::ConditionalRequiresNumericOperands{});
      return std::nullopt;
    }
    TypeRef common =
        UsualArithmeticType(true_info.type, false_info.type, types);
    TRY(ConvertExpr(conditional->true_expr, true_info, common,
                    conditional->location));
    TRY(ConvertExpr(conditional->false_expr, false_info, common,
                    conditional->location));
    return ExprInfo{common, ValueCategory::RValue};
  }

  std::optional<std::monostate> AnalyzeDeclStmt(DeclStmt *stmt, bool global) {
    for (const auto &decl : stmt->decls) {
      TRY(AnalyzeDeclaration(decl.get(), global));
    }
    return std::monostate{};
  }

  std::optional<std::monostate> AnalyzeDeclaration(ASTNode *node, bool global) {
    if (auto *var = node->DynCast<VarDecl>()) {
      return AnalyzeVarDecl(var, global);
    }
    if (auto *function = node->DynCast<FunctionDecl>()) {
      return AnalyzeFunctionDecl(function, global);
    }
    if (auto *enum_decl = node->DynCast<EnumDecl>()) {
      return AnalyzeEnumDecl(enum_decl);
    }
    if (node->Is<StructDecl>() || node->Is<UnionDecl>() ||
        node->Is<TypedefDecl>() || node->Is<ParamDecl>()) {
      return std::monostate{};
    }
    if (auto *stmt = node->DynCast<DeclStmt>()) {
      return AnalyzeDeclStmt(stmt, global);
    }
    diagnostics.Error(node->location,
                      errs::UnsupportedDeclarationASTNode{node->GetType()});
    return std::nullopt;
  }

  std::optional<std::monostate> AnalyzeTopLevelDecl(ASTNode *node) {
    switch (node->GetType()) {
    case ASTNodeType::FunctionDecl:
    case ASTNodeType::VarDecl:
    case ASTNodeType::StructDecl:
    case ASTNodeType::UnionDecl:
    case ASTNodeType::EnumDecl:
    case ASTNodeType::TypedefDecl:
    case ASTNodeType::DeclStmt:
      return AnalyzeDeclaration(node, true);
    default:
      diagnostics.Error(node->location,
                        errs::UnsupportedTopLevelASTNode{node->GetType()});
      return std::nullopt;
    }
  }

  std::optional<std::monostate>
  CheckLinkageEntityKind(std::string_view name, LinkageEntityKind kind,
                         SourceLocation location) {
    auto [found, inserted] = linkage_kinds.try_emplace(name, kind);
    if (!inserted && found->second != kind) {
      diagnostics.Error(location,
                        errs::ConflictingObjectAndFunctionDeclaration{name});
      return std::nullopt;
    }
    return std::monostate{};
  }

  std::optional<std::monostate> AnalyzeFunctionDecl(FunctionDecl *function,
                                                    bool global) {
    if (!global && function->storage_class == StorageClass::Static) {
      diagnostics.Error(function->location,
                        errs::StaticBlockFunctionForbidden{});
      return std::nullopt;
    }
    if (!global && function->body) {
      diagnostics.Error(function->location,
                        errs::FunctionDefinitionRequiresFileScope{});
      return std::nullopt;
    }
    ASSERT(function->function_type != nullptr,
           "Function declaration is missing its type");
    ASSERT(function->function_type->Is<FunctionType>(),
           "Function declaration has a non-function type");

    TRY(CheckLinkageEntityKind(function->name, LinkageEntityKind::Function,
                               function->location));
    auto found = functions.find(function->name);
    const Linkage requested_linkage =
        function->storage_class == StorageClass::Static ? Linkage::Internal
                                                        : Linkage::External;
    if (found != functions.end()) {
      FunctionInfo &previous = found->second;
      if (!CompatibleFunctionTypes(function->function_type, previous.type,
                                   types)) {
        diagnostics.Error(function->location,
                          errs::ConflictingFunctionDeclaration{function->name});
        return std::nullopt;
      }
      if (function->storage_class == StorageClass::Static &&
          previous.linkage == Linkage::External) {
        diagnostics.Error(function->location,
                          errs::StaticFunctionFollowsExternal{function->name});
        return std::nullopt;
      }
      if (previous.linkage == Linkage::Internal &&
          function->storage_class != StorageClass::Extern) {
        diagnostics.Error(
            function->location,
            errs::FunctionFollowsStaticWithoutExtern{function->name});
        return std::nullopt;
      }
    } else {
      functions.emplace(function->name, FunctionInfo{
                                            .type = function->function_type,
                                            .linkage = requested_linkage,
                                            .has_definition = false,
                                        });
    }

    if (!function->body) {
      return std::monostate{};
    }
    FunctionInfo &info = functions.at(function->name);
    if (info.has_definition) {
      diagnostics.Error(function->location,
                        errs::MultipleFunctionDefinitions{function->name});
      return std::nullopt;
    }
    info.has_definition = true;

    FunctionDecl *previous_function = current_function;
    current_function = function;
    for (auto &statement : function->body->statements) {
      if (!AnalyzeStatement(statement)) {
        current_function = previous_function;
        return std::nullopt;
      }
    }
    current_function = previous_function;
    return std::monostate{};
  }

  std::optional<std::monostate> AnalyzeEnumDecl(EnumDecl *decl) {
    if (!decl->is_definition) {
      return std::monostate{};
    }
    auto *enum_type = decl->type->As<EnumType>();
    if (enum_type->is_complete) {
      return std::monostate{};
    }

    std::vector<EnumValue> values;
    values.reserve(decl->values.size());
    for (size_t i = 0; i < decl->values.size(); ++i) {
      EnumElemDecl *element = decl->values[i].get();
      element->type = decl->type;
      NumericValue value;
      if (element->value) {
        TRY(AnalyzeExpr(element->value));
        Expr *expression = AsExpr(element->value.get());
        if (!expression->constant_value) {
          diagnostics.Error(element->location,
                            errs::EnumValueRequiresConstant{});
          return std::nullopt;
        }
        value =
            ConvertConstant(*expression->constant_value, decl->underlying_type);
        if (std::holds_alternative<long double>(value)) {
          diagnostics.Error(element->location, errs::EnumValueCannotBeFloat{});
          return std::nullopt;
        }
      } else if (i == 0) {
        value = int64_t{0};
      } else {
        value = std::visit(
            [](auto previous) -> NumericValue {
              return static_cast<decltype(previous)>(previous + 1);
            },
            values.back().value);
      }
      element->constant_value = value;
      std::variant<int64_t, uint64_t> enum_value;
      if (std::holds_alternative<int64_t>(value)) {
        enum_value = std::get<int64_t>(value);
      } else if (std::holds_alternative<uint64_t>(value)) {
        enum_value = std::get<uint64_t>(value);
      } else {
        RAISE_ERROR("Floating point enum value escaped validation");
      }
      values.push_back(
          {.name = std::string(element->name), .value = enum_value});
    }
    types.CompleteEnum(decl->type, decl->underlying_type, values);
    return std::monostate{};
  }

  std::optional<std::monostate> AnalyzeLinkedObject(VarDecl *var, TypeRef type,
                                                    bool global) {
    TRY(CheckLinkageEntityKind(var->name, LinkageEntityKind::Object,
                               var->location));

    auto found = objects.find(var->name);
    if (found != objects.end()) {
      ObjectInfo &previous = found->second;
      if (previous.type != type) {
        diagnostics.Error(var->location,
                          errs::ConflictingVariableDeclaration{var->name});
        return std::nullopt;
      }
      if (var->storage_class == StorageClass::Static &&
          previous.linkage == Linkage::External) {
        diagnostics.Error(var->location,
                          errs::StaticVariableFollowsExternal{var->name});
        return std::nullopt;
      }
      if (previous.linkage == Linkage::Internal &&
          var->storage_class != StorageClass::Extern) {
        diagnostics.Error(var->location,
                          errs::VariableFollowsStaticWithoutExtern{var->name});
        return std::nullopt;
      }
    } else {
      const Linkage linkage = var->storage_class == StorageClass::Static
                                  ? Linkage::Internal
                                  : Linkage::External;
      found = objects
                  .emplace(var->name,
                           ObjectInfo{
                               .type = type,
                               .linkage = linkage,
                           })
                  .first;
    }

    if (!global) {
      return std::monostate{};
    }

    ObjectInfo &info = found->second;
    const bool has_initialized_definition = var->initializer != nullptr;
    if (has_initialized_definition && info.has_initialized_definition) {
      diagnostics.Error(var->location,
                        errs::MultipleVariableDefinitions{var->name});
      return std::nullopt;
    }
    info.has_initialized_definition |= has_initialized_definition;
    return std::monostate{};
  }

  std::optional<std::monostate> AnalyzeVarDecl(VarDecl *var, bool global) {
    if (!global && var->storage_class == StorageClass::Static) {
      diagnostics.Error(var->location, errs::LocalStaticUnsupported{});
      return std::nullopt;
    }
    if (!global && var->storage_class == StorageClass::Extern &&
        var->initializer) {
      diagnostics.Error(var->location,
                        errs::BlockExternCannotHaveInitializer{});
      return std::nullopt;
    }
    TypeRef type = var->type;
    if (auto *array = type->DynCast<ArrayType>();
        array && !array->size.has_value()) {
      std::optional<size_t> inferred_size;
      if (!var->initializer) {
        if (global) {
          diagnostics.Error(var->location,
                            errs::GlobalArraySizeNeedsInitializer{var->name});
          return std::nullopt;
        }
        diagnostics.Error(var->location, errs::ArraySizeNeedsInitializer{});
        return std::nullopt;
      }
      if (auto *array_init = var->initializer->DynCast<ArrayInitializer>()) {
        inferred_size = array_init->initializers.size();
      } else if (auto *value_init =
                     var->initializer->DynCast<ValueInitializer>()) {
        if (auto *literal = value_init->value->DynCast<LiteralString>()) {
          if (!array->element_type->Is<BuiltinType>() ||
              array->element_type->As<BuiltinType>()->Kind() !=
                  BuiltinKind::Char) {
            diagnostics.Error(var->location, errs::StringArrayRequiresChar{});
            return std::nullopt;
          }
          inferred_size = literal->value.view().size() + 1;
        }
      }
      if (!inferred_size) {
        if (global) {
          diagnostics.Error(var->location,
                            errs::GlobalArraySizeNeedsInitializer{var->name});
          return std::nullopt;
        }
        diagnostics.Error(var->initializer->location,
                          errs::ArrayInitializerRequiresListOrString{});
        return std::nullopt;
      }
      TypeRef completed = types.Array(array->element_type, *inferred_size);
      completed.set_tag(type.get_tag());
      var->type = completed;
      type = completed;
    }

    if (global || var->storage_class == StorageClass::Extern) {
      TRY(AnalyzeLinkedObject(var, type, global));
    }
    if (var->initializer) {
      TRY(AnalyzeInitializer(var->initializer.get(), type, var->location,
                             global));
      if (global && !IsConstantFoldable(var->initializer.get())) {
        diagnostics.Error(var->location,
                          errs::GlobalInitializerRequiresConstant{});
        return std::nullopt;
      }
    }
    return std::monostate{};
  }

  std::optional<std::monostate>
  AnalyzeInitializer(Initializer *initializer, TypeRef type,
                     SourceLocation declaration_location, bool global) {
    if (auto *array_type = type->DynCast<ArrayType>()) {
      if (auto *array_init = initializer->DynCast<ArrayInitializer>()) {
        size_t size =
            array_type->size.value_or(array_init->initializers.size());
        if (array_init->initializers.size() > size) {
          diagnostics.Error(array_init->location,
                            errs::TooManyArrayInitializers{});
          return std::nullopt;
        }
        for (const auto &element : array_init->initializers) {
          TRY(AnalyzeInitializer(element.get(), array_type->element_type,
                                 declaration_location, global));
        }
        return std::monostate{};
      }
      if (auto *value_init = initializer->DynCast<ValueInitializer>()) {
        if (auto *literal = value_init->value->DynCast<LiteralString>();
            literal && array_type->element_type->Is<BuiltinType>() &&
            array_type->element_type->As<BuiltinType>()->Kind() ==
                BuiltinKind::Char) {
          if (array_type->size.value() < literal->value.view().size() + 1) {
            diagnostics.Error(literal->location,
                              errs::StringLiteralExceedsArraySize{});
            return std::nullopt;
          }
          TRY(AnalyzeExpr(value_init->value));
          return std::monostate{};
        }
      }
      diagnostics.Error(initializer->location, errs::UnexpectedInitializer{});
      return std::nullopt;
    }

    if (auto *struct_type = type->DynCast<StructType>()) {
      if (auto *field_init = initializer->DynCast<FieldInitializer>()) {
        for (auto &[name, field_initializer] : field_init->field_initializers) {
          auto field = struct_type->GetField(name);
          if (!field) {
            diagnostics.Error(field_init->location,
                              errs::UnknownInitializerField{name});
            return std::nullopt;
          }
          TRY(AnalyzeInitializer(field_initializer.get(), field->type,
                                 declaration_location, global));
        }
        return std::monostate{};
      }
      if (auto *array_init = initializer->DynCast<ArrayInitializer>()) {
        if (array_init->initializers.size() > struct_type->fields.size()) {
          diagnostics.Error(array_init->location,
                            errs::TooManyStructInitializers{});
          return std::nullopt;
        }
        for (size_t i = 0; i < array_init->initializers.size(); ++i) {
          TRY(AnalyzeInitializer(array_init->initializers[i].get(),
                                 struct_type->fields[i].type,
                                 declaration_location, global));
        }
        return std::monostate{};
      }
      if (auto *value_init = initializer->DynCast<ValueInitializer>()) {
        TRY_ASSIGN_NEW(value, AnalyzeExpr(value_init->value));
        TRY(ConvertExpr(value_init->value, value, type, value_init->location));
        return std::monostate{};
      }
    }

    if (type->Is<BuiltinType>()) {
      auto *value_init = initializer->DynCast<ValueInitializer>();
      if (!value_init) {
        diagnostics.Error(initializer->location,
                          errs::BuiltinInitializerRequiresValue{});
        return std::nullopt;
      }
      TRY_ASSIGN_NEW(value, AnalyzeExpr(value_init->value));
      TRY(ConvertExpr(value_init->value, value, type, value_init->location));
      return std::monostate{};
    }

    if (auto *pointer = type->DynCast<PointerType>()) {
      auto *value_init = initializer->DynCast<ValueInitializer>();
      if (!value_init) {
        diagnostics.Error(initializer->location,
                          errs::PointerInitializerRequiresValue{});
        return std::nullopt;
      }
      TRY_ASSIGN_NEW(value, AnalyzeExpr(value_init->value));
      value = Decay(value_init->value, value);
      if (global) {
        if (!AsExpr(value_init->value.get())->constant_value) {
          diagnostics.Error(value_init->location,
                            errs::GlobalPointerRequiresConstant{});
          return std::nullopt;
        }
        if (!value.type->Is<PointerType>()) {
          if (!IsIntegerType(value.type) && !value.type->Is<EnumType>()) {
            diagnostics.Error(value_init->location,
                              errs::TypeMismatch{pointer, value.type});
            return std::nullopt;
          }
          InsertImplicitCast(value_init->value, pointer);
        }
      } else {
        TRY(ConvertExpr(value_init->value, value, pointer,
                        value_init->location));
      }
      return std::monostate{};
    }

    if (!global && (type->Is<EnumType>() || type->Is<UnionType>())) {
      auto *value_init = initializer->DynCast<ValueInitializer>();
      if (!value_init) {
        diagnostics.Error(initializer->location, errs::UnexpectedInitializer{});
        return std::nullopt;
      }
      TRY_ASSIGN_NEW(value, AnalyzeExpr(value_init->value));
      TRY(ConvertExpr(value_init->value, value, type, value_init->location));
      return std::monostate{};
    }

    diagnostics.Error(initializer->location, errs::UnexpectedInitializer{});
    return std::nullopt;
  }

  std::optional<std::monostate>
  AnalyzeStatement(std::unique_ptr<ASTNode> &node) {
    if (!node) {
      return std::monostate{};
    }
    ASTNode *current = node.get();
    if (current->Is<EmptyStmt>() || current->Is<LabelStmt>() ||
        current->Is<GotoStmt>() || current->Is<TypedefDecl>()) {
      return std::monostate{};
    }
    if (auto *decl_stmt = current->DynCast<DeclStmt>()) {
      return AnalyzeDeclStmt(decl_stmt, false);
    }
    if (auto *var = current->DynCast<VarDecl>()) {
      return AnalyzeVarDecl(var, false);
    }
    if (auto *function = current->DynCast<FunctionDecl>()) {
      return AnalyzeFunctionDecl(function, false);
    }
    if (auto *struct_decl = current->DynCast<StructDecl>()) {
      (void)struct_decl;
      return std::monostate{};
    }
    if (auto *union_decl = current->DynCast<UnionDecl>()) {
      (void)union_decl;
      return std::monostate{};
    }
    if (auto *enum_decl = current->DynCast<EnumDecl>()) {
      return AnalyzeEnumDecl(enum_decl);
    }
    if (auto *compound = current->DynCast<CompoundStmt>()) {
      for (auto &statement : compound->statements) {
        TRY(AnalyzeStatement(statement));
      }
      return std::monostate{};
    }
    if (auto *if_stmt = current->DynCast<IfStmt>()) {
      TRY(AnalyzeCondition(if_stmt->condition));
      TRY(AnalyzeStatement(if_stmt->then_branch));
      TRY(AnalyzeStatement(if_stmt->else_branch));
      return std::monostate{};
    }
    if (auto *while_stmt = current->DynCast<WhileStmt>()) {
      TRY(AnalyzeCondition(while_stmt->condition));
      flow_contexts.push_back({.allows_break = true, .allows_continue = true});
      std::optional<std::monostate> body_status =
          AnalyzeStatement(while_stmt->body);
      flow_contexts.pop_back();
      return body_status;
    }
    if (auto *do_while = current->DynCast<DoWhileStmt>()) {
      flow_contexts.push_back({.allows_break = true, .allows_continue = true});
      std::optional<std::monostate> body_status =
          AnalyzeStatement(do_while->body);
      flow_contexts.pop_back();
      if (!body_status || !AnalyzeCondition(do_while->condition)) {
        return std::nullopt;
      }
      return std::monostate{};
    }
    if (auto *for_stmt = current->DynCast<ForStmt>()) {
      TRY(AnalyzeStatement(for_stmt->init));
      TRY(AnalyzeCondition(for_stmt->condition));
      TRY(AnalyzeStatement(for_stmt->increment));
      flow_contexts.push_back({.allows_break = true, .allows_continue = true});
      std::optional<std::monostate> body_status =
          AnalyzeStatement(for_stmt->body);
      flow_contexts.pop_back();
      return body_status;
    }
    if (auto *switch_stmt = current->DynCast<SwitchStmt>()) {
      return AnalyzeSwitch(switch_stmt);
    }
    if (current->Is<BreakStmt>()) {
      if (flow_contexts.empty() || !flow_contexts.back().allows_break) {
        diagnostics.Error(current->location, errs::BreakOutsideLoopOrSwitch{});
        return std::nullopt;
      }
      return std::monostate{};
    }
    if (current->Is<ContinueStmt>()) {
      if (flow_contexts.empty() || !flow_contexts.back().allows_continue) {
        diagnostics.Error(current->location, errs::ContinueOutsideLoop{});
        return std::nullopt;
      }
      return std::monostate{};
    }
    if (auto *return_stmt = current->DynCast<ReturnStmt>()) {
      return AnalyzeReturn(return_stmt);
    }
    if (current->Is<Expr>()) {
      TRY(AnalyzeExpr(node));
      return std::monostate{};
    }
    diagnostics.Error(current->location,
                      errs::UnsupportedStatementASTNode{current->GetType()});
    return std::nullopt;
  }

  std::optional<std::monostate>
  AnalyzeCondition(std::unique_ptr<ASTNode> &condition) {
    if (!condition) {
      return std::monostate{};
    }
    TRY_ASSIGN_NEW(info, AnalyzeExpr(condition));
    TRY(ConvertToBool(condition, info, condition->location,
                      errs::ConditionRequiresScalar{}));
    return std::monostate{};
  }

  std::optional<std::monostate> AnalyzeSwitch(SwitchStmt *switch_stmt) {
    TRY_ASSIGN_NEW(switch_info, AnalyzeExpr(switch_stmt->value));
    switch_info = Decay(switch_stmt->value, switch_info);
    TypeRef switch_type = switch_info.type;
    if (!IsIntegerOrEnum(switch_type)) {
      diagnostics.Error(switch_stmt->value->location,
                        errs::SwitchRequiresIntegerOrEnum{});
      return std::nullopt;
    }

    flow_contexts.push_back({.allows_break = true, .allows_continue = false});
    for (const auto &case_node : switch_stmt->cases) {
      if (auto *case_stmt = case_node->DynCast<CaseStmt>()) {
        for (auto &value : case_stmt->case_vals) {
          std::optional<ExprInfo> value_info = AnalyzeExpr(value);
          if (!value_info) {
            flow_contexts.pop_back();
            return std::nullopt;
          }
          if (!AsExpr(value.get())->constant_value) {
            diagnostics.Error(value->location,
                              errs::CaseValueRequiresConstant{});
            flow_contexts.pop_back();
            return std::nullopt;
          }
          if (!IsIntegerOrEnum(AsExpr(value.get())->type)) {
            diagnostics.Error(value->location,
                              errs::IntegerInitializerTypeMismatch{});
            flow_contexts.pop_back();
            return std::nullopt;
          }
          if (!ConvertExpr(value, *value_info, switch_type, value->location)) {
            flow_contexts.pop_back();
            return std::nullopt;
          }
        }
        for (auto &statement : case_stmt->body) {
          if (!AnalyzeStatement(statement)) {
            flow_contexts.pop_back();
            return std::nullopt;
          }
        }
      } else if (auto *default_stmt = case_node->DynCast<DefaultStmt>()) {
        for (auto &statement : default_stmt->body) {
          if (!AnalyzeStatement(statement)) {
            flow_contexts.pop_back();
            return std::nullopt;
          }
        }
      }
    }
    flow_contexts.pop_back();
    return std::monostate{};
  }

  std::optional<std::monostate> AnalyzeReturn(ReturnStmt *return_stmt) {
    if (!current_function) {
      diagnostics.Error(return_stmt->location, errs::ReturnOutsideFunction{});
      return std::nullopt;
    }
    TypeRef return_type =
        current_function->function_type->As<FunctionType>()->return_type;
    if (!return_stmt->ret_value) {
      return std::monostate{};
    }
    TRY_ASSIGN_NEW(value, AnalyzeExpr(return_stmt->ret_value));
    TRY(ConvertExpr(return_stmt->ret_value, value, return_type,
                    return_stmt->location));
    return std::monostate{};
  }

  std::optional<NumericValue> EvaluateConstant(Expr *expr) {
    if (auto *literal = expr->DynCast<LiteralNum>()) {
      return literal->value;
    }
    if (auto *var_ref = expr->DynCast<VarRef>()) {
      if (auto *enumerator = var_ref->decl->DynCast<EnumElemDecl>()) {
        return enumerator->constant_value;
      }
      return std::nullopt;
    }
    if (auto *implicit_cast = expr->DynCast<ImplicitCast>()) {
      auto value = EvaluateConstant(AsExpr(implicit_cast->operand.get()));
      if (!value) {
        return std::nullopt;
      }
      return ConvertConstant(*value, implicit_cast->target_type);
    }
    if (auto *type_cast = expr->DynCast<TypeCast>()) {
      auto value = EvaluateConstant(AsExpr(type_cast->operand.get()));
      if (!value) {
        return std::nullopt;
      }
      return ConvertConstant(*value, type_cast->target_type);
    }
    if (auto *unary = expr->DynCast<UnaryOp>()) {
      auto operand = EvaluateConstant(AsExpr(unary->operand.get()));
      if (!operand) {
        return std::nullopt;
      }
      switch (unary->op_type) {
      case UnaryOpType::UnaryPlus:
        return operand;
      case UnaryOpType::UnaryMinus:
        return std::visit([](auto value) -> NumericValue { return -value; },
                          *operand);
      case UnaryOpType::BitNot:
        return std::visit(
            [](auto value) -> NumericValue {
              if constexpr (std::is_integral_v<decltype(value)>) {
                return ~value;
              } else {
                return value;
              }
            },
            *operand);
      case UnaryOpType::Not:
        return std::visit(
            [](auto value) -> NumericValue { return int64_t{value == 0}; },
            *operand);
      default:
        return std::nullopt;
      }
    }
    if (auto *binary = expr->DynCast<BinaryOp>()) {
      if (binary->op_type == BinaryOpType::And ||
          binary->op_type == BinaryOpType::Or) {
        auto left = EvaluateConstant(AsExpr(binary->left.get()));
        if (!left) {
          return std::nullopt;
        }
        bool left_truth =
            std::visit([](auto value) { return value != 0; }, *left);
        if (binary->op_type == BinaryOpType::And && !left_truth) {
          return int64_t{0};
        }
        if (binary->op_type == BinaryOpType::Or && left_truth) {
          return int64_t{1};
        }
        auto right = EvaluateConstant(AsExpr(binary->right.get()));
        if (!right) {
          return std::nullopt;
        }
        return std::visit(
            [](auto value) -> NumericValue { return int64_t{value != 0}; },
            *right);
      }
      auto left = EvaluateConstant(AsExpr(binary->left.get()));
      auto right = EvaluateConstant(AsExpr(binary->right.get()));
      if (!left || !right) {
        return std::nullopt;
      }
      return EvaluateBinaryConstant(binary->op_type, *left, *right,
                                    binary->operation_type);
    }
    if (auto *conditional = expr->DynCast<ConditionalExpr>()) {
      auto condition = EvaluateConstant(AsExpr(conditional->condition.get()));
      if (!condition) {
        return std::nullopt;
      }
      bool truth =
          std::visit([](auto value) { return value != 0; }, *condition);
      return EvaluateConstant(AsExpr(
          (truth ? conditional->true_expr : conditional->false_expr).get()));
    }
    if (auto *intrinsic = expr->DynCast<IntrinsicCall>()) {
      if (intrinsic->desc->name != "sizeof") {
        return std::nullopt;
      }
      if (auto *type = std::get_if<TypeRef>(&intrinsic->args[0])) {
        return static_cast<uint64_t>((*type)->GetSize());
      }
      auto *expression =
          std::get_if<std::unique_ptr<ASTNode>>(&intrinsic->args[0]);
      if (expression) {
        return static_cast<uint64_t>(
            AsExpr(expression->get())->type->GetSize());
      }
    }
    return std::nullopt;
  }

  NumericValue EvaluateBinaryConstant(BinaryOpType op, NumericValue left,
                                      NumericValue right, TypeRef result_type) {
    BinaryOpType base_op = op;
    if (std::holds_alternative<long double>(left) ||
        std::holds_alternative<long double>(right)) {
      auto to_long_double = [](const NumericValue &value) {
        return std::visit([](auto v) { return static_cast<long double>(v); },
                          value);
      };
      long double l = to_long_double(left);
      long double r = to_long_double(right);
      switch (base_op) {
      case BinaryOpType::Add:
        return l + r;
      case BinaryOpType::Sub:
        return l - r;
      case BinaryOpType::Mul:
        return l * r;
      case BinaryOpType::Div:
        return l / r;
      case BinaryOpType::Eq:
        return int64_t{l == r};
      case BinaryOpType::Ne:
        return int64_t{l != r};
      case BinaryOpType::Lt:
        return int64_t{l < r};
      case BinaryOpType::Gt:
        return int64_t{l > r};
      case BinaryOpType::Le:
        return int64_t{l <= r};
      case BinaryOpType::Ge:
        return int64_t{l >= r};
      default:
        break;
      }
    }

    bool unsigned_math =
        result_type && HasTypeTag(result_type, TypeTag::Unsigned);
    if (unsigned_math) {
      uint64_t l =
          std::visit([](auto v) { return static_cast<uint64_t>(v); }, left);
      uint64_t r =
          std::visit([](auto v) { return static_cast<uint64_t>(v); }, right);
      switch (base_op) {
      case BinaryOpType::Add:
        return l + r;
      case BinaryOpType::Sub:
        return l - r;
      case BinaryOpType::Mul:
        return l * r;
      case BinaryOpType::Div:
        return l / r;
      case BinaryOpType::Mod:
        return l % r;
      case BinaryOpType::BitAnd:
        return l & r;
      case BinaryOpType::BitOr:
        return l | r;
      case BinaryOpType::BitXor:
        return l ^ r;
      case BinaryOpType::LShift:
        return l << r;
      case BinaryOpType::RShift:
        return l >> r;
      case BinaryOpType::Eq:
        return int64_t{l == r};
      case BinaryOpType::Ne:
        return int64_t{l != r};
      case BinaryOpType::Lt:
        return int64_t{l < r};
      case BinaryOpType::Gt:
        return int64_t{l > r};
      case BinaryOpType::Le:
        return int64_t{l <= r};
      case BinaryOpType::Ge:
        return int64_t{l >= r};
      default:
        break;
      }
    }

    int64_t l =
        std::visit([](auto v) { return static_cast<int64_t>(v); }, left);
    int64_t r =
        std::visit([](auto v) { return static_cast<int64_t>(v); }, right);
    switch (base_op) {
    case BinaryOpType::Add:
      return l + r;
    case BinaryOpType::Sub:
      return l - r;
    case BinaryOpType::Mul:
      return l * r;
    case BinaryOpType::Div:
      return l / r;
    case BinaryOpType::Mod:
      return l % r;
    case BinaryOpType::BitAnd:
      return l & r;
    case BinaryOpType::BitOr:
      return l | r;
    case BinaryOpType::BitXor:
      return l ^ r;
    case BinaryOpType::LShift:
      return l << r;
    case BinaryOpType::RShift:
      return l >> r;
    case BinaryOpType::Eq:
      return int64_t{l == r};
    case BinaryOpType::Ne:
      return int64_t{l != r};
    case BinaryOpType::Lt:
      return int64_t{l < r};
    case BinaryOpType::Gt:
      return int64_t{l > r};
    case BinaryOpType::Le:
      return int64_t{l <= r};
    case BinaryOpType::Ge:
      return int64_t{l >= r};
    default:
      return int64_t{0};
    }
  }

  TranslationUnit &translation_unit;
  TypeContext &types;
  DiagnosticContext &diagnostics;
  FunctionDecl *current_function = nullptr;
  std::vector<FlowContext> flow_contexts;
  std::unordered_map<std::string_view, ObjectInfo> objects;
  std::unordered_map<std::string_view, FunctionInfo> functions;
  std::unordered_map<std::string_view, LinkageEntityKind> linkage_kinds;
};

} // namespace

std::optional<std::monostate>
AnalyzeTranslationUnit(TranslationUnit &translation_unit, TypeContext &types,
                       DiagnosticContext &diagnostics) {
  return SemanticAnalyzer(translation_unit, types, diagnostics).Run();
}
