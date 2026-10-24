#pragma once

#include "ASTNode.h"

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
#define BINARY_OP_TYPE_VALUES(x)\
  x(Assignment)\
  x(Add)\
  x(Sub)\
  x(Mul)\
  x(Div)\
  x(Mod)\
  x(Subscript)\
  x(Eq)\
  x(GetField)

#define UNARY_OP_TYPE_VALUES(x)\
  x(IncBeforeUse)\
  x(IncAfterUse)\
  x(DecBeforeUse)\
  x(DecAfterUse)\
  x(GetField)\
  x(Deref)
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
