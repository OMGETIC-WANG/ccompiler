#include "Tree.h"

#include <variant>

template <class Node, class FnGetChildren, class FnGetString>
static void PrintTreeChildren(const Node *node,
                              const FnGetChildren &get_children,
                              const FnGetString &get_string, std::ostream &out,
                              const std::string &prefix) {
  const auto children = get_children(node);
  for (size_t child_index = 0; child_index < children.size(); ++child_index) {
    const auto *child = &children[child_index];
    const bool is_last = child_index + 1 == children.size();

    out << prefix << (is_last ? "`-" : "+-") << get_string(child) << '\n';

    const std::string next_prefix = prefix + (is_last ? "  " : "| ");
    PrintTreeChildren(child, get_children, get_string, out, next_prefix);
  }
}

template <class Node, class FnGetChildren, class FnGetString>
static void PrintTree(const Node *root, const FnGetChildren &get_children,
                      const FnGetString &get_string, std::ostream &out) {
  if (!root)
    return;

  out << get_string(root) << '\n';
  PrintTreeChildren(root, get_children, get_string, out, {});
}

static std::string
GetTreeNodePrintFieldString(const TreeNodePrintField *field) {
  std::string result;
  if (!field->name.empty()) {
    result += std::format("[{}] ", field->name);
  }

  if (const auto *node = std::get_if<const TreeNode *>(&field->value)) {
    result += std::format("{} 0x{:x} {}", (*node)->GetNodeTypeName(),
                          (uintptr_t)(*node), (*node)->ToString());
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
