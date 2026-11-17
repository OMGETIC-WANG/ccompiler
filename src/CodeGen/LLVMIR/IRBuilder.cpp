#include "IRBuilder.h"
#include "../../Util/Diagnostic.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>

#include <string>
#include <vector>

std::optional<llvm::Attribute::AttrKind> GetIntegerABIAttribute(TypeRef type) {
  if (!IsIntegerType(type) || type->GetSize() * 8 >= 32) {
    return std::nullopt;
  }
  return HasTypeTag(type, TypeTag::Unsigned) ? llvm::Attribute::ZExt
                                             : llvm::Attribute::SExt;
}

bool IRBuilder::IsTerminated() const {
  auto *block = GetInsertBlock();
  return block && block->hasTerminator();
}

void IRBuilder::CreateBr(llvm::BasicBlock *target) {
  if (!IsTerminated()) {
    _builder.CreateBr(target);
  }
}

void IRBuilder::CreateCondBr(const RValue &cond, llvm::BasicBlock *true_block,
                             llvm::BasicBlock *false_block) {
  ASSERT(cond.c_type->Is<BuiltinType>() &&
             cond.c_type->As<BuiltinType>()->Kind() == BuiltinKind::I1,
         "Sema left a non-boolean branch condition type");
  ASSERT(cond.ir_value->getType()->isIntegerTy(1),
         "Sema left a non-boolean branch condition");
  _builder.CreateCondBr(cond.ir_value, true_block, false_block);
}

RValue IRBuilder::CreateLoad(TypeRef type, const LValue &ptr) {
  auto *load = _builder.CreateLoad(_context.LLVMType(type), ptr.address,
                                   HasTypeTag(type, TypeTag::Volatile));
  load->setAlignment(llvm::Align(type->GetAlign()));
  return RValue{StripTypeQualifiers(type), load};
}

LValue IRBuilder::CreateStore(const RValue &value, const LValue &ptr) {
  auto *store = _builder.CreateStore(value.ir_value, ptr.address,
                                     HasTypeTag(ptr.c_type, TypeTag::Volatile));
  store->setAlignment(llvm::Align(ptr.c_type->GetAlign()));
  return ptr;
}

LValue IRBuilder::CreateAlloca(TypeRef type) {
  auto *alloca = _builder.CreateAlloca(_context.LLVMType(type));
  alloca->setAlignment(llvm::Align(type->GetAlign()));
  return LValue{type, alloca};
}

LValue IRBuilder::GetElementPtrImpl(llvm::StructType *type,
                                    TypeRef aggregate_type,
                                    llvm::Value *address,
                                    const StructField &field) {
  auto *ptr = _builder.CreateStructGEP(type, address, field.index);
  return LValue{ApplyAggregateMemberQualifiers(field.type, aggregate_type,
                                               _context.Types()),
                ptr};
}

LValue IRBuilder::GetElementPtr(const LValue &value, const StructField &field,
                                SourceLocation location) {
  (void)location;
  ASSERT(value.c_type->Is<StructType>(),
         "Sema accepted field access on a non-struct");
  auto *struct_type = _context.LLVMStructType(value.c_type);
  return GetElementPtrImpl(struct_type, value.c_type, value.address, field);
}

LValue IRBuilder::GetElementPtr(const LValue &value,
                                std::string_view field_name,
                                SourceLocation location) {
  ASSERT(value.c_type->Is<StructType>(),
         "Sema accepted field access on a non-struct");
  auto field = value.c_type->As<StructType>()->GetField(field_name);
  ASSERT(field.has_value(), "Sema accepted an unknown struct field");
  (void)location;
  return GetElementPtr(value, field.value());
}

LValue IRBuilder::GetPtrElementPtr(const RValue &ptr,
                                   std::string_view field_name,
                                   SourceLocation location) {
  ASSERT(ptr.c_type->Is<PointerType>(),
         "Sema accepted pointer field access on a non-pointer");
  auto *pointee_type =
      ptr.c_type->As<PointerType>()->pointee_type->DynCast<StructType>();
  ASSERT(pointee_type,
         "Sema accepted pointer field access on a non-struct pointer");
  auto field = pointee_type->GetField(field_name);
  ASSERT(field.has_value(), "Sema accepted an unknown struct field");
  (void)location;
  auto *struct_type = _context.LLVMStructType(pointee_type);
  return GetElementPtrImpl(struct_type,
                           ptr.c_type->As<PointerType>()->pointee_type,
                           ptr.ir_value, field.value());
}

RValue IRBuilder::CreateExtractValue(const RValue &value,
                                     const StructField &field,
                                     SourceLocation location) {
  ASSERT(value.c_type->Is<StructType>(),
         "Sema accepted field extraction from a non-struct");
  (void)location;
  auto *val = _builder.CreateExtractValue(value.ir_value, field.index);
  return RValue{field.type, val};
}

RValue IRBuilder::CreateExtractValue(const RValue &value,
                                     std::string_view field_name,
                                     SourceLocation location) {
  ASSERT(value.c_type->Is<StructType>(),
         "Sema accepted field extraction from a non-struct");
  auto field = value.c_type->As<StructType>()->GetField(field_name);
  ASSERT(field.has_value(), "Sema accepted an unknown struct field");
  (void)location;
  return CreateExtractValue(value, field.value(), location);
}

LValue IRBuilder::CreateSubscript(const RValue &value, const RValue &index,
                                  SourceLocation location) {
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
  (void)location;
  RAISE_ERROR("Sema accepted subscript on a non-array, non-pointer operand");
}

void IRBuilder::CreateRet(const RValue &value) {
  if (!IsTerminated()) {
    _builder.CreateRet(value.ir_value);
  }
}

void IRBuilder::CreateVoidRet() {
  if (!IsTerminated()) {
    _builder.CreateRetVoid();
  }
}

RValue IRBuilder::CreateBoolCond(const RValue &value) {
  TypeRef bool_type = ApplyTypeTag(_context.Types().getI1(), TypeTag::Unsigned);
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

RValue IRBuilder::CreateNumericCast(const RValue &value, TypeRef target_type) {
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
    ASSERT(IsFloatingType(value.c_type) && IsFloatingType(target_type),
           "Numeric cast has unsupported source and target types");
    if (FloatingConversionRank(from_c->Kind()) >
        FloatingConversionRank(to_c->Kind())) {
      result = _builder.CreateFPTrunc(value.ir_value, to_type);
    } else {
      result = _builder.CreateFPExt(value.ir_value, to_type);
    }
  }
  return RValue{target_type, result};
}

RValue IRBuilder::CreatePhi(const RValue &value1, llvm::BasicBlock *block1,
                            const RValue &value2, llvm::BasicBlock *block2) {
  ASSERT(value1.c_type == value2.c_type, "Phi values must have the same type");
  auto *phi = _builder.CreatePHI(_context.LLVMType(value1.c_type), 2);
  phi->addIncoming(value1.ir_value, block1);
  phi->addIncoming(value2.ir_value, block2);
  return RValue{value1.c_type, phi};
}

RValue IRBuilder::CreateBinaryOp(BinaryOpType op, const RValue &left,
                                 const RValue &right, SourceLocation location) {
  (void)location;
  ASSERT(left.ir_value->getType()->isIntegerTy() ||
             left.ir_value->getType()->isFloatingPointTy(),
         "Sema accepted a non-numeric binary operation");
  ASSERT(left.ir_value->getType() == right.ir_value->getType(),
         "Sema left binary operands with different IR types");
  bool is_float = left.ir_value->getType()->isFloatingPointTy();
  bool is_unsigned = HasTypeTag(left.c_type, TypeTag::Unsigned);
  TypeRef bool_type = ApplyTypeTag(_context.Types().getI1(), TypeTag::Unsigned);
  auto comparison_result = [&](llvm::Value *value) {
    return RValue{bool_type, value};
  };

  switch (op) {
  case BinaryOpType::Add:
    return RValue{left.c_type,
                  is_float ? _builder.CreateFAdd(left.ir_value, right.ir_value)
                           : _builder.CreateAdd(left.ir_value, right.ir_value)};
  case BinaryOpType::Sub:
    return RValue{left.c_type,
                  is_float ? _builder.CreateFSub(left.ir_value, right.ir_value)
                           : _builder.CreateSub(left.ir_value, right.ir_value)};
  case BinaryOpType::Mul:
    return RValue{left.c_type,
                  is_float ? _builder.CreateFMul(left.ir_value, right.ir_value)
                           : _builder.CreateMul(left.ir_value, right.ir_value)};
  case BinaryOpType::Div:
    return RValue{
        left.c_type,
        is_float ? _builder.CreateFDiv(left.ir_value, right.ir_value)
                 : (is_unsigned
                        ? _builder.CreateUDiv(left.ir_value, right.ir_value)
                        : _builder.CreateSDiv(left.ir_value, right.ir_value))};
  case BinaryOpType::Mod:
    return RValue{
        left.c_type,
        is_float ? _builder.CreateFRem(left.ir_value, right.ir_value)
                 : (is_unsigned
                        ? _builder.CreateURem(left.ir_value, right.ir_value)
                        : _builder.CreateSRem(left.ir_value, right.ir_value))};
  case BinaryOpType::BitAnd:
    ASSERT(!is_float, "Sema accepted floating point bitwise AND");
    return RValue{left.c_type,
                  _builder.CreateAnd(left.ir_value, right.ir_value)};
  case BinaryOpType::BitOr:
    ASSERT(!is_float, "Sema accepted floating point bitwise OR");
    return RValue{left.c_type,
                  _builder.CreateOr(left.ir_value, right.ir_value)};
  case BinaryOpType::BitXor:
    ASSERT(!is_float, "Sema accepted floating point bitwise XOR");
    return RValue{left.c_type,
                  _builder.CreateXor(left.ir_value, right.ir_value)};
  case BinaryOpType::LShift:
    ASSERT(!is_float, "Sema accepted floating point left shift");
    return RValue{left.c_type,
                  _builder.CreateShl(left.ir_value, right.ir_value)};
  case BinaryOpType::RShift:
    ASSERT(!is_float, "Sema accepted floating point right shift");
    return RValue{left.c_type,
                  is_unsigned
                      ? _builder.CreateLShr(left.ir_value, right.ir_value)
                      : _builder.CreateAShr(left.ir_value, right.ir_value)};
  case BinaryOpType::Eq:
    return comparison_result(
        is_float ? _builder.CreateFCmpOEQ(left.ir_value, right.ir_value)
                 : _builder.CreateICmpEQ(left.ir_value, right.ir_value));
  case BinaryOpType::Ne:
    return comparison_result(
        is_float ? _builder.CreateFCmpUNE(left.ir_value, right.ir_value)
                 : _builder.CreateICmpNE(left.ir_value, right.ir_value));
  case BinaryOpType::Lt:
    return comparison_result(
        is_float
            ? _builder.CreateFCmpOLT(left.ir_value, right.ir_value)
            : (is_unsigned
                   ? _builder.CreateICmpULT(left.ir_value, right.ir_value)
                   : _builder.CreateICmpSLT(left.ir_value, right.ir_value)));
  case BinaryOpType::Gt:
    return comparison_result(
        is_float
            ? _builder.CreateFCmpOGT(left.ir_value, right.ir_value)
            : (is_unsigned
                   ? _builder.CreateICmpUGT(left.ir_value, right.ir_value)
                   : _builder.CreateICmpSGT(left.ir_value, right.ir_value)));
  case BinaryOpType::Le:
    return comparison_result(
        is_float
            ? _builder.CreateFCmpOLE(left.ir_value, right.ir_value)
            : (is_unsigned
                   ? _builder.CreateICmpULE(left.ir_value, right.ir_value)
                   : _builder.CreateICmpSLE(left.ir_value, right.ir_value)));
  case BinaryOpType::Ge:
    return comparison_result(
        is_float
            ? _builder.CreateFCmpOGE(left.ir_value, right.ir_value)
            : (is_unsigned
                   ? _builder.CreateICmpUGE(left.ir_value, right.ir_value)
                   : _builder.CreateICmpSGE(left.ir_value, right.ir_value)));
  default:
    RAISE_ERROR("Sema accepted an unsupported binary operation");
  }
}

RValue IRBuilder::CreateFuncInvoke(const RValue &func,
                                   std::span<const RValue> args,
                                   SourceLocation location) {
  (void)location;
  TypeRef c_function_type = func.c_type;
  if (auto *pointer = c_function_type->DynCast<PointerType>()) {
    c_function_type = pointer->pointee_type;
  }
  ASSERT(c_function_type->Is<FunctionType>(),
         "Sema accepted invocation of a non-function");
  const auto *function = c_function_type->As<FunctionType>();

  ASSERT(function->has_va_args ? args.size() >= function->parameters.size()
                               : args.size() == function->parameters.size(),
         "Sema left a function invocation with the wrong argument count");

  std::vector<llvm::Value *> llvm_args;
  llvm_args.reserve(args.size());
  for (size_t i = 0; i < args.size(); ++i) {
    const RValue &arg = args[i];
    if (i < function->parameters.size()) {
      TypeRef param_type = function->parameters[i];
      if (auto *array = param_type->DynCast<ArrayType>()) {
        param_type = _context.Types().Pointer(array->element_type);
      }
      ASSERT(arg.ir_value->getType() ==
                 _context.LLVMFunctionType(c_function_type)->getParamType(i),
             "Sema left a function argument with the wrong IR type");
      (void)param_type;
    }
    llvm_args.push_back(arg.ir_value);
  }

  auto *call = _builder.CreateCall(_context.LLVMFunctionType(c_function_type),
                                   func.ir_value, llvm_args);
  if (auto attr = GetIntegerABIAttribute(function->return_type)) {
    call->addRetAttr(*attr);
  }
  for (size_t i = 0; i < function->parameters.size(); ++i) {
    TypeRef parameter_type = function->parameters[i];
    if (auto *array = parameter_type->DynCast<ArrayType>()) {
      parameter_type = _context.Types().Pointer(array->element_type);
    }
    if (auto attr = GetIntegerABIAttribute(parameter_type)) {
      call->addParamAttr(static_cast<unsigned>(i), *attr);
    }
  }
  return RValue{function->return_type, call};
}

RValue IRBuilder::CreateInv(const RValue &value) {
  if (value.ir_value->getType()->isIntegerTy()) {
    return RValue{value.c_type, _builder.CreateNeg(value.ir_value)};
  }
  return RValue{value.c_type, _builder.CreateFNeg(value.ir_value)};
}

RValue IRBuilder::CreateBitNot(const RValue &value) {
  ASSERT(value.ir_value->getType()->isIntegerTy(),
         "Bitwise NOT requires an integer operand");
  return RValue{value.c_type, _builder.CreateNot(value.ir_value)};
}

void IRBuilder::CreateVaStart(const LValue &va_list) {
  _builder.CreateIntrinsic(llvm::Intrinsic::vastart, {_builder.getPtrTy()},
                           {va_list.address});
}

void IRBuilder::CreateVaEnd(const LValue &va_list) {
  _builder.CreateIntrinsic(llvm::Intrinsic::vaend, {_builder.getPtrTy()},
                           {va_list.address});
}

RValue IRBuilder::CreateVaArg(const LValue &va_list, TypeRef type) {
  if (auto *builtin = type->DynCast<BuiltinType>();
      builtin && builtin->Kind() == BuiltinKind::LongDouble) {
    // x86_64 passes long double in the overflow area, aligned to 16 bytes.
    auto *va_list_array = va_list.c_type->As<ArrayType>();
    ASSERT(va_list_array->element_type->Is<StructType>(),
           "va_list must be an array containing the ABI va_list structure");
    auto *tag_type = va_list_array->element_type->As<StructType>();

    auto *tag_pointer = _builder.CreateInBoundsGEP(
        _context.LLVMArrayType(va_list.c_type), va_list.address,
        {_builder.getInt64(0), _builder.getInt64(0)});
    auto *overflow_address = _builder.CreateStructGEP(
        _context.LLVMStructType(tag_type), tag_pointer, 2);
    auto *overflow_area =
        _builder.CreateLoad(_builder.getPtrTy(), overflow_address);
    llvm::Align field_alignment(tag_type->fields[2].type->GetAlign());
    overflow_area->setAlignment(field_alignment);

    auto *bias =
        _builder.CreateInBoundsGEP(_builder.getInt8Ty(), overflow_area,
                                   _builder.getInt64(type->GetAlign() - 1));
    auto *aligned_area = _builder.CreateIntrinsic(
        llvm::Intrinsic::ptrmask, {_builder.getPtrTy(), _builder.getInt64Ty()},
        {bias, _builder.getInt64(-static_cast<int64_t>(type->GetAlign()))});
    auto *next_overflow_area = _builder.CreateInBoundsGEP(
        _builder.getInt8Ty(), aligned_area, _builder.getInt64(type->GetSize()));
    auto *store = _builder.CreateStore(next_overflow_area, overflow_address);
    store->setAlignment(field_alignment);

    auto *load = _builder.CreateLoad(_context.LLVMType(type), aligned_area);
    load->setAlignment(llvm::Align(type->GetAlign()));
    return RValue{type, load};
  }
  return RValue{type,
                _builder.CreateVAArg(va_list.address, _context.LLVMType(type))};
}

void IRBuilder::CreateMemcpy(const LValue &dst, const RValue &src, size_t size,
                             size_t align) {
  bool is_volatile = HasTypeTag(dst.c_type, TypeTag::Volatile);
  TypeRef element_type = dst.c_type;
  while (auto *array = element_type->DynCast<ArrayType>()) {
    element_type = array->element_type;
    is_volatile |= HasTypeTag(element_type, TypeTag::Volatile);
  }
  _builder.CreateMemCpy(dst.address, llvm::Align(align), src.ir_value,
                        llvm::Align(align), size, is_volatile);
}

void IRBuilder::CreateSwitch(
    const RValue &value,
    std::span<std::pair<llvm::ConstantInt *, llvm::BasicBlock *>> cases,
    llvm::BasicBlock *default_block, unsigned int num_cases) {
  auto inst = _builder.CreateSwitch(value.ir_value, default_block, num_cases);
  for (const auto &[val, block] : cases) {
    inst->addCase(val, block);
  }
}
