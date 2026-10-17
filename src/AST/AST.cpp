#include "AST.h"

std::string_view ASTNode::GetNodeTypeName() const {
  return EnumRefl<ASTNodeType>::GetName(GetType());
}
