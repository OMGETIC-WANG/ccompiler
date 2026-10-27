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

enum class IRValueKind { Null, Constant, LocalReg, Global, Literal };

struct IRValue {
  IRValueKind kind = IRValueKind::Null;
  std::string name;
  TypeRef type;

  static IRValue Constant(TypeRef type, std::string value) {
    return IRValue{IRValueKind::Constant, std::move(value), type};
  }

  static IRValue Literal(TypeRef type, std::string value) {
    return IRValue{IRValueKind::Literal, std::move(value), type};
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
  std::unordered_map<std::string_view, IRValue> variables;
};

class GenContext {
public:
  explicit GenContext(TypeContext &types) : counter(1), types(types) {}

  TypeContext &Types() { return types; }
  const TypeContext &Types() const { return types; }

  std::vector<GenContextLayer> layers;

  std::unordered_map<std::string_view, IRValue> functions;
  IRValue last_function;

  GenContextLayer &CurrLayer() { return layers.back(); }

  void PushLayer() { layers.emplace_back(); }

  void PopLayer() {
    if (layers.size() > 1) {
      layers.pop_back();
    }
  }

  template <auto field> IRValue *ReverseFind(std::string_view name) {
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      auto &layer = *it;
      auto var_it = (layer.*field).find(name);
      if (var_it != (layer.*field).end()) {
        return &var_it->second;
      }
    }
    return nullptr;
  }

  IRValue *FindVariable(std::string_view name) {
    return ReverseFind<&GenContextLayer::variables>(name);
  }

  void AddFunction(std::string_view name, IRValue value) {
    functions[name] = value;
    last_function = value;
  }

  IRValue *FindFunction(std::string_view name) {
    if (auto it = functions.find(name); it != functions.end()) {
      return &it->second;
    }
    return nullptr;
  }

  Counter<> counter;
  std::vector<std::pair<std::string, std::string_view>> literal_strings;
  std::vector<std::string> struct_decls;

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
