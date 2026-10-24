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
    _buffer += std::format("store {} {},ptr {}\n", GetTypeIRName(value.type),
                           value.name, ptr.name);
    return ptr;
  }

  IRValue CreateBinaryOp(std::string_view op, const IRValue &left,
                         const IRValue &right) {
    size_t out_reg = GetCounter();
    _buffer += std::format("%{}={} {} {},{}\n", out_reg, op,
                           GetTypeIRName(left.type), left.name, right.name);
    return IRValue::LocalReg(left.type, out_reg);
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
    _buffer +=
        std::format("br {} {},label %{},label %{}\n", GetTypeIRName(cond.type),
                    cond.name, true_label, false_label);
  }

  IRValue CreateAlloca(TypeRef type) {
    size_t out_reg = GetCounter();
    _buffer += std::format("%{}=alloca {},align {}\n", out_reg,
                           GetTypeIRName(type), type->GetAlign());
    return IRValue::LocalReg(type, out_reg);
  }

  IRValue GetElementPtr(StructType *type, const IRValue &val,
                        std::string_view field_name) {
    size_t out_reg = GetCounter();
    auto field_ = type->GetField(field_name);
    if (field_.has_value()) {
      auto &field = field_.value();
      _buffer += std::format("%{}=getelementptr inbounds {},ptr {},{} {}",
                             out_reg, GetTypeIRName(type), val.name,
                             GetTypeIRName(field.type), field.index);
      return IRValue::LocalReg(field.type, out_reg);
    }
    return IRValue{};
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

  IRValue CreateBoolCond(IRValue &val) {
    size_t out_reg = GetCounter();
    _buffer += std::format("%{}=icmp ne {} {},0\n", out_reg,
                           GetTypeIRName(val.type), val.name);
    return IRValue::LocalReg(_context.Types().Builtin(BuiltinKind::I1),
                             out_reg);
  }

  bool IsTerminated() const { return _is_terminated; }

private:
  size_t GetCounter() { return _context.counter(); }

  std::string &_buffer;
  GenContext &_context;

  bool _is_terminated = false;
};
