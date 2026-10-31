#pragma once

#include "ASTNode.h"
#include "Type.h"

class FunctionInvocation : public ASTNode {
  ASTNodeImpl(FunctionInvocation);
  TreeNodePrintFields(function, arguments);

public:
  std::unique_ptr<ASTNode> function;
  std::vector<std::unique_ptr<ASTNode>> arguments;
};

class SequenceEval : public ASTNode {
  ASTNodeImpl(SequenceEval);
  TreeNodePrintFields(expressions);

public:
  std::vector<std::unique_ptr<ASTNode>> expressions;
};

// clang-format off
#define BINARY_COMPOUNT_ASSIGN_OP_VALUES(x)\
  x(AddAssign)\
  x(SubAssign)\
  x(MulAssign)\
  x(DivAssign)\
  x(ModAssign)\
  x(BitAndAssign)\
  x(BitOrAssign)\
  x(BitXorAssign)\
  x(LShiftAssign)\
  x(RShiftAssign)

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
  BINARY_COMPOUNT_ASSIGN_OP_VALUES(x)\
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

class BinaryOp : public ASTNode {
  ASTNodeImpl(BinaryExpr);
  TreeNodeFieldAsString(op_type);
  TreeNodePrintFields(left, right);

public:
  explicit BinaryOp(BinaryOpType op_type) : op_type(op_type) {}

  BinaryOpType GetOpType() const { return op_type; }

  BinaryOpType op_type;
  std::unique_ptr<ASTNode> left;
  std::unique_ptr<ASTNode> right;
};

class UnaryOp : public ASTNode {
  ASTNodeImpl(UnaryExpr);
  TreeNodeFieldAsString(op_type);
  TreeNodePrintFields(operand);

public:
  explicit UnaryOp(UnaryOpType op_type) : op_type(op_type) {}

  UnaryOpType GetOpType() const { return op_type; }

  UnaryOpType op_type;
  std::unique_ptr<ASTNode> operand;
};

class TypeCast : public UnaryOp {
  ASTNodeImpl(TypeCast);
  TreeNodePrintFields(target_type, operand);

public:
  explicit TypeCast(TypeRef target_type = nullptr)
      : UnaryOp(UnaryOpType::TypeCast), target_type(target_type) {}

  TypeRef target_type;
};

class GetField : public UnaryOp {
  ASTNodeImpl(GetField);
  TreeNodePrintFields(field_name, operand);

public:
  explicit GetField(std::string_view field_name = "")
      : UnaryOp(UnaryOpType::GetField), field_name(field_name) {}

  std::string_view field_name;
};

class PtrGetField : public UnaryOp {
  ASTNodeImpl(PtrGetField);
  TreeNodePrintFields(field_name, operand);

public:
  explicit PtrGetField(std::string_view field_name = "")
      : UnaryOp(UnaryOpType::GetField), field_name(field_name) {}

  std::string_view field_name;
};
