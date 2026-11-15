#pragma once

#include <concepts>
#include <string>
#include <string_view>
#include <type_traits>

class TreeNode {
public:
  virtual ~TreeNode() = default;

  virtual std::string_view GetNodeTypeName() const = 0;

  virtual std::string ToString() const { return {}; }
};

template <typename Ptr>
concept _TreeNodePtrLike =
    std::is_convertible_v<Ptr, const TreeNode *> || requires(const Ptr &ptr) {
      { ptr.operator->() } -> std::convertible_to<const TreeNode *>;
    };
