#include "GenExpr.h"
#include "../../AST/AST.h"
#include "../../AST/Operations.h"
#include "../../AST/Type.h"
#include "IRBuilder.h"

static std::string_view BinaryOpStr(BinaryOpType op_type) {
  switch (op_type) {
  case BinaryOpType::Add:
    return "add";
  case BinaryOpType::Sub:
    return "sub";
  case BinaryOpType::Mul:
    return "mul";
  case BinaryOpType::Div:
    return "sdiv";
  default:
    RAISE_ERROR("Not suppport yet");
  }
}

struct ExprEmitter::detail {
  static void GenVarInitializer(const IRValue &value, Initializer *initializer,
                                ExprEmitter &self) {
    if (!initializer)
      return;
    if (auto val_init = initializer->DynCast<ValueInitializer>()) {
      IRValue init_val = self.EmitRValue(val_init->value.get());
      self.builder.CreateStore(init_val, value);
      return;
    }
    if (auto field_init = initializer->DynCast<FieldInitializer>()) {
      StructType *t = value.type->DynCast<StructType>();
      for (const auto &[name, initializer] : field_init->field_initializers) {
        IRValue field_lhs = self.builder.GetElementPtr(t, value, name);
        if (field_lhs.kind == IRValueKind::Null) {
          RAISE_ERROR("Unknown field name: ", name);
        }
        GenVarInitializer(field_lhs, initializer.get(), self);
      }
      return;
    }
  }

  static IRValue GenVarDecl(VarDecl *decl, ExprEmitter &self) {
    IRValue val = self.builder.CreateAlloca(decl->type);
    self.context.CurrLayer().variables[decl->name] = val;
    GenVarInitializer(val, decl->initializer.get(), self);
    return val;
  }
};

IRValue ExprEmitter::EmitLValue(ASTNode *node) {
  if (auto var_ref = node->DynCast<VarRef>()) {
    auto val = context.FindVariable(var_ref->name);
    ASSERT(val != nullptr, "Variable not found: ", var_ref->name);
    return *val;
  }
  if (auto var_decl = node->DynCast<VarDecl>()) {
    return detail::GenVarDecl(var_decl, *this);
  }
  return IRValue{};
}

IRValue ExprEmitter::EmitRValue(ASTNode *node) {
  if (auto literal_num = node->DynCast<LiteralNum>()) {
    if (std::holds_alternative<int64_t>(literal_num->value)) {
      int64_t value = std::get<int64_t>(literal_num->value);
      return IRValue::Constant(context.Types().Builtin(BuiltinKind::Int),
                               std::to_string(value));
    } else if (std::holds_alternative<double>(literal_num->value)) {
      double value = std::get<double>(literal_num->value);
      return IRValue::Constant(context.Types().Builtin(BuiltinKind::Float),
                               std::to_string(value));
    } else {
      RAISE_ERROR("Unsupported literal num type");
    }
  }
  if (auto literal_str = node->DynCast<LiteralString>()) {
    std::string_view str = literal_str->value;
    std::string symbol = std::format("@.literal_string_{}", context.counter());
    context.literal_strings.emplace_back(symbol, str);
    return IRValue::Global(
        context.Types().Pointer(context.Types().Builtin(BuiltinKind::Char)),
        symbol);
  }
  if (auto var_ref = node->DynCast<VarRef>()) {
    auto val = context.FindVariable(var_ref->name);
    ASSERT(val != nullptr, "Variable not found: ", var_ref->name);
    return builder.CreateLoad(val->type, *val);
  }
  if (auto var_decl = node->DynCast<VarDecl>()) {
    auto val_lhs = detail::GenVarDecl(var_decl, *this);
    return builder.CreateLoad(val_lhs.type, val_lhs);
  }
  if (auto bin_expr = node->DynCast<BinaryOp>()) {
    if (bin_expr->op_type == BinaryOpType::Assignment) {
      auto left = EmitLValue(bin_expr->left.get());
      auto right = EmitRValue(bin_expr->right.get());
      builder.CreateStore(right, left);
      return left;
    }
    IRValue left = EmitRValue(bin_expr->left.get());
    IRValue right = EmitRValue(bin_expr->right.get());
    return builder.CreateBinaryOp(BinaryOpStr(bin_expr->op_type), left, right);
  }
  RAISE_ERROR("Not support yet");
}

void ExprEmitter::EmitStmt(ASTNode *node) {
  if (auto raw_ir = node->DynCast<RawLLVMIR>()) {
    builder.Buffer() += "\n; raw LLVM IR begin\n";
    builder.Buffer() += raw_ir->ir;
    builder.Buffer() += "\n; raw LLVM IR end\n";
    return;
  }
  if (auto var_decl = node->DynCast<VarDecl>()) {
    auto val_lhs = detail::GenVarDecl(var_decl, *this);
    return;
  }
  if (auto if_stmt = node->DynCast<IfStmt>()) {
    GenContextLayerGuard guard(context);
    IRValue cond = EmitRValue(if_stmt->condition.get());
    std::string true_label = std::format("if_true_{}", context.counter());
    std::string false_label = std::format("if_false_{}", context.counter());
    std::string end_label = std::format("if_end_{}", context.counter());
    if (cond.type != context.Types().Builtin(BuiltinKind::Char)) {
      cond = builder.CreateBoolCond(cond);
    }
    builder.CreateCondJmp(cond, true_label,
                          if_stmt->else_branch ? false_label : end_label);
    builder.CreateLabel(true_label);
    EmitStmt(if_stmt->then_branch.get());
    bool then_fallthrough = !builder.IsTerminated();
    if (then_fallthrough) {
      builder.CreateJmp(end_label);
    }
    bool else_fallthrough = false;
    if (if_stmt->else_branch) {
      builder.CreateLabel(false_label);
      EmitStmt(if_stmt->else_branch.get());
      else_fallthrough = !builder.IsTerminated();
      if (else_fallthrough) {
        builder.CreateJmp(end_label);
      }
    }
    if (then_fallthrough || else_fallthrough) {
      builder.CreateLabel(end_label);
    }
    return;
  }
  if (auto ret_stmt = node->DynCast<ReturnStmt>()) {
    if (ret_stmt->ret_value) {
      IRValue ret_val = EmitRValue(ret_stmt->ret_value.get());
      builder.CreateRet(ret_val);
    } else {
      builder.CreateVoidRet();
    }
    return;
  }
  if (auto compound_stmt = node->DynCast<CompoundStmt>()) {
    GenContextLayerGuard guard(context);
    for (const auto &statement : compound_stmt->statements) {
      EmitStmt(statement.get());
    }
    return;
  }
  EmitRValue(node);
}
