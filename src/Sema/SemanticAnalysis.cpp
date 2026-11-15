#include "SemanticAnalysis.h"

#include "../AST/AST.h"
#include "../AST/Intrinsic.h"
#include "../AST/Operations.h"
#include "../AST/TranslationUnit.h"
#include "../AST/TypeContext.h"
#include "../Exception.h"
#include "../Util/Diagnostic.h"

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

bool IsIntegerOrEnum(TypeRef type) {
  return IsIntegerType(type) || type->Is<EnumType>();
}

bool IsScalar(TypeRef type) {
  return IsNumericType(type) || type->Is<PointerType>() || type->Is<EnumType>();
}

TypeRef Unqualified(TypeRef type) { return StripTypeTag(type, TypeTag::Const); }

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

bool LosesConstInPointerConversion(TypeRef from, TypeRef to) {
  if (!from->Is<PointerType>() || !to->Is<PointerType>()) {
    return false;
  }

  TypeRef from_pointee = from->As<PointerType>()->pointee_type;
  TypeRef to_pointee = to->As<PointerType>()->pointee_type;
  if (HasTypeTag(from_pointee, TypeTag::Const) &&
      !HasTypeTag(to_pointee, TypeTag::Const)) {
    return true;
  }
  return LosesConstInPointerConversion(from_pointee, to_pointee);
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
    if (std::holds_alternative<long double>(value)) {
      return static_cast<long double>(std::get<long double>(value));
    }
    if (std::holds_alternative<uint64_t>(value)) {
      return static_cast<long double>(std::get<uint64_t>(value));
    }
    return static_cast<long double>(std::get<int64_t>(value));
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

  void Run() {
    ASSERT(!translation_unit.is_semantically_analyzed,
           "Translation unit has already been analyzed");
    for (const auto &decl : translation_unit.decls) {
      AnalyzeTopLevelDecl(decl.get());
    }
    translation_unit.is_semantically_analyzed = true;
  }

private:
  Expr *AsExpr(ASTNode *node) {
    auto *expr = dynamic_cast<Expr *>(node);
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

  ExprInfo AnalyzeExpr(std::unique_ptr<ASTNode> &node) {
    ASSERT(node != nullptr, "Cannot analyze a null expression");
    ExprInfo result = AnalyzeExprNode(node.get());
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
      result = {types.getInt(), ValueCategory::RValue};
    }
    return result;
  }

  ExprInfo AnalyzeExprNode(ASTNode *node) {
    if (auto *literal = node->DynCast<LiteralNum>()) {
      literal->constant_value = literal->value;
      return {literal->type, ValueCategory::RValue};
    }
    if (auto *literal = node->DynCast<LiteralString>()) {
      TypeRef type =
          types.Array(types.getChar(), literal->value.view().size() + 1);
      return {type, ValueCategory::LValue};
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
      return {implicit_cast->target_type, ValueCategory::RValue};
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
      diagnostics.Fail(sequence->location,
                       "Unsupported AST node type: SequenceEval");
    }
    diagnostics.Fail(node->location,
                     "Unsupported expression AST node: ", node->GetType());
  }

  ExprInfo AnalyzeVarRef(VarRef *var_ref) {
    ValueDecl *decl = var_ref->decl;
    if (auto *var = dynamic_cast<VarDecl *>(decl)) {
      return {var->type, ValueCategory::LValue};
    }
    if (auto *param = dynamic_cast<ParamDecl *>(decl)) {
      return {AdjustedParameterType(param->type), ValueCategory::LValue};
    }
    if (auto *function = dynamic_cast<FunctionDecl *>(decl)) {
      return {function->function_type, ValueCategory::FunctionDesignator};
    }
    if (auto *enumerator = dynamic_cast<EnumElemDecl *>(decl)) {
      ASSERT(enumerator->type != nullptr,
             "Enum constant is missing its type after analysis");
      return {enumerator->type, ValueCategory::RValue};
    }
    diagnostics.Fail(var_ref->location,
                     "Declaration has no generated value: ", decl->name);
  }

  ExprInfo Decay(std::unique_ptr<ASTNode> &node, ExprInfo info) {
    if (auto *array = info.type->DynCast<ArrayType>()) {
      TypeRef pointer = types.Pointer(array->element_type);
      InsertImplicitCast(node, pointer);
      return {pointer, ValueCategory::RValue};
    }
    if (info.type->Is<FunctionType>()) {
      TypeRef pointer = types.Pointer(info.type);
      InsertImplicitCast(node, pointer);
      return {pointer, ValueCategory::RValue};
    }
    return {Unqualified(info.type), ValueCategory::RValue};
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

  ExprInfo ConvertExpr(std::unique_ptr<ASTNode> &node, ExprInfo info,
                       TypeRef target_type, SourceLocation location) {
    info = Decay(node, info);
    info.type = Unqualified(info.type);
    target_type = Unqualified(target_type);
    if (info.type == target_type) {
      return {target_type, ValueCategory::RValue};
    }
    if (IsBooleanType(target_type)) {
      if (!IsScalar(info.type)) {
        diagnostics.Fail(location,
                         "Boolean conversion requires a scalar expression");
      }
      InsertImplicitCast(node, target_type);
      return {target_type, ValueCategory::RValue};
    }
    if (info.type->Is<PointerType>() && target_type->Is<PointerType>()) {
      if (LosesConstInPointerConversion(info.type, target_type)) {
        diagnostics.Fail(location,
                         "Cannot assign a pointer after discarding const");
      }
      InsertImplicitCast(node, target_type);
      return {target_type, ValueCategory::RValue};
    }
    if (IsNumericType(info.type) && IsNumericType(target_type)) {
      InsertImplicitCast(node, target_type);
      return {target_type, ValueCategory::RValue};
    }
    if (HaveSameRepresentation(info.type, target_type)) {
      InsertImplicitCast(node, target_type);
      return {target_type, ValueCategory::RValue};
    }
    diagnostics.Fail(location, "Failed to cast between incompatible types");
  }

  ExprInfo ConvertToBool(std::unique_ptr<ASTNode> &node, ExprInfo info,
                         SourceLocation location,
                         std::string_view invalid_message) {
    info = Decay(node, info);
    if (!IsScalar(info.type)) {
      diagnostics.Fail(location, invalid_message);
    }
    TypeRef bool_type = BooleanType();
    if (info.type != bool_type) {
      InsertImplicitCast(node, bool_type);
    }
    return {bool_type, ValueCategory::RValue};
  }

  ExprInfo AnalyzeBinary(BinaryOp *binary) {
    BinaryOpType op = binary->op_type;
    ASSERT(!binary->compound_op || op == BinaryOpType::Assignment,
           "Compound operation attached to a non-assignment expression");
    if (op == BinaryOpType::Assignment && !binary->compound_op) {
      ExprInfo left_info = AnalyzeExpr(binary->left);
      if (left_info.value_category != ValueCategory::LValue) {
        diagnostics.Fail(binary->location, "Assignment requires an lvalue");
      }
      if (HasTypeTag(left_info.type, TypeTag::Const)) {
        diagnostics.Fail(binary->location,
                         "Cannot assign to a const-qualified object");
      }
      ExprInfo right_info = AnalyzeExpr(binary->right);
      ConvertExpr(binary->right, right_info, left_info.type, binary->location);
      return {Unqualified(left_info.type), ValueCategory::RValue};
    }

    if (op == BinaryOpType::Subscript) {
      ExprInfo left_info = AnalyzeExpr(binary->left);
      left_info = Decay(binary->left, left_info);
      ExprInfo right_info = AnalyzeExpr(binary->right);
      right_info = Decay(binary->right, right_info);
      if (!IsIntegerType(right_info.type)) {
        diagnostics.Fail(binary->location,
                         "Subscript requires an integer index");
      }
      if (auto *pointer = left_info.type->DynCast<PointerType>()) {
        return {pointer->pointee_type, ValueCategory::LValue};
      }
      diagnostics.Fail(binary->location,
                       "Subscript requires an array or pointer operand");
    }

    if (op == BinaryOpType::And || op == BinaryOpType::Or) {
      ExprInfo left_info = AnalyzeExpr(binary->left);
      ExprInfo right_info = AnalyzeExpr(binary->right);
      left_info = ConvertToBool(binary->left, left_info, binary->location,
                                "Logical operators require scalar operands");
      right_info = ConvertToBool(binary->right, right_info, binary->location,
                                 "Logical operators require scalar operands");
      binary->operation_type = BooleanType();
      return {BooleanType(), ValueCategory::RValue};
    }

    const bool is_compound = binary->compound_op.has_value();
    TypeRef compound_left_type = nullptr;
    ExprInfo left_info = AnalyzeExpr(binary->left);
    if (is_compound) {
      if (left_info.value_category != ValueCategory::LValue) {
        diagnostics.Fail(binary->location, "Assignment requires an lvalue");
      }
      if (HasTypeTag(left_info.type, TypeTag::Const)) {
        diagnostics.Fail(binary->location,
                         "Cannot modify a const-qualified object");
      }
      left_info.type = Unqualified(left_info.type);
      compound_left_type = left_info.type;
    } else {
      left_info = Decay(binary->left, left_info);
    }

    ExprInfo right_info = AnalyzeExpr(binary->right);
    if (!is_compound) {
      right_info = Decay(binary->right, right_info);
    }

    BinaryOpType base_op = is_compound ? *binary->compound_op : op;
    if (!is_compound &&
        (base_op == BinaryOpType::BitAnd || base_op == BinaryOpType::BitOr ||
         base_op == BinaryOpType::BitXor)) {
      if (!IsIntegerType(left_info.type)) {
        diagnostics.Fail(binary->location,
                         "Bitwise operators require an integer left operand");
      }
      if (!IsIntegerType(right_info.type)) {
        diagnostics.Fail(binary->location,
                         "Bitwise operators require an integer right operand");
      }
      TypeRef common =
          UsualArithmeticType(left_info.type, right_info.type, types);
      ConvertExpr(binary->left, left_info, common, binary->location);
      ConvertExpr(binary->right, right_info, common, binary->location);
      binary->operation_type = common;
      return {common, ValueCategory::RValue};
    }

    if (!is_compound &&
        (base_op == BinaryOpType::LShift || base_op == BinaryOpType::RShift)) {
      if (!IsIntegerType(left_info.type)) {
        diagnostics.Fail(binary->location,
                         "Shift operators require an integer left operand");
      }
      if (!IsIntegerType(right_info.type)) {
        diagnostics.Fail(binary->location,
                         "Shift operators require an integer right operand");
      }
      TypeRef promoted = PromoteInteger(left_info.type, types);
      ConvertExpr(binary->left, left_info, promoted, binary->location);
      ConvertExpr(binary->right, right_info, promoted, binary->location);
      binary->operation_type = promoted;
      return {promoted, ValueCategory::RValue};
    }

    if (!is_compound && left_info.type->Is<PointerType>()) {
      if (!IsIntegerType(right_info.type)) {
        diagnostics.Fail(binary->location,
                         "Pointer arithmetic only supports integer offsets");
      }
      if (base_op != BinaryOpType::Add && base_op != BinaryOpType::Sub) {
        diagnostics.Fail(
            binary->location,
            "Pointer arithmetic only supports addition and subtraction");
      }
      binary->operation_type = left_info.type;
      return {left_info.type, ValueCategory::RValue};
    }

    if (!IsNumericType(left_info.type) || !IsNumericType(right_info.type)) {
      diagnostics.Fail(binary->location,
                       "Arithmetic operators require numeric operands");
    }
    TypeRef common =
        UsualArithmeticType(left_info.type, right_info.type, types);
    if (!is_compound) {
      ConvertExpr(binary->left, left_info, common, binary->location);
    }
    ConvertExpr(binary->right, right_info, common, binary->location);
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
    return {result_type, ValueCategory::RValue};
  }

  ExprInfo AnalyzeUnary(UnaryOp *unary) {
    ExprInfo operand = AnalyzeExpr(unary->operand);
    switch (unary->op_type) {
    case UnaryOpType::GetAddress: {
      if (operand.value_category != ValueCategory::LValue &&
          operand.value_category != ValueCategory::FunctionDesignator) {
        diagnostics.Fail(unary->location, "Address-of requires an lvalue");
      }
      return {types.Pointer(operand.type), ValueCategory::RValue};
    }
    case UnaryOpType::Deref: {
      operand = Decay(unary->operand, operand);
      if (auto *pointer = operand.type->DynCast<PointerType>()) {
        return {pointer->pointee_type, ValueCategory::LValue};
      }
      diagnostics.Fail(unary->location,
                       "Dereference requires a pointer operand");
    }
    case UnaryOpType::IncBeforeUse:
    case UnaryOpType::IncAfterUse:
    case UnaryOpType::DecBeforeUse:
    case UnaryOpType::DecAfterUse: {
      if (operand.value_category != ValueCategory::LValue) {
        diagnostics.Fail(unary->location,
                         "Increment/decrement requires an lvalue");
      }
      if (HasTypeTag(operand.type, TypeTag::Const)) {
        diagnostics.Fail(unary->location,
                         "Cannot modify a const-qualified object");
      }
      if (!IsNumericType(operand.type) && !operand.type->Is<PointerType>()) {
        diagnostics.Fail(unary->location,
                         "Increment/decrement requires a scalar operand");
      }
      return {Unqualified(operand.type), ValueCategory::RValue};
    }
    case UnaryOpType::UnaryPlus:
    case UnaryOpType::UnaryMinus:
    case UnaryOpType::BitNot: {
      operand = Decay(unary->operand, operand);
      if (unary->op_type == UnaryOpType::BitNot) {
        if (!IsIntegerType(operand.type)) {
          diagnostics.Fail(unary->location,
                           "Bitwise NOT requires an integer operand");
        }
      } else if (!IsNumericType(operand.type)) {
        diagnostics.Fail(unary->location,
                         "Unary plus/minus requires a numeric operand");
      }
      TypeRef result_type = operand.type;
      if (IsIntegerType(operand.type)) {
        result_type = PromoteInteger(operand.type, types);
      }
      ConvertExpr(unary->operand, operand, result_type, unary->location);
      return {result_type, ValueCategory::RValue};
    }
    case UnaryOpType::Not: {
      ConvertToBool(unary->operand, operand, unary->location,
                    "Logical NOT requires a scalar operand");
      return {BooleanType(), ValueCategory::RValue};
    }
    default:
      diagnostics.Fail(unary->location,
                       "Unsupported unary operation: ", unary->op_type);
    }
  }

  ExprInfo AnalyzeTypeCast(TypeCast *type_cast) {
    ExprInfo operand = AnalyzeExpr(type_cast->operand);
    operand = Decay(type_cast->operand, operand);
    TypeRef target = type_cast->target_type;
    if (operand.type != target) {
      if (operand.type->Is<PointerType>() && target->Is<PointerType>()) {
        if (LosesConstInPointerConversion(operand.type, target)) {
          diagnostics.Fail(type_cast->location,
                           "Cannot cast a pointer after discarding const");
        }
      } else if (!IsNumericType(operand.type) || !IsNumericType(target)) {
        diagnostics.Fail(type_cast->location, "Unsupported type cast from ",
                         operand.type->ToString(), " to ", target->ToString());
      }
    }
    return {target, ValueCategory::RValue};
  }

  TypeRef GetFieldType(TypeRef type, std::string_view field_name,
                       SourceLocation location) {
    if (auto *struct_type = type->DynCast<StructType>()) {
      auto field = struct_type->GetField(field_name);
      if (!field) {
        diagnostics.Fail(location, "Unknown struct field: ", field_name);
      }
      return field->type;
    }
    if (auto *union_type = type->DynCast<UnionType>()) {
      auto field = union_type->GetField(field_name);
      if (!field) {
        diagnostics.Fail(location, "Unknown union field: ", field_name);
      }
      return field->type;
    }
    diagnostics.Fail(location,
                     "Field access requires a struct or union operand");
  }

  ExprInfo AnalyzeGetField(GetField *get_field) {
    ExprInfo operand = AnalyzeExpr(get_field->operand);
    ExprInfo result{
        GetFieldType(operand.type, get_field->field_name, get_field->location),
        operand.value_category == ValueCategory::LValue ? ValueCategory::LValue
                                                        : ValueCategory::RValue,
    };
    return result;
  }

  ExprInfo AnalyzePtrGetField(PtrGetField *ptr_get_field) {
    ExprInfo operand = AnalyzeExpr(ptr_get_field->operand);
    operand = Decay(ptr_get_field->operand, operand);
    if (auto *pointer = operand.type->DynCast<PointerType>()) {
      return {GetFieldType(pointer->pointee_type, ptr_get_field->field_name,
                           ptr_get_field->location),
              ValueCategory::LValue};
    }
    diagnostics.Fail(ptr_get_field->location,
                     "Pointer field access requires a pointer operand");
  }

  TypeRef AdjustedParameterType(TypeRef type) {
    if (auto *array = type->DynCast<ArrayType>()) {
      return types.Pointer(array->element_type);
    }
    if (type->Is<FunctionType>()) {
      return types.Pointer(type);
    }
    return type;
  }

  ExprInfo AnalyzeFunctionInvocation(FunctionInvocation *call) {
    ExprInfo function = AnalyzeExpr(call->function);
    function = Decay(call->function, function);
    TypeRef function_type = function.type;
    if (auto *pointer = function_type->DynCast<PointerType>()) {
      function_type = pointer->pointee_type;
    }
    if (!function_type->Is<FunctionType>()) {
      diagnostics.Fail(
          call->location,
          "Function invocation requires a function or function pointer");
    }

    const auto *signature = function_type->As<FunctionType>();
    if (signature->has_va_args) {
      if (call->arguments.size() < signature->parameters.size()) {
        diagnostics.Fail(call->location,
                         "Function argument count mismatch, expected at least ",
                         signature->parameters.size(), " arguments");
      }
    } else if (call->arguments.size() != signature->parameters.size()) {
      diagnostics.Fail(call->location,
                       "Function argument count mismatch, expected ",
                       signature->parameters.size(), " arguments");
    }

    for (size_t i = 0; i < call->arguments.size(); ++i) {
      ExprInfo argument = AnalyzeExpr(call->arguments[i]);
      if (i < signature->parameters.size()) {
        TypeRef parameter_type =
            AdjustedParameterType(signature->parameters[i]);
        ConvertExpr(call->arguments[i], argument, parameter_type,
                    call->location);
      } else {
        argument = Decay(call->arguments[i], argument);
        if (IsIntegerType(argument.type)) {
          TypeRef promoted = PromoteInteger(argument.type, types);
          ConvertExpr(call->arguments[i], argument, promoted, call->location);
        } else if (IsFloatingType(argument.type) &&
                   argument.type->As<BuiltinType>()->Kind() ==
                       BuiltinKind::Float) {
          ConvertExpr(call->arguments[i], argument, types.getDouble(),
                      call->location);
        }
      }
    }
    return {signature->return_type, ValueCategory::RValue};
  }

  ExprInfo AnalyzeIntrinsic(IntrinsicCall *call) {
    ASSERT(call->desc != nullptr, "Intrinsic call is missing its descriptor");
    for (auto &arg : call->args) {
      if (auto *expression = std::get_if<std::unique_ptr<ASTNode>>(&arg)) {
        AnalyzeExpr(*expression);
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
        diagnostics.Fail(call->location,
                         "sizeof requires a complete object type");
      }
    }
    return {result, ValueCategory::RValue};
  }

  ExprInfo AnalyzeConditional(ConditionalExpr *conditional) {
    ExprInfo condition = AnalyzeExpr(conditional->condition);
    ConvertToBool(conditional->condition, condition, conditional->location,
                  "Conditional expression requires a scalar condition");

    ExprInfo true_info = AnalyzeExpr(conditional->true_expr);
    ExprInfo false_info = AnalyzeExpr(conditional->false_expr);
    true_info = Decay(conditional->true_expr, true_info);
    false_info = Decay(conditional->false_expr, false_info);
    if (!IsNumericType(true_info.type) || !IsNumericType(false_info.type)) {
      diagnostics.Fail(
          conditional->location,
          "Conditional expression requires compatible numeric operands");
    }
    TypeRef common =
        UsualArithmeticType(true_info.type, false_info.type, types);
    ConvertExpr(conditional->true_expr, true_info, common,
                conditional->location);
    ConvertExpr(conditional->false_expr, false_info, common,
                conditional->location);
    return {common, ValueCategory::RValue};
  }

  void AnalyzeDeclStmt(DeclStmt *stmt, bool global) {
    for (const auto &decl : stmt->decls) {
      AnalyzeDeclaration(decl.get(), global);
    }
  }

  void AnalyzeDeclaration(ASTNode *node, bool global) {
    if (auto *var = node->DynCast<VarDecl>()) {
      AnalyzeVarDecl(var, global);
      return;
    }
    if (auto *function = node->DynCast<FunctionDecl>()) {
      AnalyzeFunctionDecl(function, global);
      return;
    }
    if (auto *enum_decl = node->DynCast<EnumDecl>()) {
      AnalyzeEnumDecl(enum_decl);
      return;
    }
    if (node->Is<StructDecl>() || node->Is<UnionDecl>() ||
        node->Is<TypedefDecl>() || node->Is<ParamDecl>()) {
      return;
    }
    if (auto *stmt = node->DynCast<DeclStmt>()) {
      AnalyzeDeclStmt(stmt, global);
      return;
    }
    diagnostics.Fail(node->location,
                     "Unsupported declaration AST node: ", node->GetType());
  }

  void AnalyzeTopLevelDecl(ASTNode *node) {
    switch (node->GetType()) {
    case ASTNodeType::FunctionDecl:
    case ASTNodeType::VarDecl:
    case ASTNodeType::StructDecl:
    case ASTNodeType::UnionDecl:
    case ASTNodeType::EnumDecl:
    case ASTNodeType::TypedefDecl:
    case ASTNodeType::DeclStmt:
      AnalyzeDeclaration(node, true);
      return;
    default:
      diagnostics.Fail(node->location,
                       "Unsupported top-level AST node: ", node->GetType());
    }
  }

  void AnalyzeFunctionDecl(FunctionDecl *function, bool global) {
    if (!global) {
      diagnostics.Fail(function->location,
                       "Function definitions are only valid at file scope");
    }
    ASSERT(function->function_type != nullptr,
           "Function declaration is missing its type");
    const auto *function_type = function->function_type->As<FunctionType>();
    auto found = functions.find(function->name);
    if (found != functions.end()) {
      const auto *previous = found->second->function_type->As<FunctionType>();
      if (function->function_type != found->second->function_type ||
          function_type->has_va_args != previous->has_va_args) {
        diagnostics.Fail(function->location,
                         "Conflicting declaration for function ",
                         function->name);
      }
    } else {
      functions.emplace(function->name, function);
    }

    if (!function->body) {
      return;
    }
    if (auto found_definition = definitions.find(function->name);
        found_definition != definitions.end()) {
      diagnostics.Fail(function->location, "Multiple definitions of function ",
                       function->name);
    }
    definitions.emplace(function->name, function);

    FunctionDecl *previous_function = current_function;
    current_function = function;
    for (auto &statement : function->body->statements) {
      AnalyzeStatement(statement);
    }
    current_function = previous_function;
  }

  void AnalyzeEnumDecl(EnumDecl *decl) {
    if (!decl->is_definition) {
      return;
    }
    auto *enum_type = decl->type->As<EnumType>();
    if (enum_type->is_complete) {
      return;
    }

    std::vector<EnumValue> values;
    values.reserve(decl->values.size());
    for (size_t i = 0; i < decl->values.size(); ++i) {
      EnumElemDecl *element = decl->values[i].get();
      element->type = decl->type;
      NumericValue value;
      if (element->value) {
        AnalyzeExpr(element->value);
        Expr *expression = AsExpr(element->value.get());
        if (!expression->constant_value) {
          diagnostics.Fail(element->location,
                           "Enum value must be a constant expression");
        }
        value =
            ConvertConstant(*expression->constant_value, decl->underlying_type);
        if (std::holds_alternative<long double>(value)) {
          diagnostics.Fail(element->location,
                           "Enum value cannot be a floating point number");
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
  }

  void AnalyzeVarDecl(VarDecl *var, bool global) {
    if (!global && var->storage_class != StorageClass::None) {
      diagnostics.Fail(var->location,
                       "Local static storage is not supported yet");
    }
    TypeRef type = var->type;
    if (auto *array = type->DynCast<ArrayType>();
        array && !array->size.has_value()) {
      std::optional<size_t> inferred_size;
      if (!var->initializer) {
        if (global) {
          diagnostics.Fail(
              var->location,
              "Can't infer array size from initializer for global variable: ",
              var->name);
        }
        diagnostics.Fail(var->location,
                         "Array with unspecified size requires an initializer");
      }
      if (auto *array_init = var->initializer->DynCast<ArrayInitializer>()) {
        inferred_size = array_init->initializers.size();
      } else if (auto *value_init =
                     var->initializer->DynCast<ValueInitializer>()) {
        if (auto *literal = value_init->value->DynCast<LiteralString>()) {
          if (!array->element_type->Is<BuiltinType>() ||
              array->element_type->As<BuiltinType>()->Kind() !=
                  BuiltinKind::Char) {
            diagnostics.Fail(
                var->location,
                "Array type must be a char array for string literal "
                "initializer");
          }
          inferred_size = literal->value.view().size() + 1;
        }
      }
      if (!inferred_size) {
        if (global) {
          diagnostics.Fail(
              var->location,
              "Can't infer array size from initializer for global variable: ",
              var->name);
        }
        diagnostics.Fail(
            var->initializer->location,
            "Array initializer must be a brace list or string literal");
      }
      TypeRef completed = types.Array(array->element_type, *inferred_size);
      completed.set_tag(type.get_tag());
      var->type = completed;
      type = completed;
    }

    if (var->initializer) {
      AnalyzeInitializer(var->initializer.get(), type, var->location, global);
      if (global && !IsConstantFoldable(var->initializer.get())) {
        diagnostics.Fail(
            var->location,
            "Global variable initializer must be constant foldable");
      }
    }
  }

  void AnalyzeInitializer(Initializer *initializer, TypeRef type,
                          SourceLocation declaration_location, bool global) {
    if (auto *array_type = type->DynCast<ArrayType>()) {
      if (auto *array_init = initializer->DynCast<ArrayInitializer>()) {
        size_t size =
            array_type->size.value_or(array_init->initializers.size());
        if (array_init->initializers.size() > size) {
          diagnostics.Fail(
              array_init->location,
              "Array initializer has more elements than the array size");
        }
        for (const auto &element : array_init->initializers) {
          AnalyzeInitializer(element.get(), array_type->element_type,
                             declaration_location, global);
        }
        return;
      }
      if (auto *value_init = initializer->DynCast<ValueInitializer>()) {
        if (auto *literal = value_init->value->DynCast<LiteralString>();
            literal && array_type->element_type->Is<BuiltinType>() &&
            array_type->element_type->As<BuiltinType>()->Kind() ==
                BuiltinKind::Char) {
          if (array_type->size.value() < literal->value.view().size() + 1) {
            diagnostics.Fail(
                literal->location,
                "Array size is smaller than the literal string size");
          }
          AnalyzeExpr(value_init->value);
          return;
        }
      }
      diagnostics.Fail(initializer->location, "Unexpected initializer");
    }

    if (auto *struct_type = type->DynCast<StructType>()) {
      if (auto *field_init = initializer->DynCast<FieldInitializer>()) {
        for (auto &[name, field_initializer] : field_init->field_initializers) {
          auto field = struct_type->GetField(name);
          if (!field) {
            diagnostics.Fail(field_init->location,
                             "Unknown field name: ", name);
          }
          AnalyzeInitializer(field_initializer.get(), field->type,
                             declaration_location, global);
        }
        return;
      }
      if (auto *array_init = initializer->DynCast<ArrayInitializer>()) {
        if (array_init->initializers.size() > struct_type->fields.size()) {
          diagnostics.Fail(
              array_init->location,
              "Struct initializer has more elements than the struct fields");
        }
        for (size_t i = 0; i < array_init->initializers.size(); ++i) {
          AnalyzeInitializer(array_init->initializers[i].get(),
                             struct_type->fields[i].type, declaration_location,
                             global);
        }
        return;
      }
      if (auto *value_init = initializer->DynCast<ValueInitializer>()) {
        ExprInfo value = AnalyzeExpr(value_init->value);
        ConvertExpr(value_init->value, value, type, value_init->location);
        return;
      }
    }

    if (type->Is<BuiltinType>()) {
      auto *value_init = initializer->DynCast<ValueInitializer>();
      if (!value_init) {
        diagnostics.Fail(initializer->location,
                         "Builtin type requires a value initializer");
      }
      ExprInfo value = AnalyzeExpr(value_init->value);
      ConvertExpr(value_init->value, value, type, value_init->location);
      return;
    }

    if (auto *pointer = type->DynCast<PointerType>()) {
      auto *value_init = initializer->DynCast<ValueInitializer>();
      if (!value_init) {
        diagnostics.Fail(initializer->location,
                         "Pointer type requires a value initializer");
      }
      ExprInfo value = AnalyzeExpr(value_init->value);
      value = Decay(value_init->value, value);
      if (global) {
        if (!AsExpr(value_init->value.get())->constant_value) {
          diagnostics.Fail(
              value_init->location,
              "Global pointer initializer must be a constant expression");
        }
        if (!value.type->Is<PointerType>()) {
          if (!IsIntegerType(value.type) && !value.type->Is<EnumType>()) {
            diagnostics.Fail(value_init->location,
                             "Failed to cast between incompatible types");
          }
          InsertImplicitCast(value_init->value, pointer);
        }
      } else {
        ConvertExpr(value_init->value, value, pointer, value_init->location);
      }
      return;
    }

    if (!global && (type->Is<EnumType>() || type->Is<UnionType>())) {
      auto *value_init = initializer->DynCast<ValueInitializer>();
      if (!value_init) {
        diagnostics.Fail(initializer->location, "Unexpected initializer");
      }
      ExprInfo value = AnalyzeExpr(value_init->value);
      ConvertExpr(value_init->value, value, type, value_init->location);
      return;
    }

    diagnostics.Fail(initializer->location, "Unexpected initializer");
  }

  void AnalyzeStatement(std::unique_ptr<ASTNode> &node) {
    if (!node) {
      return;
    }
    ASTNode *current = node.get();
    if (current->Is<EmptyStmt>() || current->Is<LabelStmt>() ||
        current->Is<GotoStmt>() || current->Is<TypedefDecl>()) {
      return;
    }
    if (auto *decl_stmt = current->DynCast<DeclStmt>()) {
      AnalyzeDeclStmt(decl_stmt, false);
      return;
    }
    if (auto *var = current->DynCast<VarDecl>()) {
      AnalyzeVarDecl(var, false);
      return;
    }
    if (auto *struct_decl = current->DynCast<StructDecl>()) {
      (void)struct_decl;
      return;
    }
    if (auto *union_decl = current->DynCast<UnionDecl>()) {
      (void)union_decl;
      return;
    }
    if (auto *enum_decl = current->DynCast<EnumDecl>()) {
      AnalyzeEnumDecl(enum_decl);
      return;
    }
    if (auto *compound = current->DynCast<CompoundStmt>()) {
      for (auto &statement : compound->statements) {
        AnalyzeStatement(statement);
      }
      return;
    }
    if (auto *if_stmt = current->DynCast<IfStmt>()) {
      AnalyzeCondition(if_stmt->condition);
      AnalyzeStatement(if_stmt->then_branch);
      AnalyzeStatement(if_stmt->else_branch);
      return;
    }
    if (auto *while_stmt = current->DynCast<WhileStmt>()) {
      AnalyzeCondition(while_stmt->condition);
      flow_contexts.push_back({.allows_break = true, .allows_continue = true});
      AnalyzeStatement(while_stmt->body);
      flow_contexts.pop_back();
      return;
    }
    if (auto *do_while = current->DynCast<DoWhileStmt>()) {
      flow_contexts.push_back({.allows_break = true, .allows_continue = true});
      AnalyzeStatement(do_while->body);
      flow_contexts.pop_back();
      AnalyzeCondition(do_while->condition);
      return;
    }
    if (auto *for_stmt = current->DynCast<ForStmt>()) {
      AnalyzeStatement(for_stmt->init);
      AnalyzeCondition(for_stmt->condition);
      AnalyzeStatement(for_stmt->increment);
      flow_contexts.push_back({.allows_break = true, .allows_continue = true});
      AnalyzeStatement(for_stmt->body);
      flow_contexts.pop_back();
      return;
    }
    if (auto *switch_stmt = current->DynCast<SwitchStmt>()) {
      AnalyzeSwitch(switch_stmt);
      return;
    }
    if (current->Is<BreakStmt>()) {
      if (flow_contexts.empty() || !flow_contexts.back().allows_break) {
        diagnostics.Fail(current->location,
                         "break statement is not inside a loop or switch");
      }
      return;
    }
    if (current->Is<ContinueStmt>()) {
      if (flow_contexts.empty() || !flow_contexts.back().allows_continue) {
        diagnostics.Fail(current->location,
                         "continue statement is not inside a loop");
      }
      return;
    }
    if (auto *return_stmt = current->DynCast<ReturnStmt>()) {
      AnalyzeReturn(return_stmt);
      return;
    }
    if (dynamic_cast<Expr *>(current)) {
      AnalyzeExpr(node);
      return;
    }
    diagnostics.Fail(current->location,
                     "Unsupported statement AST node: ", current->GetType());
  }

  void AnalyzeCondition(std::unique_ptr<ASTNode> &condition) {
    if (!condition) {
      return;
    }
    ExprInfo info = AnalyzeExpr(condition);
    ConvertToBool(condition, info, condition->location,
                  "Condition requires a scalar expression");
  }

  void AnalyzeSwitch(SwitchStmt *switch_stmt) {
    ExprInfo switch_info = AnalyzeExpr(switch_stmt->value);
    switch_info = Decay(switch_stmt->value, switch_info);
    TypeRef switch_type = switch_info.type;
    if (!IsIntegerOrEnum(switch_type)) {
      diagnostics.Fail(
          switch_stmt->value->location,
          "switch requires an integer or enum controlling expression");
    }

    flow_contexts.push_back({.allows_break = true, .allows_continue = false});
    for (const auto &case_node : switch_stmt->cases) {
      if (auto *case_stmt = case_node->DynCast<CaseStmt>()) {
        for (auto &value : case_stmt->case_vals) {
          ExprInfo value_info = AnalyzeExpr(value);
          if (!AsExpr(value.get())->constant_value) {
            diagnostics.Fail(value->location,
                             "Case value must be a constant expression");
          }
          if (!IsIntegerOrEnum(AsExpr(value.get())->type)) {
            diagnostics.Fail(value->location,
                             "Integer initializer type mismatch");
          }
          ConvertExpr(value, value_info, switch_type, value->location);
        }
        for (auto &statement : case_stmt->body) {
          AnalyzeStatement(statement);
        }
      } else if (auto *default_stmt = case_node->DynCast<DefaultStmt>()) {
        for (auto &statement : default_stmt->body) {
          AnalyzeStatement(statement);
        }
      }
    }
    flow_contexts.pop_back();
  }

  void AnalyzeReturn(ReturnStmt *return_stmt) {
    if (!current_function) {
      diagnostics.Fail(return_stmt->location,
                       "return statement is not inside a function");
    }
    TypeRef return_type =
        current_function->function_type->As<FunctionType>()->return_type;
    if (!return_stmt->ret_value) {
      return;
    }
    ExprInfo value = AnalyzeExpr(return_stmt->ret_value);
    ConvertExpr(return_stmt->ret_value, value, return_type,
                return_stmt->location);
  }

  std::optional<NumericValue> EvaluateConstant(Expr *expr) {
    if (auto *literal = expr->DynCast<LiteralNum>()) {
      return literal->value;
    }
    if (auto *var_ref = expr->DynCast<VarRef>()) {
      if (auto *enumerator = dynamic_cast<EnumElemDecl *>(var_ref->decl)) {
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
  std::unordered_map<std::string_view, FunctionDecl *> functions;
  std::unordered_map<std::string_view, FunctionDecl *> definitions;
};

} // namespace

void AnalyzeTranslationUnit(TranslationUnit &translation_unit,
                            TypeContext &types,
                            DiagnosticContext &diagnostics) {
  SemanticAnalyzer(translation_unit, types, diagnostics).Run();
}
