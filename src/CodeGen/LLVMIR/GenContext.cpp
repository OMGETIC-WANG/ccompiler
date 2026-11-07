#include "GenContext.h"

#include "../../Exception.h"
#include "Value.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>

#include <format>

LLVMTypeConverter::LLVMTypeConverter(llvm::LLVMContext &context,
                                     Counter<> &counter)
    : _context(context), _counter(counter) {}

llvm::Type *LLVMTypeConverter::ConvertParameter(TypeRef type) {
  if (type->Is<ArrayType>() || type->Is<FunctionType>()) {
    return llvm::PointerType::get(_context, 0);
  }
  return Convert(type);
}

llvm::Type *LLVMTypeConverter::Convert(TypeRef type) {
  ASSERT(type != nullptr, "Cannot convert a null TypeRef to LLVM");

  const TypeBase *key = type.get();
  if (auto found = _types.find(key); found != _types.end()) {
    return found->second;
  }

  llvm::Type *result = nullptr;
  switch (type->GetType()) {
  case TypeBaseType::BuiltinType:
    switch (type->As<BuiltinType>()->Kind()) {
    case BuiltinKind::Void:
      result = llvm::Type::getVoidTy(_context);
      break;
    case BuiltinKind::Int:
      result = llvm::Type::getInt32Ty(_context);
      break;
    case BuiltinKind::Char:
      result = llvm::Type::getInt8Ty(_context);
      break;
    case BuiltinKind::Short:
      result = llvm::Type::getInt16Ty(_context);
      break;
    case BuiltinKind::Float:
      result = llvm::Type::getFloatTy(_context);
      break;
    case BuiltinKind::Double:
      result = llvm::Type::getDoubleTy(_context);
      break;
    case BuiltinKind::LongDouble:
      result = llvm::Type::getX86_FP80Ty(_context);
      break;
    case BuiltinKind::Long:
      result = llvm::Type::getInt64Ty(_context);
      break;
    case BuiltinKind::LongLong:
      result = llvm::Type::getInt64Ty(_context);
      break;
    case BuiltinKind::I1:
      result = llvm::Type::getInt1Ty(_context);
      break;
    }
    break;
  case TypeBaseType::PointerType:
    result = llvm::PointerType::get(_context, 0);
    break;
  case TypeBaseType::FunctionType:
    result = ConvertFunction(type->As<FunctionType>());
    break;
  case TypeBaseType::ArrayType: {
    const auto *array = type->As<ArrayType>();
    if (array->size) {
      result = llvm::ArrayType::get(Convert(array->element_type), *array->size);
    } else {
      result = llvm::PointerType::get(_context, 0);
    }
    break;
  }
  case TypeBaseType::StructType:
    result = ConvertStruct(type->As<StructType>());
    break;
  case TypeBaseType::UnionType:
    result = ConvertUnion(type->As<UnionType>());
    break;
  case TypeBaseType::EnumType: {
    const auto *enum_type = type->As<EnumType>();
    result = enum_type->underlying_type ? Convert(enum_type->underlying_type)
                                        : llvm::Type::getInt32Ty(_context);
    break;
  }
  }

  ASSERT(result != nullptr, "Failed to convert TypeRef to LLVM type");
  _types.emplace(key, result);
  return result;
}

llvm::FunctionType *LLVMTypeConverter::ConvertFunction(FunctionType *type) {
  ASSERT(type != nullptr, "Expected a C function type");
  if (auto found = _types.find(type); found != _types.end()) {
    return llvm::cast<llvm::FunctionType>(found->second);
  }

  std::vector<llvm::Type *> params;
  params.reserve(type->parameters.size());
  for (TypeRef param : type->parameters) {
    params.push_back(ConvertParameter(param));
  }

  auto *result = llvm::FunctionType::get(Convert(type->return_type), params,
                                         type->has_va_args);
  _types.emplace(type, result);
  return result;
}

llvm::StructType *LLVMTypeConverter::ConvertStruct(StructType *type) {
  ASSERT(type != nullptr, "Expected a C struct type");
  if (auto found = _types.find(type); found != _types.end()) {
    return llvm::cast<llvm::StructType>(found->second);
  }

  std::string name = std::format("struct.{}.{}", type->type_name, _counter());
  auto *result = llvm::StructType::create(_context, name);
  _types.emplace(type, result);

  if (type->is_complete) {
    std::vector<llvm::Type *> fields;
    fields.reserve(type->fields.size());
    for (size_t i = 0; i < type->fields.size(); ++i) {
      type->fields[i].index = i;
      fields.push_back(Convert(type->fields[i].type));
    }
    result->setBody(fields);
  }
  return result;
}

llvm::StructType *LLVMTypeConverter::ConvertUnion(UnionType *type) {
  ASSERT(type != nullptr, "Expected a C union type");
  if (auto found = _types.find(type); found != _types.end()) {
    return llvm::cast<llvm::StructType>(found->second);
  }

  std::string name = std::format("union.{}.{}", type->type_name, _counter());
  auto *result = llvm::StructType::create(_context, name);
  _types.emplace(type, result);

  if (type->is_complete) {
    size_t max_size = (size_t)-1;
    size_t max_size_index = (size_t)-1;
    for (size_t i = 0; i < type->fields.size(); i++) {
      auto field_type = type->fields[i].type;
      if (max_size == (size_t)-1 || field_type->GetSize() > max_size) {
        max_size = field_type->GetSize();
        max_size_index = i;
      }
      Convert(field_type);
    }
    if (max_size_index != (size_t)-1) {
      result->setBody(Convert(type->fields[max_size_index].type));
    }
  }
  return result;
}

llvm::ArrayType *LLVMTypeConverter::ConvertArray(ArrayType *type, size_t size) {
  ASSERT(type != nullptr, "Expected a C array type");
  if (auto found = _types.find(type); found != _types.end()) {
    return llvm::cast<llvm::ArrayType>(found->second);
  }

  auto *result = llvm::ArrayType::get(Convert(type->element_type), size);
  _types.emplace(type, result);
  return result;
}

GenContext::GenContext(llvm::LLVMContext &llvm_context, llvm::Module &module,
                       TypeContext &types)
    : counter(1), _llvm_context(llvm_context), _module(module), types(types),
      type_converter(llvm_context, counter) {}

llvm::Value *GenContext::CreateStringConstant(std::string_view value,
                                              size_t buffer_size) {
  llvm::Constant *data = nullptr;
  if (value.size() + 1 == buffer_size) {
    data = llvm::ConstantDataArray::getString(_llvm_context, value, true);
  } else {
    std::string s(buffer_size, '\0');
    s.replace(0, value.size(), value);
    data = llvm::ConstantDataArray::getString(_llvm_context, s, false);
  }
  auto *global = new llvm::GlobalVariable(
      _module, data->getType(), true, llvm::GlobalValue::PrivateLinkage, data,
      std::format(".str.{}", counter()));
  global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  global->setAlignment(llvm::Align(1));
  return global;
}

llvm::Constant *GenContext::CreateLiteralConstant(std::string_view value,
                                                  size_t buffer_size) {
  if (value.size() + 1 == buffer_size) {
    return llvm::ConstantDataArray::getString(_llvm_context, value, true);
  }
  std::string str;
  str.reserve(buffer_size);
  str.assign(value.begin(), value.end());
  str.resize(buffer_size, '\0');
  auto *data = llvm::ConstantDataArray::getString(_llvm_context, str, false);
  return data;
}
