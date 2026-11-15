#pragma once

#include "AST.h"
#include "Type.h"

#include <optional>

class FunctionInvocation : public Expr {
  ASTNodeImpl(FunctionInvocation);
  ExprPrintFields(function, arguments);

public:
  std::unique_ptr<ASTNode> function;
  std::vector<std::unique_ptr<ASTNode>> arguments;
};

class SequenceEval : public Expr {
  ASTNodeImpl(SequenceEval);
  ExprPrintFields(expressions);

public:
  std::vector<std::unique_ptr<ASTNode>> expressions;
};

// clang-format off
#define BINARY_NUMERIC_ARITHMETIC_OP_VALUES(x)\
  x(Add)\
  x(Sub)\
  x(Mul)\
  x(Div)\
  x(Mod)\
  x(Eq)\
  x(Ne)\
  x(Lt)\
  x(Gt)\
  x(Le)\
  x(Ge)

#define BINARY_BITWISE_ARITHMETIC_OP_VALUES(x)\
  x(BitAnd)\
  x(BitOr)\
  x(BitXor)\
  x(LShift)\
  x(RShift)

#define BINARY_OP_TYPE_VALUES(x)\
  x(Assignment)\
  BINARY_NUMERIC_ARITHMETIC_OP_VALUES(x)\
  BINARY_BITWISE_ARITHMETIC_OP_VALUES(x)\
  x(Subscript)\
  x(And)\
  x(Or)

#define UNARY_ARITHMETIC_OP_VALUES(x)\
  x(Not)\
  x(BitNot)\
  x(UnaryPlus)\
  x(UnaryMinus)\
  x(TypeCast)

#define UNARY_OP_TYPE_VALUES(x)\
  x(IncBeforeUse)\
  x(IncAfterUse)\
  x(DecBeforeUse)\
  x(DecAfterUse)\
  x(GetField)\
  x(PtrGetField)\
  x(Deref)\
  x(GetAddress)\
  UNARY_ARITHMETIC_OP_VALUES(x)
// clang-format on

MAKE_ENUM(BinaryOpType, BINARY_OP_TYPE_VALUES)
REGISTER_ENUM(BinaryOpType, BINARY_OP_TYPE_VALUES)

MAKE_ENUM(UnaryOpType, UNARY_OP_TYPE_VALUES)
REGISTER_ENUM(UnaryOpType, UNARY_OP_TYPE_VALUES)

inline void
AppendTreeNodePrintField(std::vector<TreeNodePrintField> &fields,
                         std::string_view name,
                         const std::optional<BinaryOpType> &op_type) {
  if (op_type) {
    AppendTreeNodePrintField(fields, name,
                             EnumRefl<BinaryOpType>::GetName(*op_type));
  }
}

inline void AppendTreeNodePrintField(std::vector<TreeNodePrintField> &fields,
                                     std::string_view name,
                                     const std::optional<TypeRef> &type) {
  if (type) {
    AppendTreeNodePrintField(fields, name, *type);
  }
}

class UnaryOp : public Expr {
  ASTNodeImpl(UnaryExpr);
  ExprFieldAsString(op_type);
  ExprPrintFields(operand);

public:
  explicit UnaryOp(UnaryOpType op_type) : op_type(op_type) {}

  UnaryOpType GetOpType() const { return op_type; }

  UnaryOpType op_type;
  std::unique_ptr<ASTNode> operand;
};

class TypeCast : public UnaryOp {
  ASTNodeImpl(TypeCast);
  ExprPrintFields(target_type, operand);

public:
  explicit TypeCast(TypeRef target_type = nullptr)
      : UnaryOp(UnaryOpType::TypeCast), target_type(target_type) {}

  TypeRef target_type;
};

class ImplicitCast : public UnaryOp {
  ASTNodeImpl(ImplicitCast);
  ExprPrintFields(target_type, operand);

public:
  explicit ImplicitCast(TypeRef target_type = nullptr)
      : UnaryOp(UnaryOpType::TypeCast), target_type(target_type) {}

  TypeRef target_type;
};

class BinaryOp : public Expr {
  ASTNodeImpl(BinaryExpr);
  ExprFieldAsString(op_type);
  ExprPrintFields(operation_type, compound_op, left_value_cast_type,
                  result_cast_type, left, right);

public:
  explicit BinaryOp(BinaryOpType op_type) : op_type(op_type) {}

  BinaryOpType GetOpType() const { return op_type; }

  BinaryOpType op_type;
  TypeRef operation_type = nullptr;
  std::optional<BinaryOpType> compound_op;
  std::optional<TypeRef> left_value_cast_type;
  std::optional<TypeRef> result_cast_type;
  std::unique_ptr<ASTNode> left;
  std::unique_ptr<ASTNode> right;
};

class GetField : public UnaryOp {
  ASTNodeImpl(GetField);
  ExprPrintFields(field_name, operand);

public:
  explicit GetField(std::string_view field_name = "")
      : UnaryOp(UnaryOpType::GetField), field_name(field_name) {}

  std::string_view field_name;
};

class PtrGetField : public UnaryOp {
  ASTNodeImpl(PtrGetField);
  ExprPrintFields(field_name, operand);

public:
  explicit PtrGetField(std::string_view field_name = "")
      : UnaryOp(UnaryOpType::GetField), field_name(field_name) {}

  std::string_view field_name;
};
