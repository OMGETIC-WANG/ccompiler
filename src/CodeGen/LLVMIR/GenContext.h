#pragma once

#include "../../AST/TypeContext.h"
#include "Value.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace llvm {
class Function;
class FunctionType;
class LLVMContext;
class Module;
class StructType;
class Type;
class Value;
} // namespace llvm

template <typename Integer = size_t> class Counter {
public:
  Counter(Integer start = Integer{}) : _curr(start) {}

  Integer operator()() { return _curr++; }

  void Set(Integer value) { _curr = value; }

private:
  Integer _curr;
};

class LLVMTypeConverter {
public:
  LLVMTypeConverter(llvm::LLVMContext &context, Counter<> &counter);

  llvm::Type *Convert(TypeRef type);
  llvm::FunctionType *ConvertFunction(TypeRef type);
  llvm::StructType *ConvertStruct(TypeRef type);

private:
  llvm::Type *ConvertParameter(TypeRef type);

  llvm::LLVMContext &_context;
  Counter<> &_counter;
  std::unordered_map<const TypeBase *, llvm::Type *> _types;
};

struct FunctionValue {
  TypeRef c_type = nullptr;
  llvm::Function *function = nullptr;
};

class GenContextLayer {
public:
  std::unordered_map<std::string_view, LValue> variables;
};

class LoopContextLayer {
public:
  llvm::BasicBlock *continue_block = nullptr;
  llvm::BasicBlock *break_block = nullptr;
};

class GenContext {
public:
  GenContext(llvm::LLVMContext &llvm_context, llvm::Module &module,
             TypeContext &types);

  llvm::LLVMContext &LLVMContext() { return _llvm_context; }
  llvm::Module &Module() { return _module; }

  TypeContext &Types() { return types; }
  const TypeContext &Types() const { return types; }

  llvm::Type *LLVMType(TypeRef type) { return type_converter.Convert(type); }
  llvm::FunctionType *LLVMFunctionType(TypeRef type) {
    return type_converter.ConvertFunction(type);
  }
  llvm::StructType *LLVMStructType(TypeRef type) {
    return type_converter.ConvertStruct(type);
  }

  std::vector<GenContextLayer> layers;
  std::vector<LoopContextLayer> loop_layers;

  std::unordered_map<std::string_view, FunctionValue> functions;
  llvm::Function *current_function = nullptr;
  TypeRef current_function_type = nullptr;

  GenContextLayer &CurrLayer() { return layers.back(); }
  LoopContextLayer &CurrLoopLayer() { return loop_layers.back(); }

  void PushLayer() { layers.emplace_back(); }
  void PushLoopLayer() { loop_layers.emplace_back(); }

  void PopLayer() {
    if (layers.size() > 1) {
      layers.pop_back();
    }
  }
  void PopLoopLayer() {
    if (loop_layers.size() > 1) {
      loop_layers.pop_back();
    }
  }

  LValue *FindVariable(std::string_view name) {
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      auto &layer = *it;
      auto var_it = layer.variables.find(name);
      if (var_it != layer.variables.end()) {
        return &var_it->second;
      }
    }
    return nullptr;
  }

  FunctionValue *FindFunction(std::string_view name) {
    if (auto it = functions.find(name); it != functions.end()) {
      return &it->second;
    }
    return nullptr;
  }

  void AddFunction(std::string_view name, FunctionValue value) {
    functions[name] = value;
  }

  llvm::Value *CreateStringConstant(std::string_view value);

  Counter<> counter;

private:
  llvm::LLVMContext &_llvm_context;
  llvm::Module &_module;
  TypeContext &types;
  LLVMTypeConverter type_converter;
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

class GenContextLoopLayerGuard {
public:
  explicit GenContextLoopLayerGuard(GenContext &context) : _context(context) {
    _context.PushLoopLayer();
  }
  ~GenContextLoopLayerGuard() { _context.PopLoopLayer(); }

private:
  GenContext &_context;
};
