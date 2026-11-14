#pragma once

#include "../../AST/TypeContext.h"
#include "../../Util/Diagnostic.h"
#include "Value.h"

#include <llvm/IR/Constants.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
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

class EnumElemDecl;
class ValueDecl;

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

class LoopContextLayer {
public:
  llvm::BasicBlock *continue_block = nullptr;
  llvm::BasicBlock *break_block = nullptr;
};

class GenContext {
public:
  using DeclValue = std::variant<LValue, EnumType *>;

  GenContext(llvm::LLVMContext &llvm_context, llvm::Module &module,
             TypeContext &types, DiagnosticContext &diagnostics);

  llvm::LLVMContext &LLVMContext() { return _llvm_context; }
  llvm::Module &Module() { return _module; }

  TypeContext &Types() { return types; }
  const TypeContext &Types() const { return types; }

  DiagnosticContext &Diagnostics() { return _diagnostics; }
  const DiagnosticContext &Diagnostics() const { return _diagnostics; }

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

  std::vector<LoopContextLayer> loop_layers;

  llvm::Function *current_function = nullptr;
  TypeRef current_function_type = nullptr;

  LoopContextLayer &CurrLoopLayer() { return loop_layers.back(); }

  void PushLoopLayer() { loop_layers.emplace_back(); }

  void PopLoopLayer() {
    if (loop_layers.size() > 1) {
      loop_layers.pop_back();
    }
  }

  void AddLValue(const ValueDecl *decl, LValue value);
  void AddEnum(const EnumElemDecl *decl, EnumType *type);
  const LValue *FindLValue(const ValueDecl *decl) const;
  std::optional<RValue> FindEnumValue(const EnumElemDecl *decl);
  std::optional<TypeRef> FindDeclType(const ValueDecl *decl);

  llvm::Value *CreateStringConstant(std::string_view value, size_t buffer_size);

  llvm::Constant *CreateLiteralConstant(std::string_view value,
                                        size_t buffer_size);

  Counter<> counter;

private:
  llvm::LLVMContext &_llvm_context;
  llvm::Module &_module;
  TypeContext &types;
  DiagnosticContext &_diagnostics;
  LLVMTypeConverter type_converter;
  std::unordered_map<const ValueDecl *, DeclValue> values;
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
