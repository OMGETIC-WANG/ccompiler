#include "AST/Operations.h"

#include <gtest/gtest.h>

namespace {

template <typename T> void ExpectUnaryOpNode(T &node) {
  ASTNode *base = &node;
  const ASTNode *const_base = &node;

  EXPECT_TRUE(base->Is<T>());
  EXPECT_TRUE(base->Is<UnaryOp>());
  EXPECT_TRUE(base->Is<Expr>());
  EXPECT_EQ(base->DynCast<T>(), &node);
  EXPECT_EQ(base->DynCast<UnaryOp>(), &node);
  EXPECT_EQ(base->DynCast<Expr>(), &node);
  EXPECT_EQ(base->As<UnaryOp>(), &node);
  EXPECT_EQ(base->As<Expr>(), &node);

  EXPECT_TRUE(const_base->Is<T>());
  EXPECT_TRUE(const_base->Is<UnaryOp>());
  EXPECT_TRUE(const_base->Is<Expr>());
  EXPECT_EQ(const_base->DynCast<T>(), &node);
  EXPECT_EQ(const_base->DynCast<UnaryOp>(), &node);
  EXPECT_EQ(const_base->DynCast<Expr>(), &node);
  EXPECT_EQ(const_base->As<UnaryOp>(), &node);
  EXPECT_EQ(const_base->As<Expr>(), &node);
}

} // namespace

TEST(ASTNodeCasting, SupportsConcreteBaseTypes) {
  TypeCast type_cast;
  ImplicitCast implicit_cast;
  GetField get_field;
  PtrGetField ptr_get_field;

  ExpectUnaryOpNode(type_cast);
  ExpectUnaryOpNode(implicit_cast);
  ExpectUnaryOpNode(get_field);
  ExpectUnaryOpNode(ptr_get_field);
}

TEST(ASTNodeCasting, RejectsUnrelatedNodeTypes) {
  LiteralNum literal;
  UnaryOp unary(UnaryOpType::UnaryPlus);

  EXPECT_FALSE(literal.Is<BinaryOp>());
  EXPECT_EQ(literal.DynCast<BinaryOp>(), nullptr);

  EXPECT_TRUE(unary.Is<UnaryOp>());
  EXPECT_FALSE(unary.Is<TypeCast>());
  EXPECT_EQ(unary.DynCast<TypeCast>(), nullptr);
}

TEST(ASTNodeCasting, SupportsCategoryBases) {
  VarDecl variable;
  LiteralNum literal;
  EmptyStmt empty;

  ASTNode *variable_node = &variable;
  ASTNode *literal_node = &literal;
  ASTNode *statement_node = &empty;

  EXPECT_TRUE(variable_node->Is<NamedDecl>());
  EXPECT_TRUE(variable_node->Is<ValueDecl>());
  EXPECT_EQ(variable_node->DynCast<NamedDecl>(), &variable);
  EXPECT_EQ(variable_node->DynCast<ValueDecl>(), &variable);

  EXPECT_TRUE(literal_node->Is<Expr>());
  EXPECT_EQ(literal_node->DynCast<Expr>(), &literal);

  EXPECT_TRUE(statement_node->Is<Stmt>());
  EXPECT_FALSE(statement_node->Is<Expr>());
  EXPECT_EQ(statement_node->DynCast<Stmt>(), &empty);
  EXPECT_EQ(statement_node->DynCast<Expr>(), nullptr);
}
