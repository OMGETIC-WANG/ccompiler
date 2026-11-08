#pragma once

#include "../../AST/TypeContext.h"
#include "Value.h"

#include <llvm/IR/Constants.h>

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
  llvm::Type *ConvertBuiltin(BuiltinType *type);
  llvm::FunctionType *ConvertFunction(FunctionType *type);
  llvm::StructType *ConvertStruct(StructType *type);
  llvm::StructType *ConvertUnion(UnionType *type);
  llvm::ArrayType *ConvertArray(ArrayType *type);
  llvm::Type *ConvertEnum(EnumType *type);

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
  std::unordered_map<std::string_view, EnumType *> enums;
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
    return type_converter.ConvertFunction(type->As<FunctionType>());
  }
  llvm::StructType *LLVMStructType(TypeRef type) {
    return type_converter.ConvertStruct(type->As<StructType>());
  }
  llvm::StructType *LLVMUnionType(TypeRef type) {
    return type_converter.ConvertUnion(type->As<UnionType>());
  }
  llvm::ArrayType *LLVMArrayType(TypeRef type) {
    return type_converter.ConvertArray(type->As<ArrayType>());
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

  std::optional<RValue> FindEnumValue(std::string_view name) {
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      for (auto &[_, enum_type] : it->enums) {
        for (auto &value : enum_type->enumerators) {
          if (value.name == name) {
            auto llvm_val = std::visit(
                [this, enum_type](auto v) {
                  return llvm::ConstantInt::get(
                      LLVMType(enum_type), v,
                      !HasTypeTag(enum_type, TypeTag::Unsigned));
                },
                value.value);
            return RValue{enum_type, llvm_val};
          }
        }
      }
    }
    return std::nullopt;
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

  llvm::Value *CreateStringConstant(std::string_view value, size_t buffer_size);

  llvm::Constant *CreateLiteralConstant(std::string_view value,
                                        size_t buffer_size);

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
