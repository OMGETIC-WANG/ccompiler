#pragma once

#include <iostream>
#include <string>

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
