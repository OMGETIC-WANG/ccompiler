#include "AST.h"
#include "../Util/Tree.h"

static std::string get_str(const ASTNode *node) {
  return std::string(EnumRefl<ASTNodeType>::GetName(node->GetType())) + ": " +
         node->ToString();
}

static size_t get_child_count(const ASTNode *node) {
  return node->GetChildCount();
}

static const ASTNode *get_child(const ASTNode *node, size_t index) {
  return node->GetChild(index);
}

void ASTNode::Print(std::ostream &out) const {
  PrintTree(this, get_child_count, get_child, get_str, out);
}
