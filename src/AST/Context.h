#pragma once

#include "../Exception.h"
#include "../Util/EnumRefl.h"
#include "../Util/Util.h"
#include "TypeContext.h"
#include <string_view>
#include <unordered_map>
#include <vector>

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
  ScopeId scope_id = 0;

  VALID_CONTEXT_ITEM_TYPE_VALUES(__EXTRACT_ITEM_CONTAINER)

  std::unordered_map<std::string_view, TypeRef> typedef_bindings;

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

// Lexical symbol table and name-to-type bindings used while building the AST.
class CompileContext {
public:
  explicit CompileContext(TypeContext &types)
      : _types(types), _next_scope_id(1) {}

  std::vector<CompileContextLayer> layers;

  VALID_CONTEXT_ITEM_TYPE_VALUES(__EXTRACT_ADD_ITEM_FUNC)

  void AddTypedef(std::string_view name, TypeRef type) {
    AddTypedef(name);
    layers.back().typedef_bindings[name] = type;
  }

  void AddItem(std::string_view name, ContextItemType type) {
    if (layers.empty()) {
      RAISE_ERROR("No context layer to add item");
    }
    layers.back().AddItem(name, type);
  }

  CompileContextLayer &CurrentLayer() { return layers.back(); }

  const CompileContextLayer &CurrentLayer() const { return layers.back(); }

  TypeContext &Types() { return _types; }
  const TypeContext &Types() const { return _types; }

  ScopeId CurrentScopeId() const { return CurrentLayer().scope_id; }

  TypeRef FindType(std::string_view name) const {
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      auto found = it->typedef_bindings.find(name);
      if (found != it->typedef_bindings.end()) {
        return found->second;
      }
    }
    return nullptr;
  }

  void PushLayer() {
    layers.emplace_back();
    layers.back().scope_id = _next_scope_id++;
  }
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

private:
  TypeContext &_types;
  ScopeId _next_scope_id;
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
