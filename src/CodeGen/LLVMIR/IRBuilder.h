#pragma once

#include "../../AST/ASTNode.h"
#include "../../AST/Type.h"
#include "GenContext.h"
#include "Naming.h"

class IRBuilder {
public:
  IRBuilder(std::string &buffer, GenContext &context)
      : _buffer(buffer), _context(context) {}

  std::string &Buffer() { return _buffer; }

  IRValue CreateLoad(TypeRef type, const IRValue &ptr) {
    size_t out_reg = GetCounter();
    _buffer += std::format("%{}=load {},ptr {}\n", out_reg, GetTypeIRName(type),
                           ptr.name);
    return IRValue::LocalReg(type, out_reg);
  }

  IRValue CreateStore(const IRValue &value, const IRValue &ptr) {
    _buffer += std::format("store {} {},ptr {}\n", GetTypeIRName(ptr.type),
                           value.name, ptr.name);
    return ptr;
  }

  IRValue CreateBinaryOp(std::string_view op, const IRValue &left,
                         const IRValue &right, TypeRef out_type = nullptr) {
    size_t out_reg = GetCounter();
    _buffer += std::format("%{}={} {} {},{}\n", out_reg, op,
                           GetTypeIRName(left.type), left.name, right.name);
    return IRValue::LocalReg(out_type ? out_type : left.type, out_reg);
  }

  void CreateLabel(std::string_view label) {
    _buffer += std::format("{}:\n", label);
    _is_terminated = false;
  }

  void CreateJmp(std::string_view label) {
    if (_is_terminated)
      return;
    _is_terminated = true;
    _buffer += std::format("br label %{}\n", label);
  }

  void CreateCondJmp(const IRValue &cond, std::string_view true_label,
                     std::string_view false_label) {
    if (cond.type != _context.Types().Builtin(BuiltinKind::I1, TypeTag::None)) {
      CreateCondJmp(CreateBoolCond(cond), true_label, false_label);
    } else {
      _buffer += std::format("br {} {},label %{},label %{}\n",
                             GetTypeIRName(cond.type), cond.name, true_label,
                             false_label);
    }
  }

  IRValue CreateAlloca(TypeRef type) {
    size_t out_reg = GetCounter();
    _buffer += std::format("%{}=alloca {},align {}\n", out_reg,
                           GetTypeIRName(type), type->GetAlign());
    return IRValue::LocalReg(type, out_reg);
  }

  IRValue GetElementPtr(const IRValue &val, std::string_view field_name) {
    ASSERT(val.type->Is<StructType>(),
           "GetElementPtr only supports struct type");
    StructType *type = val.type->As<StructType>();
    auto field_ = type->GetField(field_name);
    if (field_.has_value()) {
      auto &field = field_.value();
      return GetElementPtr(val, field);
    }
    return IRValue{};
  }

  IRValue GetElementPtr(const IRValue &val, const StructField &field) {
    size_t out_reg = GetCounter();
    _buffer += std::format("%{}=getelementptr inbounds {},ptr {},i64 0,{} {}\n",
                           out_reg, GetTypeIRName(val.type->As<StructType>()),
                           val.name, GetTypeIRName(field.type), field.index);
    return IRValue::LocalReg(field.type, out_reg);
  }

  IRValue CreateSubscript(const IRValue &val, const IRValue &index) {
    size_t out_reg = GetCounter();
    if (auto array_type = val.type->DynCast<ArrayType>()) {
      _buffer +=
          std::format("%{}=getelementptr inbounds {},ptr {},i64 0,{} {}\n",
                      out_reg, GetTypeIRName(array_type), val.name,
                      GetTypeIRName(index.type), index.name);
      return IRValue::LocalReg(array_type->element_type, out_reg);
    } else if (auto pointer_type = val.type->DynCast<PointerType>()) {
      std::string_view elem_type_s = GetTypeIRName(pointer_type->pointee_type);
      _buffer += std::format("%{}=getelementptr inbounds {},ptr {},{} {}\n",
                             out_reg, elem_type_s, val.name,
                             GetTypeIRName(index.type), index.name);
      return IRValue::LocalReg(pointer_type->pointee_type, out_reg);
    } else {
      RAISE_ERROR("Subscript only supports array or pointer type");
    }
  }

  void CreateRet(const IRValue &value) {
    if (_is_terminated)
      return;
    _is_terminated = true;
    _buffer +=
        std::format("ret {} {}\n", GetTypeIRName(value.type), value.name);
  }

  void CreateVoidRet() {
    if (_is_terminated)
      return;
    _is_terminated = true;
    _buffer += "ret void\n";
  }

  IRValue CreateBoolCond(const IRValue &val) {
    size_t out_reg = GetCounter();
    if (val.type->Is<PointerType>()) {
      _buffer += std::format("%{}=icmp ne {} {},null\n", out_reg,
                             GetTypeIRName(val.type), val.name);
    } else {
      _buffer += std::format("%{}=icmp ne {} {},0\n", out_reg,
                             GetTypeIRName(val.type), val.name);
    }
    return IRValue::LocalReg(
        _context.Types().Builtin(BuiltinKind::I1, TypeTag::None), out_reg);
  }

  IRValue CreateNumericCast(const IRValue &val, TypeRef target_type) {
    size_t out_reg = GetCounter();
    std::string_view op = GetNumericCastOp(val.type, target_type);
    _buffer += std::format("%{}={} {} {} to {}\n", out_reg, op,
                           GetTypeIRName(val.type), val.name,
                           GetTypeIRName(target_type));
    return IRValue::LocalReg(target_type, out_reg);
  }

  IRValue CreatePhi(const IRValue &val1, std::string_view label1,
                    const IRValue &val2, std::string_view label2) {
    ASSERT(val1.type == val2.type, "Phi values must have the same type");
    size_t out_reg = GetCounter();
    _buffer += std::format("%{}=phi {} [{},%{}],[{},%{}]\n", out_reg,
                           GetTypeIRName(val1.type), val1.name, label1,
                           val2.name, label2);
    return IRValue::LocalReg(val1.type, out_reg);
  }

  IRValue CreateFuncInvoke(const IRValue &func, std::span<IRValue> args) {
    FunctionType *func_t = func.type->DynCast<FunctionType>();
    if (!func_t) {
      auto ptr_t = func.type->DynCast<PointerType>();
      ASSERT(ptr_t, "Function invoke only supports function or pointer type");
      ASSERT(ptr_t->pointee_type->Is<FunctionType>(),
             "Function invoke only supports function or pointer type");
      func_t = ptr_t->pointee_type->As<FunctionType>();
    }

    std::string ir = std::format("call {} {}(",
                                 GetTypeIRName(func_t->return_type), func.name);
    for (size_t i = 0; i < args.size(); i++) {
      if (i > 0) {
        ir += ",";
      }
      ir += std::format("{} {}", GetTypeIRName(args[i].type), args[i].name);
    }
    ir += ')';

    if (func_t->return_type->Is<BuiltinType>() &&
        func_t->return_type->As<BuiltinType>()->Kind() == BuiltinKind::Void) {
      _buffer += ir;
      _buffer += '\n';
      return IRValue{};
    } else {
      size_t out_reg = GetCounter();
      _buffer += std::format("%{}={}\n", out_reg, ir);
      return IRValue::LocalReg(func_t->return_type, out_reg);
    }
  }

  IRValue CreateAutoCastLoad(TypeRef type, const IRValue &ptr) {
    IRValue val = CreateLoad(ptr.type, ptr);
    int promote = GetPromote(ptr.type, type);
    if (promote != -1) {
      val = CreateNumericCast(val, type);
    }
    return val;
  }

  IRValue CreateAutoCastStore(const IRValue &value, const IRValue &ptr) {
    int promote = GetPromote(value.type, ptr.type);
    IRValue val = value;
    if (promote == 0) {
      val = CreateNumericCast(val, ptr.type);
    }
    return CreateStore(val, ptr);
  }

  IRValue CreateInv(const IRValue &value) {
    if (IsIntegerType(value.type)) {
      size_t out_reg = GetCounter();
      _buffer += std::format("%{}=sub {} 0,{}\n", out_reg,
                             GetTypeIRName(value.type), value.name);
      return IRValue::LocalReg(value.type, out_reg);
    } else {
      size_t out_reg = GetCounter();
      _buffer += std::format("%{}=fneg {} 0.0,{}\n", out_reg,
                             GetTypeIRName(value.type), value.name);
      return IRValue::LocalReg(value.type, out_reg);
    }
  }

  bool IsTerminated() const { return _is_terminated; }

private:
  static std::string_view GetNumericCastOp(TypeRef from_, TypeRef to_) {
    auto from = from_->DynCast<BuiltinType>(), to = to_->DynCast<BuiltinType>();
    ASSERT(from && to, "Numeric cast only supports builtin type");
    if (IsIntegerType(from) && IsIntegerType(to)) {
      if (from->GetSize() > to->GetSize()) {
        return "trunc";
      } else {
        if (HasTypeTag(from_, TypeTag::Unsigned)) {
          return "zext";
        } else {
          return "sext";
        }
      }
    }
    if (IsFloatingType(from) && IsIntegerType(to)) {
      return HasTypeTag(to_, TypeTag::Unsigned) ? "fptoui" : "fptosi";
    }
    if (IsIntegerType(from) && IsFloatingType(to)) {
      return HasTypeTag(from_, TypeTag::Unsigned) ? "uitofp" : "sitofp";
    }
    RAISE_ERROR("Unsupported numeric cast");
  }

  size_t GetCounter() { return _context.counter(); }

  std::string &_buffer;
  GenContext &_context;

  bool _is_terminated = false;
};
