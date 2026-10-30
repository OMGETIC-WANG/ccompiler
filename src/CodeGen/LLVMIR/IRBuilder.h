#pragma once

#include "../../AST/Operations.h"
#include "../../AST/Type.h"
#include "GenContext.h"
#include "Value.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

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

  bool IsTerminated() const {
    auto *block = GetInsertBlock();
    return block && block->hasTerminator();
  }

  void CreateBr(llvm::BasicBlock *target) {
    if (!IsTerminated()) {
      _builder.CreateBr(target);
    }
  }

  void CreateCondBr(const RValue &cond, llvm::BasicBlock *true_block,
                    llvm::BasicBlock *false_block) {
    RValue bool_cond = cond;
    if (!cond.c_type->Is<BuiltinType>() ||
        cond.c_type->As<BuiltinType>()->Kind() != BuiltinKind::I1) {
      bool_cond = CreateBoolCond(cond);
    }
    _builder.CreateCondBr(bool_cond.ir_value, true_block, false_block);
  }

  RValue CreateLoad(TypeRef type, const LValue &ptr) {
    auto *load = _builder.CreateLoad(_context.LLVMType(type), ptr.address);
    load->setAlignment(llvm::Align(type->GetAlign()));
    return RValue{type, load};
  }

  LValue CreateStore(const RValue &value, const LValue &ptr) {
    auto *store = _builder.CreateStore(value.ir_value, ptr.address);
    store->setAlignment(llvm::Align(ptr.c_type->GetAlign()));
    return ptr;
  }

  LValue CreateAlloca(TypeRef type) {
    auto *alloca = _builder.CreateAlloca(_context.LLVMType(type));
    alloca->setAlignment(llvm::Align(type->GetAlign()));
    return LValue{type, alloca};
  }

  LValue GetElementPtr(const LValue &value, const StructField &field) {
    ASSERT(value.c_type->Is<StructType>(),
           "GetElementPtr only supports struct type");
    size_t field_index = 0;
    bool found = false;
    for (size_t i = 0; i < value.c_type->As<StructType>()->fields.size(); ++i) {
      if (value.c_type->As<StructType>()->fields[i].name == field.name) {
        field_index = i;
        found = true;
        break;
      }
    }
    ASSERT(found, "Unknown struct field: ", field.name);
    auto *struct_type = _context.LLVMStructType(value.c_type);
    auto *ptr =
        _builder.CreateStructGEP(struct_type, value.address, field_index);
    return LValue{field.type, ptr};
  }

  LValue GetElementPtr(const LValue &value, std::string_view field_name) {
    ASSERT(value.c_type->Is<StructType>(),
           "GetElementPtr only supports struct type");
    auto field = value.c_type->As<StructType>()->GetField(field_name);
    ASSERT(field.has_value(), "Unknown struct field: ", field_name);
    return GetElementPtr(value, field.value());
  }

  LValue CreateSubscript(const RValue &value, const RValue &index) {
    if (auto *array = value.c_type->DynCast<ArrayType>()) {
      auto *source_type = _context.LLVMType(value.c_type);
      auto *zero = _builder.getInt64(0);
      auto *ptr = _builder.CreateInBoundsGEP(source_type, value.ir_value,
                                             {zero, index.ir_value});
      return LValue{array->element_type, ptr};
    }
    if (auto *pointer = value.c_type->DynCast<PointerType>()) {
      auto *source_type = _context.LLVMType(pointer->pointee_type);
      auto *ptr = _builder.CreateInBoundsGEP(source_type, value.ir_value,
                                             {index.ir_value});
      return LValue{pointer->pointee_type, ptr};
    }
    RAISE_ERROR("Subscript only supports array or pointer type");
  }

  void CreateRet(const RValue &value) {
    if (!IsTerminated()) {
      _builder.CreateRet(value.ir_value);
    }
  }

  void CreateVoidRet() {
    if (!IsTerminated()) {
      _builder.CreateRetVoid();
    }
  }

  RValue CreateBoolCond(const RValue &value) {
    TypeRef bool_type =
        _context.Types().Builtin(BuiltinKind::I1, TypeTag::Unsigned);
    llvm::Value *cond = nullptr;
    if (value.c_type->Is<PointerType>()) {
      cond = _builder.CreateICmpNE(
          value.ir_value,
          llvm::ConstantPointerNull::get(
              llvm::cast<llvm::PointerType>(_context.LLVMType(value.c_type))));
    } else if (IsFloatingType(value.c_type)) {
      cond = _builder.CreateFCmpUNE(
          value.ir_value,
          llvm::ConstantFP::get(_context.LLVMType(value.c_type), 0.0));
    } else {
      cond = _builder.CreateICmpNE(
          value.ir_value,
          llvm::Constant::getNullValue(_context.LLVMType(value.c_type)));
    }
    return RValue{bool_type, cond};
  }

  RValue CreateInvBoolCond(const RValue &value) {
    TypeRef bool_type =
        _context.Types().Builtin(BuiltinKind::I1, TypeTag::Unsigned);
    llvm::Value *cond = nullptr;
    if (value.c_type->Is<PointerType>()) {
      cond = _builder.CreateICmpEQ(
          value.ir_value,
          llvm::ConstantPointerNull::get(
              llvm::cast<llvm::PointerType>(_context.LLVMType(value.c_type))));
    } else if (IsFloatingType(value.c_type)) {
      cond = _builder.CreateFCmpUEQ(
          value.ir_value,
          llvm::ConstantFP::get(_context.LLVMType(value.c_type), 0.0));
    } else {
      cond = _builder.CreateICmpEQ(
          value.ir_value,
          llvm::Constant::getNullValue(_context.LLVMType(value.c_type)));
    }
    return RValue{bool_type, cond};
  }

  RValue CreateNumericCast(const RValue &value, TypeRef target_type) {
    llvm::Type *from_type = _context.LLVMType(value.c_type);
    llvm::Type *to_type = _context.LLVMType(target_type);
    if (from_type == to_type) {
      return RValue{target_type, value.ir_value};
    }

    auto *from_c = value.c_type->DynCast<BuiltinType>();
    auto *to_c = target_type->DynCast<BuiltinType>();
    ASSERT(from_c && to_c, "Numeric cast only supports builtin type");

    llvm::Value *result = nullptr;
    if (IsIntegerType(value.c_type) && IsIntegerType(target_type)) {
      auto *from_int = llvm::cast<llvm::IntegerType>(from_type);
      auto *to_int = llvm::cast<llvm::IntegerType>(to_type);
      if (from_int->getBitWidth() > to_int->getBitWidth()) {
        result = _builder.CreateTrunc(value.ir_value, to_type);
      } else if (HasTypeTag(value.c_type, TypeTag::Unsigned)) {
        result = _builder.CreateZExt(value.ir_value, to_type);
      } else {
        result = _builder.CreateSExt(value.ir_value, to_type);
      }
    } else if (IsFloatingType(value.c_type) && IsIntegerType(target_type)) {
      result = HasTypeTag(target_type, TypeTag::Unsigned)
                   ? _builder.CreateFPToUI(value.ir_value, to_type)
                   : _builder.CreateFPToSI(value.ir_value, to_type);
    } else if (IsIntegerType(value.c_type) && IsFloatingType(target_type)) {
      result = HasTypeTag(value.c_type, TypeTag::Unsigned)
                   ? _builder.CreateUIToFP(value.ir_value, to_type)
                   : _builder.CreateSIToFP(value.ir_value, to_type);
    } else {
      if (from_type->getPrimitiveSizeInBits() >
          to_type->getPrimitiveSizeInBits()) {
        result = _builder.CreateFPTrunc(value.ir_value, to_type);
      } else {
        result = _builder.CreateFPExt(value.ir_value, to_type);
      }
    }
    return RValue{target_type, result};
  }

  RValue CreatePhi(const RValue &value1, llvm::BasicBlock *block1,
                   const RValue &value2, llvm::BasicBlock *block2) {
    ASSERT(value1.c_type == value2.c_type,
           "Phi values must have the same type");
    auto *phi = _builder.CreatePHI(_context.LLVMType(value1.c_type), 2);
    phi->addIncoming(value1.ir_value, block1);
    phi->addIncoming(value2.ir_value, block2);
    return RValue{value1.c_type, phi};
  }

  RValue CreateBinaryOp(BinaryOpType op, const RValue &left,
                        const RValue &right, TypeRef out_type) {
    ASSERT(left.ir_value->getType()->isIntegerTy() ||
               left.ir_value->getType()->isFloatingPointTy(),
           "Binary operation only supports numeric values");
    ASSERT(left.ir_value->getType() == right.ir_value->getType(),
           "Binary operation operand type mismatch");
    bool is_float = left.ir_value->getType()->isFloatingPointTy();
    bool is_unsigned = HasTypeTag(left.c_type, TypeTag::Unsigned);

    switch (op) {
    case BinaryOpType::Add:
      return RValue{out_type,
                    is_float
                        ? _builder.CreateFAdd(left.ir_value, right.ir_value)
                        : _builder.CreateAdd(left.ir_value, right.ir_value)};
    case BinaryOpType::Sub:
      return RValue{out_type,
                    is_float
                        ? _builder.CreateFSub(left.ir_value, right.ir_value)
                        : _builder.CreateSub(left.ir_value, right.ir_value)};
    case BinaryOpType::Mul:
      return RValue{out_type,
                    is_float
                        ? _builder.CreateFMul(left.ir_value, right.ir_value)
                        : _builder.CreateMul(left.ir_value, right.ir_value)};
    case BinaryOpType::Div:
      return RValue{
          out_type,
          is_float
              ? _builder.CreateFDiv(left.ir_value, right.ir_value)
              : (is_unsigned
                     ? _builder.CreateUDiv(left.ir_value, right.ir_value)
                     : _builder.CreateSDiv(left.ir_value, right.ir_value))};
    case BinaryOpType::Mod:
      return RValue{
          out_type,
          is_float
              ? _builder.CreateFRem(left.ir_value, right.ir_value)
              : (is_unsigned
                     ? _builder.CreateURem(left.ir_value, right.ir_value)
                     : _builder.CreateSRem(left.ir_value, right.ir_value))};
    case BinaryOpType::BitAnd:
      ASSERT(!is_float, "Bitwise AND requires integer operands");
      return RValue{out_type,
                    _builder.CreateAnd(left.ir_value, right.ir_value)};
    case BinaryOpType::BitOr:
      ASSERT(!is_float, "Bitwise OR requires integer operands");
      return RValue{out_type, _builder.CreateOr(left.ir_value, right.ir_value)};
    case BinaryOpType::BitXor:
      ASSERT(!is_float, "Bitwise XOR requires integer operands");
      return RValue{out_type,
                    _builder.CreateXor(left.ir_value, right.ir_value)};
    case BinaryOpType::LShift:
      ASSERT(!is_float, "Left shift requires integer operands");
      return RValue{out_type,
                    _builder.CreateShl(left.ir_value, right.ir_value)};
    case BinaryOpType::RShift:
      ASSERT(!is_float, "Right shift requires integer operands");
      return RValue{out_type,
                    is_unsigned
                        ? _builder.CreateLShr(left.ir_value, right.ir_value)
                        : _builder.CreateAShr(left.ir_value, right.ir_value)};
    case BinaryOpType::Eq:
      return RValue{out_type,
                    is_float
                        ? _builder.CreateFCmpOEQ(left.ir_value, right.ir_value)
                        : _builder.CreateICmpEQ(left.ir_value, right.ir_value)};
    case BinaryOpType::Ne:
      return RValue{out_type,
                    is_float
                        ? _builder.CreateFCmpUNE(left.ir_value, right.ir_value)
                        : _builder.CreateICmpNE(left.ir_value, right.ir_value)};
    case BinaryOpType::Lt:
      return RValue{
          out_type,
          is_float
              ? _builder.CreateFCmpOLT(left.ir_value, right.ir_value)
              : (is_unsigned
                     ? _builder.CreateICmpULT(left.ir_value, right.ir_value)
                     : _builder.CreateICmpSLT(left.ir_value, right.ir_value))};
    case BinaryOpType::Gt:
      return RValue{
          out_type,
          is_float
              ? _builder.CreateFCmpOGT(left.ir_value, right.ir_value)
              : (is_unsigned
                     ? _builder.CreateICmpUGT(left.ir_value, right.ir_value)
                     : _builder.CreateICmpSGT(left.ir_value, right.ir_value))};
    case BinaryOpType::Le:
      return RValue{
          out_type,
          is_float
              ? _builder.CreateFCmpOLE(left.ir_value, right.ir_value)
              : (is_unsigned
                     ? _builder.CreateICmpULE(left.ir_value, right.ir_value)
                     : _builder.CreateICmpSLE(left.ir_value, right.ir_value))};
    case BinaryOpType::Ge:
      return RValue{
          out_type,
          is_float
              ? _builder.CreateFCmpOGE(left.ir_value, right.ir_value)
              : (is_unsigned
                     ? _builder.CreateICmpUGE(left.ir_value, right.ir_value)
                     : _builder.CreateICmpSGE(left.ir_value, right.ir_value))};
    default:
      RAISE_ERROR("Unsupported binary operation");
    }
  }

  RValue CreateFuncInvoke(const RValue &func, std::span<const RValue> args) {
    TypeRef c_function_type = func.c_type;
    if (auto *pointer = c_function_type->DynCast<PointerType>()) {
      c_function_type = pointer->pointee_type;
    }
    ASSERT(c_function_type->Is<FunctionType>(),
           "Function invoke only supports function or pointer type");
    const auto *function = c_function_type->As<FunctionType>();
    ASSERT(function->parameters.size() == args.size(),
           "Function argument count mismatch");

    std::vector<llvm::Value *> llvm_args;
    llvm_args.reserve(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
      RValue arg = args[i];
      TypeRef param_type = function->parameters[i];
      if (auto *array = param_type->DynCast<ArrayType>()) {
        param_type = _context.Types().Pointer(array->element_type);
      }
      if (arg.c_type != param_type && IsNumericType(arg.c_type) &&
          IsNumericType(param_type)) {
        arg = CreateNumericCast(arg, param_type);
      }
      ASSERT(arg.ir_value->getType() ==
                 _context.LLVMFunctionType(c_function_type)->getParamType(i),
             "Function argument type mismatch at argument ", std::to_string(i));
      llvm_args.push_back(arg.ir_value);
    }

    auto *call = _builder.CreateCall(_context.LLVMFunctionType(c_function_type),
                                     func.ir_value, llvm_args);
    return RValue{function->return_type, call};
  }

  RValue CreateInv(const RValue &value) {
    if (value.ir_value->getType()->isIntegerTy()) {
      return RValue{value.c_type, _builder.CreateNeg(value.ir_value)};
    }
    return RValue{value.c_type, _builder.CreateFNeg(value.ir_value)};
  }

  RValue CreateBitNot(const RValue &value) {
    ASSERT(value.ir_value->getType()->isIntegerTy(),
           "Bitwise NOT requires an integer operand");
    return RValue{value.c_type, _builder.CreateNot(value.ir_value)};
  }

private:
  GenContext &_context;
  llvm::IRBuilder<> _builder;
};
