#include "AST.h"
#include "Operations.h"

std::string_view ASTNode::GetNodeTypeName() const {
  return EnumRefl<ASTNodeType>::GetName(GetType());
}

bool IsConstantFoldable(const ASTNode *node) {
  if (node == nullptr) {
    return false;
  }

  if (node->DynCast<LiteralNum>() || node->DynCast<LiteralString>()) {
    return true;
  }
  if (auto *bin_expr = node->DynCast<BinaryOp>()) {
    return IsConstantFoldable(bin_expr->left.get()) &&
           IsConstantFoldable(bin_expr->right.get());
  }
  if (auto *unary_expr = node->DynCast<UnaryOp>()) {
    return IsConstantFoldable(unary_expr->operand.get());
  }
  if (auto *cond_expr = node->DynCast<ConditionalExpr>()) {
    return IsConstantFoldable(cond_expr->condition.get()) &&
           IsConstantFoldable(cond_expr->true_expr.get()) &&
           IsConstantFoldable(cond_expr->false_expr.get());
  }
  return false;
}
