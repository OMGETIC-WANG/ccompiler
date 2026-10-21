#pragma once

#include "AST.h"

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
  x(Eq)

#define UNARY_OP_TYPE_VALUES(x)\
  x(IncBeforeUse)\
  x(IncAfterUse)\
  x(DecBeforeUse)\
  x(DecAfterUse)\
  x(GetField)\
  x(Deref)

#define OPERATION_TYPE_VALUES(x)\
  BINARY_OP_TYPE_VALUES(x)\
  UNARY_OP_TYPE_VALUES(x)
// clang-format on

MAKE_ENUM(OperationType, OPERATION_TYPE_VALUES)
REGISTER_ENUM(OperationType, OPERATION_TYPE_VALUES)

class BinaryOp : public ASTNode {
  ASTNodeImpl(BinaryExpr);
  TreeNodePrintFields(left, right);

public:
  explicit BinaryOp(OperationType op_type) : op_type(op_type) {}

  OperationType GetOpType() const { return op_type; }

  std::string ToString() const override {
    return std::string(EnumRefl<OperationType>::GetName(GetOpType()));
  }

  OperationType op_type;
  std::unique_ptr<ASTNode> left;
  std::unique_ptr<ASTNode> right;
};

class UnaryOp : public ASTNode {
  ASTNodeImpl(UnaryExpr);
  TreeNodePrintFields(operand);

public:
  explicit UnaryOp(OperationType op_type) : op_type(op_type) {}

  OperationType GetOpType() const { return op_type; }

  std::string ToString() const override {
    return std::string(EnumRefl<OperationType>::GetName(GetOpType()));
  }

  OperationType op_type;
  std::unique_ptr<ASTNode> operand;
};
