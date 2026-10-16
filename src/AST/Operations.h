#pragma once

#include "AST.h"

class FunctionInvocation : public ASTNode {
  ASTNodeImpl(FunctionInvocation);

public:
  size_t GetChildCount() const override { return 1 + arguments.size(); }
  ASTNode *GetChild(size_t index) const override {
    if (index == 0)
      return function.get();
    else if (index - 1 < arguments.size())
      return arguments[index - 1].get();
    return nullptr;
  }

public:
  std::unique_ptr<ASTNode> function;
  std::vector<std::unique_ptr<ASTNode>> arguments;
};

class SequenceEval : public ASTNode {
  ASTNodeImpl(SequenceEval);

public:
  size_t GetChildCount() const override { return expressions.size(); }
  ASTNode *GetChild(size_t index) const override {
    if (index < expressions.size())
      return expressions[index].get();
    return nullptr;
  }

  std::vector<std::unique_ptr<ASTNode>> expressions;
};

// clang-format off
#define OPERATION_TYPE_VALUES(x)\
  x(Assignment)\
  x(Add)\
  x(Sub)\
  x(Mul)\
  x(Div)\
  x(Mod)\
  x(Subscript)\
  x(IncBeforeUse)\
  x(IncAfterUse)\
  x(DecBeforeUse)\
  x(DecAfterUse)\
  x(GetField)\
  x(Deref)
// clang-format on

MAKE_ENUM(OperationType, OPERATION_TYPE_VALUES)
REGISTER_ENUM(OperationType, OPERATION_TYPE_VALUES)

class BinaryOp : public ASTNode {
  ASTNodeImpl(BinaryExpr);

public:
  virtual OperationType GetOpType() const = 0;

  size_t GetChildCount() const override { return 2; }
  ASTNode *GetChild(size_t index) const override {
    if (index == 0)
      return left.get();
    else if (index == 1)
      return right.get();
    return nullptr;
  }

  std::string ToString() const override {
    return std::string(EnumRefl<OperationType>::GetName(GetOpType()));
  }

  std::unique_ptr<ASTNode> left;
  std::unique_ptr<ASTNode> right;
};

class UnaryOp : public ASTNode {
  ASTNodeImpl(UnaryExpr);

public:
  virtual OperationType GetOpType() const = 0;

  size_t GetChildCount() const override { return 1; }
  ASTNode *GetChild(size_t index) const override {
    if (index == 0)
      return operand.get();
    return nullptr;
  }

  std::string ToString() const override {
    return std::string(EnumRefl<OperationType>::GetName(GetOpType()));
  }

  std::unique_ptr<ASTNode> operand;
};

#define OpImpl(type_enum)                                                      \
public:                                                                        \
  OperationType GetOpType() const override {                                   \
    return OperationType::type_enum;                                           \
  }                                                                            \
  constexpr static OperationType __op_type = OperationType::type_enum

class AssignmentOp : public BinaryOp {
  OpImpl(Assignment);
};

class AddOp : public BinaryOp {
  OpImpl(Add);
};

class SubOp : public BinaryOp {
  OpImpl(Sub);
};

class MulOp : public BinaryOp {
  OpImpl(Mul);
};

class DivOp : public BinaryOp {
  OpImpl(Div);
};

class ModOp : public BinaryOp {
  OpImpl(Mod);
};

class SubscriptOp : public BinaryOp {
  OpImpl(Subscript);
};

class GetFieldOp : public BinaryOp {
  OpImpl(GetField);
};

inline std::unique_ptr<BinaryOp> MakeBinaryOp(OperationType op_type) {
  switch (op_type) {
  case OperationType::Assignment:
    return std::make_unique<AssignmentOp>();
  case OperationType::Add:
    return std::make_unique<AddOp>();
  case OperationType::Sub:
    return std::make_unique<SubOp>();
  case OperationType::Mul:
    return std::make_unique<MulOp>();
  case OperationType::Div:
    return std::make_unique<DivOp>();
  case OperationType::Mod:
    return std::make_unique<ModOp>();
  case OperationType::Subscript:
    return std::make_unique<SubscriptOp>();
  case OperationType::GetField:
    return std::make_unique<GetFieldOp>();
  default:
    return nullptr;
  }
}

class IncBeforeUseOp : public UnaryOp {
  OpImpl(IncBeforeUse);
};

class DecBeforeUseOp : public UnaryOp {
  OpImpl(DecBeforeUse);
};

class IncAfterUseOp : public UnaryOp {
  OpImpl(IncAfterUse);
};

class DecAfterUseOp : public UnaryOp {
  OpImpl(DecAfterUse);
};

class DerefOp : public UnaryOp {
  OpImpl(Deref);
};
