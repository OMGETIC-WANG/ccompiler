#pragma once

#include <AST/Type.h>
#include <Util/EnumRefl.h>
#include <Util/Tree.h>
#include <concepts>
#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <yaml-cpp/yaml.h>

class VerifyContext {
public:
  std::vector<std::string> path_stack;

  std::string GetPathString() const {
    std::string path;
    for (const auto &segment : path_stack) {
      if (!path.empty()) {
        path += ".";
      }
      path += segment;
    }
    return path;
  }

  void AddMessage(std::string_view msg) {
    messages.push_back(std::format("{}: {}", GetPathString(), msg));
  }

  std::vector<std::string> messages;
};

class VerifyContextGuard {
public:
  VerifyContextGuard(VerifyContext &path, std::string_view child) : path(path) {
    path.path_stack.push_back(std::string(child));
  }

  ~VerifyContextGuard() { path.path_stack.pop_back(); }

  VerifyContext &path;
};

void VerifyTreeNode(const TreeNode *tree_node, const YAML::Node &expected,
                    VerifyContext &context);

void VerifyFields(const YAML::Node &expected,
                  std::initializer_list<std::string_view> allowed_fields,
                  VerifyContext &context);

void VerifyTypeRef(TypeRef value, const YAML::Node &expected,
                   VerifyContext &context);

std::vector<std::string> FindMissingVerifierNames();

template <_TreeNodePtrLike Ptr> auto GetPtr(const Ptr &ptr) {
  if constexpr (requires { ptr.get(); }) {
    return ptr.get();
  } else {
    return ptr;
  }
}

template <_TreeNodePtrLike Ptr>
inline void VerifyField(std::string_view field_name, const Ptr &tree_node,
                        const YAML::Node &parent, VerifyContext &context) {
  YAML::Node field_node = parent[field_name];
  if (!field_node) {
    return;
  }
  VerifyContextGuard guard(context, field_name);
  auto *node = GetPtr(tree_node);
  if (field_node.IsNull()) {
    if (node != nullptr) {
      context.AddMessage("expected null");
    }
    return;
  }
  VerifyTreeNode(node, field_node, context);
}

inline void VerifyField(std::string_view field_name, TypeRef value,
                        const YAML::Node &parent, VerifyContext &context) {
  YAML::Node field_node = parent[field_name];
  if (!field_node) {
    return;
  }
  VerifyContextGuard guard(context, field_name);
  VerifyTypeRef(value, field_node, context);
}

inline void VerifyField(std::string_view field_name,
                        const std::vector<TypeRef> &value,
                        const YAML::Node &parent, VerifyContext &context) {
  YAML::Node field_node = parent[field_name];
  if (!field_node) {
    return;
  }
  VerifyContextGuard guard(context, field_name);
  if (!field_node.IsSequence()) {
    context.AddMessage(
        std::format("expected sequence for field '{}'", field_name));
    return;
  }
  if (field_node.size() != value.size()) {
    context.AddMessage(std::format("expected {} items for field '{}', got {}",
                                   value.size(), field_name,
                                   field_node.size()));
    return;
  }
  for (size_t i = 0; i < value.size(); ++i) {
    VerifyContextGuard item_guard(context, std::to_string(i));
    VerifyTypeRef(value[i], field_node[i], context);
  }
}

template <typename Str>
  requires std::is_constructible_v<std::string_view, const Str &>
inline void VerifyField(std::string_view field_name, const Str &value,
                        const YAML::Node &parent, VerifyContext &context) {
  YAML::Node field_node = parent[field_name];
  if (!field_node) {
    return;
  }
  VerifyContextGuard guard(context, field_name);
  if (!field_node.IsScalar()) {
    context.AddMessage(
        std::format("expected scalar for field '{}'", field_name));
    return;
  }
  const std::string_view actual_value(value);
  if (field_node.Scalar() != actual_value) {
    context.AddMessage(
        std::format("expected value '{}' for field '{}', got '{}'",
                    actual_value, field_name, field_node.Scalar()));
  }
}

template <std::integral Int>
  requires(!std::same_as<Int, bool>)
inline void VerifyField(std::string_view field_name, Int value,
                        const YAML::Node &parent, VerifyContext &context) {
  YAML::Node field_node = parent[field_name];
  if (!field_node) {
    return;
  }
  VerifyContextGuard guard(context, field_name);
  if (!field_node.IsScalar()) {
    context.AddMessage(
        std::format("expected scalar for field '{}'", field_name));
    return;
  }
  try {
    Int expected = field_node.as<Int>();
    if (expected != value) {
      context.AddMessage(std::format("expected {}, got {}", value, expected));
    }
  } catch (const YAML::BadConversion &) {
    context.AddMessage(std::format("expected integer {}", value));
  }
}

inline void VerifyField(std::string_view field_name, bool value,
                        const YAML::Node &parent, VerifyContext &context) {
  YAML::Node field_node = parent[field_name];
  if (!field_node) {
    return;
  }
  VerifyContextGuard guard(context, field_name);
  if (!field_node.IsScalar()) {
    context.AddMessage(
        std::format("expected scalar for field '{}'", field_name));
    return;
  }
  try {
    bool expected = field_node.as<bool>();
    if (expected != value) {
      context.AddMessage(std::format("expected {}, got {}", value, expected));
    }
  } catch (const YAML::BadConversion &) {
    context.AddMessage(std::format("expected boolean {}", value));
  }
}

inline void VerifyField(std::string_view field_name,
                        const std::optional<size_t> &value,
                        const YAML::Node &parent, VerifyContext &context) {
  YAML::Node field_node = parent[field_name];
  if (!field_node) {
    return;
  }
  VerifyContextGuard guard(context, field_name);
  if (field_node.IsNull()) {
    if (value.has_value()) {
      context.AddMessage(std::format("expected null, got {}", value.value()));
    }
    return;
  }
  if (!value.has_value()) {
    context.AddMessage("expected an integer, got null");
    return;
  }
  if (!field_node.IsScalar()) {
    context.AddMessage("expected an integer");
    return;
  }
  try {
    size_t expected = field_node.as<size_t>();
    if (expected != value.value()) {
      context.AddMessage(
          std::format("expected {}, got {}", value.value(), expected));
    }
  } catch (const YAML::BadConversion &) {
    context.AddMessage(std::format("expected integer {}", value.value()));
  }
}

template <std::ranges::range Range>
  requires _TreeNodePtrLike<std::ranges::range_value_t<Range>>
inline void VerifyField(std::string_view field_name, const Range &value,
                        const YAML::Node &parent, VerifyContext &context) {
  YAML::Node field_node = parent[field_name];
  if (!field_node) {
    return;
  }
  VerifyContextGuard guard(context, field_name);
  if (!field_node.IsSequence()) {
    context.AddMessage(
        std::format("expected sequence for field '{}'", field_name));
    return;
  }
  if (field_node.size() != std::ranges::size(value)) {
    context.AddMessage(std::format("expected {} items for field '{}', got {}",
                                   std::ranges::size(value), field_name,
                                   field_node.size()));
    return;
  }
  size_t index = 0;
  for (const auto &item : value) {
    YAML::Node item_node = field_node[index];
    if (!item_node) {
      context.AddMessage(std::format("missing item at index {} for field '{}'",
                                     index, field_name));
      ++index;
      continue;
    }
    VerifyContextGuard guard(context, std::to_string(index));
    auto *node = GetPtr(item);
    if (item_node.IsNull()) {
      if (node != nullptr) {
        context.AddMessage("expected null");
      }
    } else {
      VerifyTreeNode(node, item_node, context);
    }
    ++index;
  }
}

template <EnumReflRegistered E>
inline void VerifyField(std::string_view field_name, E value,
                        const YAML::Node &parent, VerifyContext &context) {
  VerifyField(field_name, std::string(EnumRefl<E>::GetName(value)), parent,
              context);
}

#define BEGIN_VERIFY_NODE(NODE)                                                \
  void Verify##NODE(const TreeNode *tree_node, const YAML::Node &expected,     \
                    VerifyContext &context) {                                  \
    const NODE *node = static_cast<const NODE *>(tree_node);

#define VERIFY_FIELDS(...) VerifyFields(expected, {__VA_ARGS__}, context);

#define VERIFY_FIELD(FIELD) VerifyField(#FIELD, node->FIELD, expected, context);

#define END_VERIFY_NODE() }
