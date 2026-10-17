#pragma once

#include "Util.h"

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

class TreeNode;

struct TreeNodePrintField {
  std::string_view name;
  std::variant<const TreeNode *, std::string> value;
};

class TreeNode {
public:
  virtual ~TreeNode() = default;

  virtual std::string_view GetNodeTypeName() const = 0;

  virtual std::string ToString() const { return {}; }

  virtual std::vector<TreeNodePrintField> GetPrintFields() const { return {}; }

  void Print(std::ostream &out) const;
};

template <class Node, class Deleter>
  requires std::is_base_of_v<TreeNode, Node>
void AppendTreeNodePrintField(std::vector<TreeNodePrintField> &fields,
                              std::string_view name,
                              const std::unique_ptr<Node, Deleter> &node) {
  if (node)
    fields.push_back({name, node.get()});
}

template <class Node>
  requires std::is_base_of_v<TreeNode, Node>
void AppendTreeNodePrintField(std::vector<TreeNodePrintField> &fields,
                              std::string_view name,
                              const std::shared_ptr<Node> &node) {
  if (node)
    fields.push_back({name, node.get()});
}

template <class Node, class Deleter, class Allocator>
  requires std::is_base_of_v<TreeNode, Node>
void AppendTreeNodePrintField(
    std::vector<TreeNodePrintField> &fields, std::string_view name,
    const std::vector<std::unique_ptr<Node, Deleter>, Allocator> &nodes) {
  for (const auto &node : nodes)
    AppendTreeNodePrintField(fields, name, node);
}

inline void AppendTreeNodePrintField(std::vector<TreeNodePrintField> &fields,
                                     std::string_view name,
                                     std::string_view value) {
  if (!value.empty())
    fields.push_back({name, std::string(value)});
}

#define __AddTreeNodePrintField(field)                                         \
  AppendTreeNodePrintField(print_fields, #field, this->field);

#define TreeNodePrintFields(...)                                               \
public:                                                                        \
  std::vector<TreeNodePrintField> GetPrintFields() const override {            \
    std::vector<TreeNodePrintField> print_fields;                              \
    FOR_EACH(__AddTreeNodePrintField __VA_OPT__(, ) __VA_ARGS__)               \
    return print_fields;                                                       \
  }

#define TreeNodePrintFieldsDecl()                                              \
public:                                                                        \
  std::vector<TreeNodePrintField> GetPrintFields() const override

#define TreeNodePrintFieldsImpl(node, ...)                                     \
  std::vector<TreeNodePrintField> node::GetPrintFields() const {               \
    std::vector<TreeNodePrintField> print_fields;                              \
    FOR_EACH(__AddTreeNodePrintField __VA_OPT__(, ) __VA_ARGS__)               \
    return print_fields;                                                       \
  }

#define TreeNodeFieldAsString(field)                                           \
  std::string ToString() const override { return std::string(field); }

template <class Node, class FnGetChildren, class FnGetString>
void PrintTreeChildren(const Node *node, const FnGetChildren &get_children,
                       const FnGetString &get_string, std::ostream &out,
                       const std::string &prefix) {
  const auto children = get_children(node);
  for (size_t child_index = 0; child_index < children.size(); ++child_index) {
    const auto *child = &children[child_index];
    const bool is_last = child_index + 1 == children.size();

    out << prefix << "|\n";
    out << prefix << "+-" << get_string(child) << '\n';

    const std::string next_prefix = prefix + (is_last ? "  " : "| ");
    PrintTreeChildren(child, get_children, get_string, out, next_prefix);
  }
}

template <class Node, class FnGetChildren, class FnGetString>
void PrintTree(const Node *root, const FnGetChildren &get_children,
               const FnGetString &get_string, std::ostream &out) {
  if (!root)
    return;

  out << get_string(root) << '\n';
  PrintTreeChildren(root, get_children, get_string, out, {});
}

template <class T, class FnGetChildCount, class FnGetChild, class FnGetStr>
void __PrintTreeInternal(const T *node, const FnGetChildCount &get_child_count,
                         const FnGetChild &get_child, const FnGetStr &get_str,
                         std::ostream &out, std::string &prefix) {
  if (!node)
    return;

  size_t child_count = get_child_count(node);
  for (size_t i = 0; i < child_count; ++i) {
    const auto child = get_child(node, i);
    if (!child)
      continue;

    bool is_last = (i + 1 == child_count);

    if (i == 0) {
      out << prefix << "|\n";
    }

    out << prefix << "+-" << get_str(child) << "\n";

    std::string next_prefix = prefix + (is_last ? "  " : "| ");

    __PrintTreeInternal(child, get_child_count, get_child, get_str, out,
                        next_prefix);

    if (!is_last) {
      out << prefix << "|\n";
    }
  }
}

template <class T, class FnGetChildCount, class FnGetChild, class FnGetStr>
void PrintTree(const T *root, const FnGetChildCount &get_child_count,
               const FnGetChild &get_child, const FnGetStr &get_str,
               std::ostream &out) {
  if (!root)
    return;

  out << get_str(root) << "\n";

  std::string prefix;
  __PrintTreeInternal(root, get_child_count, get_child, get_str, out, prefix);
}
