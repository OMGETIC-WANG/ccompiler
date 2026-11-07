#pragma once

#include "../../AST/Operations.h"
#include "../../AST/Type.h"
#include "GenContext.h"
#include "Value.h"

#include <llvm/IR/Attributes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>

#include <optional>
#include <span>
#include <string_view>
#include <vector>

std::optional<llvm::Attribute::AttrKind> GetIntegerABIAttribute(TypeRef type);

class IRBuilder {
public:
  explicit IRBuilder(GenContext &context)
      : _context(context), _builder(context.LLVMContext()) {}

  llvm::IRBuilder<> &LLVM() { return _builder; }
  GenContext &Context() { return _context; }

  void SetInsertPoint(llvm::BasicBlock *block) {
    _builder.SetInsertPoint(block);
  }

  llvm::BasicBlock *GetInsertBlock() const { return _builder.GetInsertBlock(); }

  bool IsTerminated() const;

  void CreateBr(llvm::BasicBlock *target);

  void CreateCondBr(const RValue &cond, llvm::BasicBlock *true_block,
                    llvm::BasicBlock *false_block);

  RValue CreateLoad(TypeRef type, const LValue &ptr);

  LValue CreateStore(const RValue &value, const LValue &ptr);

  LValue CreateAlloca(TypeRef type);

  LValue GetElementPtrImpl(llvm::StructType *type, llvm::Value *address,
                           const StructField &field);

  LValue GetElementPtr(const LValue &value, const StructField &field);

  LValue GetElementPtr(const LValue &value, std::string_view field_name);

  LValue GetPtrElementPtr(const RValue &ptr, std::string_view field_name);

  RValue CreateExtractValue(const RValue &value, const StructField &field);

  RValue CreateExtractValue(const RValue &value, std::string_view field_name);

  LValue CreateSubscript(const RValue &value, const RValue &index);

  void CreateRet(const RValue &value);

  void CreateVoidRet();

  RValue CreateBoolCond(const RValue &value);

  RValue CreateInvBoolCond(const RValue &value);

  RValue CreateNumericCast(const RValue &value, TypeRef target_type);

  RValue CreatePhi(const RValue &value1, llvm::BasicBlock *block1,
                   const RValue &value2, llvm::BasicBlock *block2);

  RValue CreateBinaryOp(BinaryOpType op, const RValue &left,
                        const RValue &right, TypeRef out_type);

  RValue CreateFuncInvoke(const RValue &func, std::span<const RValue> args);

  RValue CreateInv(const RValue &value);

  RValue CreateBitNot(const RValue &value);

  void CreateVaStart(const LValue &va_list);

  RValue CreateVaArg(const LValue &va_list, TypeRef type);

  void CreateVaEnd(const LValue &va_list);

  void CreateMemcpy(const LValue &dst, const RValue &src, size_t size,
                    size_t align);

  void CreateSwitch(
      const RValue &value,
      std::span<std::pair<llvm::ConstantInt *, llvm::BasicBlock *>> cases,
      llvm::BasicBlock *default_block, unsigned int num_cases);

private:
  GenContext &_context;
  llvm::IRBuilder<> _builder;
};
