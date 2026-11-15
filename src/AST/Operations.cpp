#include "Operations.h"

#include <string>
#include <vector>

namespace {

void AppendTypePrintField(std::vector<ASTPrintField> &fields,
                          std::string_view name, TypeRef type) {
  if (type) {
    AppendASTPrintField(fields, name, type->ToString(), ASTPrintRole::Type);
  }
}

} // namespace

AST_EXPR_PRINT_FIELDS_IMPL(FunctionInvocation)

AST_PRINT_CHILDREN_IMPL(FunctionInvocation, AST_CHILD(function),
                        AST_CHILDREN(arguments))

AST_EXPR_PRINT_FIELDS_IMPL(SequenceEval)

AST_PRINT_CHILDREN_IMPL(SequenceEval, AST_CHILDREN(expressions))

AST_EXPR_PRINT_FIELDS_IMPL(UnaryOp, AST_FIELD(op_type))

AST_PRINT_CHILDREN_IMPL(UnaryOp, AST_CHILD(operand))

std::vector<ASTPrintField> TypeCast::GetPrintFields() const {
  std::vector<ASTPrintField> fields = UnaryOp::GetPrintFields();
  AppendTypePrintField(fields, "target_type", target_type);
  return fields;
}

std::vector<ASTPrintField> ImplicitCast::GetPrintFields() const {
  std::vector<ASTPrintField> fields = UnaryOp::GetPrintFields();
  AppendTypePrintField(fields, "target_type", target_type);
  return fields;
}

AST_EXPR_PRINT_FIELDS_IMPL(BinaryOp, AST_FIELD(op_type),
                           AST_OPTIONAL_FIELD(compound_op),
                           AST_TYPE_FIELD(operation_type),
                           AST_OPTIONAL_TYPE_FIELD(left_value_cast_type),
                           AST_OPTIONAL_TYPE_FIELD(result_cast_type))

AST_PRINT_CHILDREN_IMPL(BinaryOp, AST_CHILD(left), AST_CHILD(right))

std::vector<ASTPrintField> GetField::GetPrintFields() const {
  std::vector<ASTPrintField> fields = UnaryOp::GetPrintFields();
  AppendASTPrintField(fields, "field_name", std::string(field_name));
  return fields;
}

std::vector<ASTPrintField> PtrGetField::GetPrintFields() const {
  std::vector<ASTPrintField> fields = UnaryOp::GetPrintFields();
  AppendASTPrintField(fields, "field_name", std::string(field_name));
  return fields;
}
