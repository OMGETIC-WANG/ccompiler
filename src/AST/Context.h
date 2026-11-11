#pragma once

#include "../Exception.h"
#include "../SourceManager.h"
#include "../Util/EnumRefl.h"
#include "../Util/Util.h"
#include "TypeContext.h"
#include <string_view>
#include <unordered_map>
#include <vector>

// clang-format off
#define TYPED_CONTEXT_ITEM_TYPE_VALUES(x)\
  x(Function)\
  x(Typedef)\
  x(Struct)\
  x(Union)\
  x(Variable)\
  x(Enum)

#define CONTEXT_ITEM_TYPE_VALUES(x)\
  TYPED_CONTEXT_ITEM_TYPE_VALUES(x)\
  x(Unknown)\
  x(Label)
// clang-format on

MAKE_ENUM(ContextItemType, CONTEXT_ITEM_TYPE_VALUES)
REGISTER_ENUM(ContextItemType, CONTEXT_ITEM_TYPE_VALUES)

#define MAKE_CompileContextLayer_DECL_AND_Add_x(x)                             \
  std::unordered_map<std::string_view, TypeRef> x;                             \
  void Add##x(std::string_view name, TypeRef type) { x[name] = type; }

#define MAKE_CompileContextLayer_GetTypeOf_CASE(x)                             \
  if (x.contains(name)) {                                                      \
    return ContextItemType::x;                                                 \
  }

struct CompileContextLayer {
  TYPED_CONTEXT_ITEM_TYPE_VALUES(MAKE_CompileContextLayer_DECL_AND_Add_x)

  std::vector<std::string_view> labels;
  void AddLabel(std::string_view name) { labels.push_back(name); }

  ContextItemType GetTypeOf(std::string_view name) const {
    TYPED_CONTEXT_ITEM_TYPE_VALUES(MAKE_CompileContextLayer_GetTypeOf_CASE);
    if (std::find(labels.begin(), labels.end(), name) != labels.end()) {
      return ContextItemType::Label;
    }
    return ContextItemType::Unknown;
  }
};

#define MAKE_CompileContext_Add_x(x)                                           \
  void Add##x(std::string_view name, TypeRef type) {                           \
    layers.back().x[name] = type;                                              \
  }

#define MAKE_CompileContext_Find_x(x)                                          \
  TypeRef Find##x(std::string_view name) const {                               \
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {               \
      auto found = it->x.find(name);                                           \
      if (found != it->x.end()) {                                              \
        return found->second;                                                  \
      }                                                                        \
    }                                                                          \
    return nullptr;                                                            \
  }

// Lexical symbol table and name-to-type bindings used while building the AST.
class CompileContext {
public:
  explicit CompileContext(TypeContext &types, SourceManager &source_manager)
      : _types(types), _source_manager(source_manager), _next_scope_id(1) {}

  std::vector<CompileContextLayer> layers;

  TYPED_CONTEXT_ITEM_TYPE_VALUES(MAKE_CompileContext_Add_x)
  void AddLabel(std::string_view name) { layers.back().AddLabel(name); }

  CompileContextLayer &CurrentLayer() { return layers.back(); }

  const CompileContextLayer &CurrentLayer() const { return layers.back(); }

  TypeContext &Types() { return _types; }
  const TypeContext &Types() const { return _types; }

  SourceManager &Sources() { return _source_manager; }
  const SourceManager &Sources() const { return _source_manager; }

  ScopeId CurrentScopeId() { return _next_scope_id++; }

  TYPED_CONTEXT_ITEM_TYPE_VALUES(MAKE_CompileContext_Find_x)

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

  TypeRef GetType(const TypeSpecifier &spec) const {
    TypeRef res;
    if (spec.is_builtin) {
      res = Types().Builtin(spec.type_name);
    } else if (spec.is_struct) {
      res = FindStruct(spec.type_name);
    } else if (spec.is_union) {
      res = FindUnion(spec.type_name);
    } else if (spec.is_enum) {
      res = FindEnum(spec.type_name);
    } else if (spec.is_named_type) {
      TypeRef type = FindTypedef(spec.type_name);
      if (type) {
        res = type;
      } else {
        type = FindEnum(spec.type_name);
        if (type) {
          res = type;
        } else {
          res = nullptr;
        }
      }
    }
    if (res) {
      if (spec.is_unsigned) {
        res = ApplyTypeTag(res, TypeTag::Unsigned);
      }
      if (spec.is_const) {
        res = ApplyTypeTag(res, TypeTag::Const);
      }
      return res;
    }
    return nullptr;
  }

private:
  TypeContext &_types;
  SourceManager &_source_manager;
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
