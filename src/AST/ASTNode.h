#pragma once

#include <string_view>

#include "../Util/EnumRefl.h"
#include "../Util/Tree.h"

// clang-format off
#define AST_NODE_TYPE_VALUES(x)\
  x(Unknown)\
  x(TranslationUnit)\
  x(FunctionDecl)\
  x(StructDecl)\
  x(LiteralString)\
  x(LiteralNum)\
  x(BinaryExpr)\
  x(UnaryExpr)\
  x(DeclStmt)\
  x(VarDecl)\
  x(TypedefDecl)\
  x(VarRef)\
  x(FunctionInvocation)\
  x(SequenceEval)\
  x(CompoundStmt)\
  x(IfStmt)\
  x(ReturnStmt)\
  x(WhileStmt)\
  x(EnumDecl)\
  x(EnumElemDecl)\
  x(LabelStmt)\
  x(GotoStmt)
// clang-format on

MAKE_ENUM(ASTNodeType, AST_NODE_TYPE_VALUES)
REGISTER_ENUM(ASTNodeType, AST_NODE_TYPE_VALUES)

class ASTNode;

class ASTNode : public TreeNode {
public:
  virtual ~ASTNode() = default;

  virtual ASTNodeType GetType() const = 0;

  std::string_view GetNodeTypeName() const override;
};

template <class T> ASTNodeType ASTNodeTypeOf() { return T::__type; }

#define ASTNodeImpl(type_enum)                                                 \
public:                                                                        \
  ASTNodeType GetType() const override { return ASTNodeType::type_enum; }      \
  constexpr static ASTNodeType __type = ASTNodeType::type_enum
