#include "AST.h"

#include "Intrinsic.h"
#include "Operations.h"

#include <cstdint>
#include <format>
#include <string>
#include <variant>
#include <vector>

namespace {

std::string NumericValueToString(const NumericValue &value) {
  return std::visit([](const auto &item) { return std::format("{}", item); },
                    value);
}

void AppendTypePrintField(std::vector<ASTPrintField> &fields,
                          std::string_view name, TypeRef type) {
  if (type) {
    AppendASTPrintField(fields, name, type->ToString(), ASTPrintRole::Type);
  }
}

void AppendStorageClassPrintField(std::vector<ASTPrintField> &fields,
                                  StorageClass storage_class) {
  switch (storage_class) {
  case StorageClass::None:
    return;
  case StorageClass::Static:
    AppendASTPrintField(fields, "storage_class", "static");
    return;
  case StorageClass::Extern:
    AppendASTPrintField(fields, "storage_class", "extern");
    return;
  }
}

void AppendInitializerPrintFields(std::vector<ASTPrintField> &fields,
                                  const Initializer *initializer) {
  if (!initializer) {
    return;
  }

  switch (initializer->GetType()) {
  case DeclType::ValueInitializer:
    AppendASTPrintField(fields, "initializer", "value");
    break;
  case DeclType::ArrayInitializer: {
    const auto *array = initializer->As<ArrayInitializer>();
    AppendASTPrintField(fields, "initializer", "array");
    AppendASTPrintField(fields, "elements",
                        std::to_string(array->initializers.size()));
    break;
  }
  case DeclType::FieldInitializer: {
    const auto *aggregate = initializer->As<FieldInitializer>();
    std::string names;
    for (const auto &[name, value] : aggregate->field_initializers) {
      (void)value;
      if (!names.empty()) {
        names += ',';
      }
      names += name;
    }
    AppendASTPrintField(fields, "initializer", "fields");
    AppendASTPrintField(fields, "field_names", names);
    break;
  }
  default:
    AppendASTPrintField(fields, "initializer",
                        std::string(initializer->GetNodeTypeName()));
    break;
  }
}

void AppendInitializerPrintChildren(std::vector<const ASTNode *> &children,
                                    const Initializer *initializer) {
  if (!initializer) {
    return;
  }

  switch (initializer->GetType()) {
  case DeclType::ValueInitializer:
    AppendASTPrintChild(children,
                        initializer->As<ValueInitializer>()->value.get());
    break;
  case DeclType::ArrayInitializer:
    for (const auto &element :
         initializer->As<ArrayInitializer>()->initializers) {
      AppendInitializerPrintChildren(children, element.get());
    }
    break;
  case DeclType::FieldInitializer:
    for (const auto &[name, value] :
         initializer->As<FieldInitializer>()->field_initializers) {
      (void)name;
      AppendInitializerPrintChildren(children, value.get());
    }
    break;
  default:
    break;
  }
}

} // namespace

void ASTNode::Print(std::ostream &out, ASTPrintOptions options) const {
  PrintAST(out, *this, options);
}

std::string_view ASTNode::GetNodeTypeName() const {
  return EnumRefl<ASTNodeType>::GetName(GetType());
}

std::string NamedDecl::ToString() const { return std::string(name); }

void AppendExprPrintFields(std::vector<ASTPrintField> &fields,
                           const Expr &expr) {
  AppendASTPrintField(fields, "value_category",
                      std::format("{}", expr.value_category));
  AppendTypePrintField(fields, "type", expr.type);
  if (expr.constant_value) {
    AppendASTPrintField(fields, "constant_value",
                        NumericValueToString(*expr.constant_value));
  }
}

bool IsConstantFoldable(const ASTNode *node) {
  if (node == nullptr) {
    return false;
  }

  if (node->DynCast<LiteralNum>() || node->DynCast<LiteralString>()) {
    return true;
  }
  if (auto *var_ref = node->DynCast<VarRef>()) {
    return var_ref->constant_value.has_value();
  }
  if (auto *bin_expr = node->DynCast<BinaryOp>()) {
    return IsConstantFoldable(bin_expr->left.get()) &&
           IsConstantFoldable(bin_expr->right.get());
  }
  if (auto *unary_expr = node->DynCast<UnaryOp>()) {
    return IsConstantFoldable(unary_expr->operand.get());
  }
  if (auto *implicit_cast = node->DynCast<ImplicitCast>()) {
    return IsConstantFoldable(implicit_cast->operand.get());
  }
  if (auto *type_cast = node->DynCast<TypeCast>()) {
    return IsConstantFoldable(type_cast->operand.get());
  }
  if (auto *cond_expr = node->DynCast<ConditionalExpr>()) {
    return IsConstantFoldable(cond_expr->condition.get()) &&
           IsConstantFoldable(cond_expr->true_expr.get()) &&
           IsConstantFoldable(cond_expr->false_expr.get());
  }
  if (auto *sequence = node->DynCast<SequenceEval>()) {
    if (sequence->expressions.empty()) {
      return false;
    }
    for (const auto &expression : sequence->expressions) {
      if (!IsConstantFoldable(expression.get())) {
        return false;
      }
    }
    return true;
  }
  if (auto *intrinsic_call = node->DynCast<IntrinsicCall>()) {
    return intrinsic_call->desc && intrinsic_call->desc->is_constant_evalable;
  }
  return false;
}

AST_EXPR_PRINT_FIELDS_IMPL(LiteralString, AST_FIELD(value))

std::vector<ASTPrintField> LiteralNum::GetPrintFields() const {
  std::vector<ASTPrintField> fields;
  AppendExprPrintFields(fields, *this);
  AppendASTPrintField(fields, "value", NumericValueToString(value));
  return fields;
}

AST_PRINT_FIELDS_IMPL(ParamDecl, AST_FIELD(name), AST_TYPE_FIELD(type))

std::vector<ASTPrintField> VarDecl::GetPrintFields() const {
  std::vector<ASTPrintField> fields;
  AppendASTPrintField(fields, "name", std::string(name));
  AppendTypePrintField(fields, "type", type);
  AppendStorageClassPrintField(fields, storage_class);
  AppendInitializerPrintFields(fields, initializer.get());
  return fields;
}

std::vector<const ASTNode *> VarDecl::GetPrintChildren() const {
  std::vector<const ASTNode *> children;
  AppendInitializerPrintChildren(children, initializer.get());
  return children;
}

AST_PRINT_FIELDS_IMPL(TypedefDecl, AST_FIELD(name), AST_TYPE_FIELD(type))

std::vector<ASTPrintField> VarRef::GetPrintFields() const {
  std::vector<ASTPrintField> fields;
  AppendExprPrintFields(fields, *this);
  AppendASTPrintField(fields, "decl", std::string(decl->GetNodeTypeName()),
                      ASTPrintRole::Declaration);
  AppendASTPrintField(fields, "decl_address",
                      std::format("0x{:x}", reinterpret_cast<uintptr_t>(decl)),
                      ASTPrintRole::Address);
  AppendASTPrintField(fields, "decl_name", std::string(decl->name));
  return fields;
}

AST_PRINT_CHILDREN_IMPL(DeclStmt, AST_CHILDREN(decls))

AST_PRINT_CHILDREN_IMPL(CompoundStmt, AST_CHILDREN(statements))

AST_PRINT_FIELDS_IMPL(StructDecl, AST_FIELD(name), AST_TYPE_FIELD(type))

AST_PRINT_CHILDREN_IMPL(StructDecl, AST_CHILD(fields))

AST_PRINT_FIELDS_IMPL(UnionDecl, AST_FIELD(name), AST_TYPE_FIELD(type))

AST_PRINT_CHILDREN_IMPL(UnionDecl, AST_CHILD(fields))

AST_PRINT_CHILDREN_IMPL(IfStmt, AST_CHILD(condition), AST_CHILD(then_branch),
                        AST_CHILD(else_branch))

AST_PRINT_CHILDREN_IMPL(WhileStmt, AST_CHILD(condition), AST_CHILD(body))

AST_PRINT_CHILDREN_IMPL(DoWhileStmt, AST_CHILD(body), AST_CHILD(condition))

AST_PRINT_CHILDREN_IMPL(ForStmt, AST_CHILD(init), AST_CHILD(condition),
                        AST_CHILD(increment), AST_CHILD(body))

AST_PRINT_CHILDREN_IMPL(CaseStmt, AST_CHILDREN(case_vals), AST_CHILDREN(body))

AST_PRINT_CHILDREN_IMPL(DefaultStmt, AST_CHILDREN(body))

AST_PRINT_CHILDREN_IMPL(SwitchStmt, AST_CHILD(value), AST_CHILDREN(cases))

AST_PRINT_CHILDREN_IMPL(ReturnStmt, AST_CHILD(ret_value))

std::vector<ASTPrintField> FunctionDecl::GetPrintFields() const {
  std::vector<ASTPrintField> fields;
  AppendASTPrintField(fields, "name", std::string(name));
  AppendTypePrintField(fields, "function_type", function_type);
  AppendStorageClassPrintField(fields, storage_class);
  return fields;
}

AST_PRINT_CHILDREN_IMPL(FunctionDecl, AST_CHILDREN(parameters), AST_CHILD(body))

std::vector<ASTPrintField> EnumElemDecl::GetPrintFields() const {
  std::vector<ASTPrintField> fields;
  AppendASTPrintField(fields, "name", std::string(name));
  AppendTypePrintField(fields, "type", type);
  if (constant_value) {
    AppendASTPrintField(fields, "constant_value",
                        NumericValueToString(*constant_value));
  }
  return fields;
}

AST_PRINT_CHILDREN_IMPL(EnumElemDecl, AST_CHILD(value))

AST_PRINT_FIELDS_IMPL(EnumDecl, AST_FIELD(name), AST_TYPE_FIELD(type),
                      AST_TYPE_FIELD(underlying_type),
                      AST_TRUE_FIELD(is_definition))

AST_PRINT_CHILDREN_IMPL(EnumDecl, AST_CHILDREN(values))

std::string LabelStmt::ToString() const { return std::string(name); }

AST_PRINT_FIELDS_IMPL(LabelStmt, AST_FIELD(name))

std::string GotoStmt::ToString() const { return std::string(target); }

AST_PRINT_FIELDS_IMPL(GotoStmt, AST_FIELD(target))

AST_EXPR_PRINT_FIELDS_IMPL(ConditionalExpr)

AST_PRINT_CHILDREN_IMPL(ConditionalExpr, AST_CHILD(condition),
                        AST_CHILD(true_expr), AST_CHILD(false_expr))
