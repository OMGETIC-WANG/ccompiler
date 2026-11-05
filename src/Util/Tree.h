#pragma once

#include "Util.h"

#include <format>
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

template <typename Ptr>
concept _TreeNodePtrLike =
    std::is_convertible_v<Ptr, const TreeNode *> || requires(const Ptr &ptr) {
      { ptr.operator->() } -> std::convertible_to<const TreeNode *>;
    };

template <_TreeNodePtrLike Ptr>
struct std::formatter<Ptr> : std::formatter<char> {
  auto format(const Ptr &ptr, std::format_context &ctx) const {
    return std::format_to(ctx.out(), "{}:{}", ptr->GetNodeTypeName(),
                          ptr->ToString());
  }
};

template <typename Elem>
concept _MapElemLike = requires(const Elem &elem) {
  { elem.first } -> std::convertible_to<std::string_view>;
  { elem.second } -> _TreeNodePtrLike;
};

template <_TreeNodePtrLike Ptr>
void AppendTreeNodePrintField(std::vector<TreeNodePrintField> &fields,
                              std::string_view name, const Ptr &node) {
  if (auto ptr = std::to_address(node)) {
    fields.push_back({name, ptr});
  }
}

template <std::ranges::input_range Range>
  requires _TreeNodePtrLike<std::ranges::range_value_t<Range>>
void AppendTreeNodePrintField(std::vector<TreeNodePrintField> &fields,
                              std::string_view name, const Range &nodes) {
  for (const auto &node : nodes) {
    AppendTreeNodePrintField(fields, name, node);
  }
}

template <std::ranges::input_range Range>
  requires _MapElemLike<std::ranges::range_value_t<Range>>
void AppendTreeNodePrintField(std::vector<TreeNodePrintField> &fields,
                              std::string_view name, const Range &map) {
  for (const auto &elem : map) {
    AppendTreeNodePrintField(fields, elem.first, elem.second);
  }
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

template <class T> std::string CastToString(const T &val) {
  using CleanT = std::decay_t<T>;
  if constexpr (std::is_convertible_v<CleanT, std::string_view>) {
    return std::string(val);
  } else if constexpr (requires { std::format("{}", val); }) {
    return std::format("{}", val);
  } else if constexpr (requires { std::to_string(val); }) {
    return std::to_string(val);
  } else {
    return {};
  }
}

#define TreeNodeFieldAsString(field)                                           \
  std::string ToString() const override { return CastToString(this->field); }
