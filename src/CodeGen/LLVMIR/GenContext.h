#pragma once

#include "../../AST/TypeContext.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

template <typename Integer = size_t> class Counter {
public:
  Counter(Integer start = Integer{}) : _curr(start) {}

  Integer operator()() { return _curr++; }

  void Set(Integer value) { _curr = value; }

private:
  Integer _curr;
};

enum class IRValueKind { Null, Constant, LocalReg, Global };

struct IRValue {
  IRValueKind kind = IRValueKind::Null;
  std::string name;
  TypeRef type;

  static IRValue Constant(TypeRef type, std::string value) {
    return IRValue{IRValueKind::Constant, std::move(value), type};
  }

  static IRValue LocalReg(TypeRef type, std::string name) {
    return IRValue{IRValueKind::LocalReg, std::move(name), type};
  }

  static IRValue LocalReg(TypeRef type, size_t id) {
    return IRValue{IRValueKind::LocalReg, std::format("%{}", id), type};
  }

  static IRValue Global(TypeRef type, std::string name) {
    return IRValue{IRValueKind::Global, std::move(name), type};
  }
};

class GenContextLayer {
public:
  std::unordered_map<std::string_view, TypeRef> functions;
  std::unordered_map<std::string_view, IRValue> variables;
};

class GenContext {
public:
  explicit GenContext(TypeContext &types) : counter(1), types(types) {}

  TypeContext &Types() { return types; }
  const TypeContext &Types() const { return types; }

  std::vector<GenContextLayer> layers;

  GenContextLayer &CurrLayer() { return layers.back(); }

  void PushLayer() { layers.emplace_back(); }

  void PopLayer() {
    if (layers.size() > 1) {
      layers.pop_back();
    }
  }

  IRValue *FindVariable(std::string_view name) {
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      auto &layer = *it;
      auto var_it = layer.variables.find(name);
      if (var_it != layer.variables.end()) {
        return &var_it->second;
      }
    }
    return nullptr;
  }

  Counter<> counter;
  std::vector<std::pair<std::string, std::string_view>> literal_strings;

  TypeContext &types;
};

class GenContextLayerGuard {
public:
  explicit GenContextLayerGuard(GenContext &context) : _context(context) {
    _context.PushLayer();
  }
  ~GenContextLayerGuard() { _context.PopLayer(); }

private:
  GenContext &_context;
};
