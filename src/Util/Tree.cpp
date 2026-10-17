#include "Tree.h"

#include <variant>

static std::string GetTreeNodePrintFieldString(
    const TreeNodePrintField *field) {
  std::string result;
  if (!field->name.empty()) {
    result += '[';
    result += field->name;
    result += "] ";
  }

  if (const auto *node = std::get_if<const TreeNode *>(&field->value)) {
    result += (*node)->GetNodeTypeName();
    const auto summary = (*node)->ToString();
    if (!summary.empty()) {
      result += ' ';
      result += summary;
    }
  } else {
    result += std::get<std::string>(field->value);
  }
  return result;
}

static std::vector<TreeNodePrintField>
GetTreeNodePrintChildren(const TreeNodePrintField *field) {
  if (const auto *node = std::get_if<const TreeNode *>(&field->value))
    return (*node)->GetPrintFields();
  return {};
}

void TreeNode::Print(std::ostream &out) const {
  const TreeNodePrintField root{{}, this};
  PrintTree(&root, GetTreeNodePrintChildren, GetTreeNodePrintFieldString, out);
}
