#pragma once

#include "../Exception.h"
#include "../Util/EnumRefl.h"
#include "../Util/Util.h"
#include <string_view>

// clang-format off
#define VALID_CONTEXT_ITEM_TYPE_VALUES(x)\
  x(Function)\
  x(Typedef)\
  x(Struct)\
  x(Variable)\
  x(Enum)\
  x(Label)

#define CONTEXT_ITEM_TYPE_VALUES(x) \
  x(Unknown)\
  VALID_CONTEXT_ITEM_TYPE_VALUES(x)
// clang-format on

MAKE_ENUM(ContextItemType, CONTEXT_ITEM_TYPE_VALUES)
REGISTER_ENUM(ContextItemType, CONTEXT_ITEM_TYPE_VALUES)

#define __EXTRACT_ITEM_CONTAINER(x)                                            \
  std::vector<std::string_view> x##s;                                          \
  void Add##x(std::string_view name) {                                         \
    if (!Contains(x##s, name))                                                 \
      x##s.push_back(name);                                                    \
  }

#define __EXTRACT_ADD_ITEM_FUNC_CASE(x)                                        \
  case ContextItemType::x:                                                     \
    if (!Contains(x##s, name))                                                 \
      x##s.push_back(name);                                                    \
    break;

#define __EXTRACT_GET_ITEM_TYPE_FUNC_IF(x)                                     \
  if (Contains(x##s, name))                                                    \
    return ContextItemType::x;

#define __EXTRACT_ADD_ITEM_FUNC(x)                                             \
  void Add##x(std::string_view name) {                                         \
    if (layers.empty()) {                                                      \
      RAISE_ERROR("No context layer to add item");                             \
    }                                                                          \
    layers.back().Add##x(name);                                                \
  }

struct CompileContextLayer {
  VALID_CONTEXT_ITEM_TYPE_VALUES(__EXTRACT_ITEM_CONTAINER)

  void AddItem(std::string_view name, ContextItemType type) {
    switch (type) {
      VALID_CONTEXT_ITEM_TYPE_VALUES(__EXTRACT_ADD_ITEM_FUNC_CASE)
    default:
      RAISE_ERROR("Unexpected ContextItemType");
    }
  }

  ContextItemType GetTypeOf(std::string_view name) const {
    VALID_CONTEXT_ITEM_TYPE_VALUES(__EXTRACT_GET_ITEM_TYPE_FUNC_IF)
    return ContextItemType::Unknown;
  }
};

// A collection of symbol table for AST building, not storaging accurate type
// info
class CompileContext {
public:
  std::vector<CompileContextLayer> layers;

  VALID_CONTEXT_ITEM_TYPE_VALUES(__EXTRACT_ADD_ITEM_FUNC)

  void AddItem(std::string_view name, ContextItemType type) {
    if (layers.empty()) {
      RAISE_ERROR("No context layer to add item");
    }
    layers.back().AddItem(name, type);
  }

  CompileContextLayer &CurrentLayer() { return layers.back(); }

  void PushLayer() { layers.emplace_back(); }
  void PopLayer() {
    if (layers.empty()) {
      RAISE_ERROR("No context layer to remove");
    }
    layers.pop_back();
  }

  ContextItemType GetTypeOf(std::string_view name) const {
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      ContextItemType type = it->GetTypeOf(name);
      if (type != ContextItemType::Unknown) {
        return type;
      }
    }
    return ContextItemType::Unknown;
  }

  bool Is(std::string_view name, ContextItemType type) const {
    return GetTypeOf(name) == type;
  }
};

class CompileContextLayerGuard {
public:
  CompileContextLayerGuard(CompileContext &context) : _context(context) {
    _context.PushLayer();
  }
  ~CompileContextLayerGuard() { _context.PopLayer(); }

private:
  CompileContext &_context;
};
