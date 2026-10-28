#include "GenExpr.h"
#include "../../AST/AST.h"
#include "../../AST/Operations.h"
#include "../../AST/Type.h"
#include "GenDecl.h"
#include "IRBuilder.h"

static int GetPromote(const IRValue &left, const IRValue &right) {
  return GetPromote(left.type, right.type);
}

static std::string_view GetArithmeticOpStr(BinaryOpType op_type,
                                           bool is_unsigned = false,
                                           bool is_float = false) {
  switch (op_type) {
  case BinaryOpType::Add:
    return is_float ? "fadd" : "add";
  case BinaryOpType::Sub:
    return is_float ? "fsub" : "sub";
  case BinaryOpType::Mul:
    return is_float ? "fmul" : "mul";
  case BinaryOpType::Div:
    return is_float ? "fdiv" : (is_unsigned ? "udiv" : "sdiv");
  case BinaryOpType::Mod:
    return is_float ? "frem" : (is_unsigned ? "urem" : "srem");
  default:
    RAISE_ERROR("Not suppport yet");
  }
}

static bool IsArithmeticOp(BinaryOpType op_type) {
  switch (op_type) {
  case BinaryOpType::Add:
  case BinaryOpType::Sub:
  case BinaryOpType::Mul:
  case BinaryOpType::Div:
  case BinaryOpType::Mod:
    return true;
  default:
    return false;
  }
}

#define MAKE_COMP_OP(raw)                                                      \
  is_unsigned ? ("icmp u" raw)                                                 \
              : (is_float ? (is_ordered ? ("fcmp o" raw) : ("fcmp u" raw))     \
                          : ("icmp s" raw))

static std::string_view GetCompOpStr(BinaryOpType op_type,
                                     bool is_unsigned = false,
                                     bool is_float = false,
                                     bool is_ordered = true) {
  switch (op_type) {
  case BinaryOpType::Eq:
    return "icmp eq";
  case BinaryOpType::Ne:
    return "icmp ne";
  case BinaryOpType::Lt:
    return MAKE_COMP_OP("lt");
  case BinaryOpType::Gt:
    return MAKE_COMP_OP("gt");
  case BinaryOpType::Le:
    return MAKE_COMP_OP("le");
  case BinaryOpType::Ge:
    return MAKE_COMP_OP("ge");
  default:
    RAISE_ERROR("Unexpected BinaryOpType ", op_type);
  }
}

static IRValue TryCastTo(TypeRef type, const IRValue &val, IRBuilder &builder) {
  int promote = GetPromote(type, val.type);
  if (promote == -1) {
    ASSERT(type == val.type, "Failed to cast from ", type->GetType(), " to ",
           val.type->GetType());
    return val;
  } else {
    return builder.CreateNumericCast(val, type);
  }
}

struct ExprEmitter::detail {
  static void GenVarInitializer(const IRValue &value, TypeBase *type,
                                Initializer *initializer, ExprEmitter &self) {
    if (!initializer)
      return;
    if (auto val_init = initializer->DynCast<ValueInitializer>()) {
      IRValue init_val = self.EmitRValue(val_init->value.get());
      self.builder.CreateStore(TryCastTo(type, init_val, self.builder), value);
      return;
    }
    if (auto field_init = initializer->DynCast<FieldInitializer>()) {
      for (const auto &[name, initializer] : field_init->field_initializers) {
        auto field_ = type->As<StructType>()->GetField(name);
        if (field_.has_value()) {
          const auto &field = field_.value();
          IRValue field_lhs = self.builder.GetElementPtr(value, field);
          GenVarInitializer(field_lhs, field.type.get(), initializer.get(),
                            self);
        } else {
          RAISE_ERROR("Unknown field: ", name);
        }
      }
      return;
    }
  }

  static IRValue GenVarDecl(VarDecl *decl, ExprEmitter &self) {
    ASSERT(decl->storage_class == StorageClass::None,
           "Local static storage is not supported yet");
    IRValue val = self.builder.CreateAlloca(decl->type);
    self.context.CurrLayer().variables[decl->name] = val;
    GenVarInitializer(val, val.type.get(), decl->initializer.get(), self);
    return val;
  }

  static IRValue LoadAsRValue(const IRValue &val, ExprEmitter &self) {
    if (val.type->Is<ArrayType>()) {
      return IRValue::LocalReg(
          self.context.Types().Pointer(val.type->As<ArrayType>()->element_type),
          val.name);
    }
    if (val.type->Is<FunctionType>()) {
      return IRValue::LocalReg(
          self.context.Types().Pointer(val.type->As<FunctionType>()), val.name);
    }
    return self.builder.CreateLoad(val.type, val);
  }

  static IRValue CastToI1IfNeeded(const IRValue &val, ExprEmitter &self) {
    if (val.type !=
        self.context.Types().Builtin(BuiltinKind::I1, TypeTag::Unsigned)) {
      return self.builder.CreateBoolCond(val);
    }
    return val;
  }

  enum class EvalAndOr { And, Or };

  template <EvalAndOr eval_kind>
  static IRValue EvalAndOr(ASTNode *left, ASTNode *right, ExprEmitter &self) {
    size_t label_id = self.context.counter();
    std::string start_label = std::format(
        eval_kind == EvalAndOr::And ? "and_eval.start_{}" : "or_eval.start_{}",
        label_id);
    std::string next_label = std::format(
        eval_kind == EvalAndOr::And ? "and_eval.next_{}" : "or_eval.next_{}",
        label_id);
    std::string end_label = std::format(
        eval_kind == EvalAndOr::And ? "and_eval.end_{}" : "or_eval.end_{}",
        label_id);

    self.builder.CreateJmp(start_label);
    self.builder.CreateLabel(start_label);
    IRValue left_val = self.EmitRValue(left);
    if constexpr (eval_kind == EvalAndOr::And) {
      self.builder.CreateCondJmp(left_val, next_label, end_label);
    } else {
      self.builder.CreateCondJmp(left_val, end_label, next_label);
    }
    self.builder.CreateLabel(next_label);
    IRValue right_val = self.EmitRValue(right);
    if (right_val.type !=
        self.context.Types().Builtin(BuiltinKind::I1, TypeTag::Unsigned)) {
      right_val = self.builder.CreateBoolCond(right_val);
    }
    self.builder.CreateJmp(end_label);
    self.builder.CreateLabel(end_label);
    if constexpr (eval_kind == EvalAndOr::And) {
      return self.builder.CreatePhi(
          IRValue::Constant(
              self.context.Types().Builtin(BuiltinKind::I1, TypeTag::Unsigned),
              "false"),
          start_label, right_val, next_label);
    } else {
      return self.builder.CreatePhi(
          IRValue::Constant(
              self.context.Types().Builtin(BuiltinKind::I1, TypeTag::Unsigned),
              "true"),
          start_label, right_val, next_label);
    }
  }

  static IRValue EvalLogicAnd(ASTNode *left, ASTNode *right,
                              ExprEmitter &self) {
    return EvalAndOr<EvalAndOr::And>(left, right, self);
  }

  static IRValue EvalLogicOr(ASTNode *left, ASTNode *right, ExprEmitter &self) {
    return EvalAndOr<EvalAndOr::Or>(left, right, self);
  }
};

IRValue ExprEmitter::EmitLValue(ASTNode *node) {
  if (auto var_ref = node->DynCast<VarRef>()) {
    auto pval = context.FindVariable(var_ref->name);
    if (pval) {
      return *pval;
    }
    pval = context.FindFunction(var_ref->name);
    if (pval) {
      return *pval;
    }
    RAISE_ERROR("Unknown variable or function: ", var_ref->name);
  }
  if (auto var_decl = node->DynCast<VarDecl>()) {
    return detail::GenVarDecl(var_decl, *this);
  }
  if (auto get_field = node->DynCast<GetField>()) {
    IRValue operand = EmitLValue(get_field->operand.get());
    IRValue field_lhs = builder.GetElementPtr(operand, get_field->field_name);
    return field_lhs;
  }
  if (auto binary = node->DynCast<BinaryOp>()) {
    switch (binary->op_type) {
    case BinaryOpType::Subscript: {
      IRValue array = EmitRValue(binary->left.get());
      IRValue index = EmitRValue(binary->right.get());
      return builder.CreateSubscript(array, index);
    }
    default:
      RAISE_ERROR("Unexpected op");
    }
  }
  if (auto unary = node->DynCast<UnaryOp>()) {
    switch (unary->op_type) {
    case UnaryOpType::Deref: {
      IRValue ptr = EmitRValue(unary->operand.get());
      return IRValue::LocalReg(ptr.type->DynCast<PointerType>()->pointee_type,
                               ptr.name);
    }
    default:
      break;
    }
  }
  return IRValue{};
}

static bool LosesConstInPointerConversion(TypeRef from, TypeRef to) {
  if (!from->Is<PointerType>() || !to->Is<PointerType>()) {
    return false;
  }

  TypeRef from_pointee = from->As<PointerType>()->pointee_type;
  TypeRef to_pointee = to->As<PointerType>()->pointee_type;
  if (HasTypeTag(from_pointee, TypeTag::Const) &&
      !HasTypeTag(to_pointee, TypeTag::Const)) {
    return true;
  }
  return LosesConstInPointerConversion(from_pointee, to_pointee);
}

static void CheckConstAssignment(TypeRef target, TypeRef value) {
  ASSERT(!HasTypeTag(target, TypeTag::Const),
         "Cannot assign to a const-qualified object");
  ASSERT(!LosesConstInPointerConversion(value, target),
         "Cannot assign a pointer after discarding const");
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
  if (auto var_decl = node->DynCast<VarDecl>()) {
    auto val_lhs = detail::GenVarDecl(var_decl, *this);
    return detail::LoadAsRValue(val_lhs, *this);
  }
  if (auto bin_expr = node->DynCast<BinaryOp>()) {
    switch (bin_expr->op_type) {
    case BinaryOpType::Assignment: {
      IRValue left = EmitLValue(bin_expr->left.get());
      IRValue right = EmitRValue(bin_expr->right.get());
      CheckConstAssignment(left.type, right.type);
      int promote = GetPromote(left, right);
      ASSERT(promote == -1 || promote == 1, "Failed to cast ",
             right.type->GetType(), " to ", left.type->GetType());
      if (promote == 1) {
        right = builder.CreateNumericCast(right, left.type);
      }
      builder.CreateStore(right, left);
      return right;
    }
    case BinaryOpType::And:
      return detail::EvalLogicAnd(bin_expr->left.get(), bin_expr->right.get(),
                                  *this);
    case BinaryOpType::Or:
      return detail::EvalLogicOr(bin_expr->left.get(), bin_expr->right.get(),
                                 *this);
    case BinaryOpType::Eq:
    case BinaryOpType::Ne:
    case BinaryOpType::Lt:
    case BinaryOpType::Gt:
    case BinaryOpType::Le:
    case BinaryOpType::Ge:
    case BinaryOpType::Add:
    case BinaryOpType::Sub:
    case BinaryOpType::Mul:
    case BinaryOpType::Div:
    case BinaryOpType::Mod: {
      IRValue left = EmitRValue(bin_expr->left.get());
      IRValue right = EmitRValue(bin_expr->right.get());
      if (left.type->Is<PointerType>()) {
        ASSERT(IsIntegerType(right.type),
               "Pointer arithmetic only supports integer type");
        ASSERT(bin_expr->op_type == BinaryOpType::Add ||
                   bin_expr->op_type == BinaryOpType::Sub,
               "Pointer arithmetic only supports addition and subtraction");
        if (bin_expr->op_type == BinaryOpType::Sub) {
          right = builder.CreateInv(right);
        }
        IRValue subscripted = builder.CreateSubscript(left, right);
        subscripted.type = left.type;
        return subscripted;
      }
      int promote = GetPromote(left, right);
      switch (promote) {
      case 0:
        left = builder.CreateNumericCast(left, right.type);
        break;
      case 1:
        right = builder.CreateNumericCast(right, left.type);
        break;
      default:
        break;
      }
      bool is_unsigned = HasTypeTag(left.type, TypeTag::Unsigned);
      bool is_float = IsFloatingType(left.type);
      std::string_view op_str =
          IsArithmeticOp(bin_expr->op_type)
              ? GetArithmeticOpStr(bin_expr->op_type, is_unsigned, is_float)
              : GetCompOpStr(bin_expr->op_type, is_unsigned, is_float);
      TypeRef out_type =
          IsArithmeticOp(bin_expr->op_type)
              ? left.type
              : context.Types().Builtin(BuiltinKind::I1, TypeTag::Unsigned);
      return builder.CreateBinaryOp(op_str, left, right, out_type);
    }
    default: {
      break;
    }
    }
  }
  if (auto *func_invoke = node->DynCast<FunctionInvocation>()) {
    std::vector<IRValue> args;
    args.reserve(func_invoke->arguments.size());
    for (size_t i = 0; i < func_invoke->arguments.size(); i++) {
      args.push_back(EmitRValue(func_invoke->arguments[i].get()));
    }
    IRValue func = EmitRValue(func_invoke->function.get());
    return builder.CreateFuncInvoke(func, args);
  }
  if (auto *type_cast = node->DynCast<TypeCast>()) {
    auto operand_rhs = EmitRValue(type_cast->operand.get());
    TypeRef target_type = type_cast->target_type;
    if (operand_rhs.type == target_type) {
      return operand_rhs;
    }
    if (operand_rhs.type->Is<PointerType>() && target_type->Is<PointerType>()) {
      ASSERT(!LosesConstInPointerConversion(operand_rhs.type, target_type),
             "Cannot cast a pointer after discarding const");
      return IRValue::LocalReg(target_type, operand_rhs.name);
    }
    if (IsNumericType(operand_rhs.type) && IsNumericType(target_type)) {
      return builder.CreateNumericCast(operand_rhs, target_type);
    }
    RAISE_ERROR("Unsupported type cast from ", operand_rhs.type->ToString(),
                " to ", target_type->ToString());
  }
  if (auto unary_expr = node->DynCast<UnaryOp>()) {
    switch (unary_expr->op_type) {
    case UnaryOpType::GetAddress: {
      auto operand_lhs = EmitLValue(unary_expr->operand.get());
      return IRValue::LocalReg(context.Types().Pointer(operand_lhs.type),
                               operand_lhs.name);
    }
    case UnaryOpType::IncBeforeUse:
    case UnaryOpType::IncAfterUse:
    case UnaryOpType::DecBeforeUse:
    case UnaryOpType::DecAfterUse: {
      bool is_inc = (unary_expr->op_type == UnaryOpType::IncBeforeUse ||
                     unary_expr->op_type == UnaryOpType::IncAfterUse);
      bool is_before_use = (unary_expr->op_type == UnaryOpType::IncBeforeUse ||
                            unary_expr->op_type == UnaryOpType::DecBeforeUse);

      IRValue operand = EmitLValue(unary_expr->operand.get());
      ASSERT(!HasTypeTag(operand.type, TypeTag::Const),
             "Cannot modify a const-qualified object");
      IRValue val = builder.CreateLoad(operand.type, operand);
      IRValue added = builder.CreateBinaryOp(is_inc ? "add" : "sub", val,
                                             IRValue::Constant(val.type, "1"));
      builder.CreateStore(added, operand);
      if (is_before_use) {
        return added;
      } else {
        return val;
      }
    }
    default:
      break;
    }
  }
  IRValue lhs = EmitLValue(node);
  if (lhs.kind != IRValueKind::Null) {
    return detail::LoadAsRValue(lhs, *this);
  } else {
    RAISE_ERROR("Unsupported ASTNode type: ", node->GetType());
  }
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
  if (auto label_stmt = node->DynCast<LabelStmt>()) {
    builder.CreateJmp(label_stmt->name);
    builder.CreateLabel(label_stmt->name);
    return;
  }
  if (auto goto_stmt = node->DynCast<GotoStmt>()) {
    builder.CreateJmp(goto_stmt->target);
    return;
  }
  if (auto if_stmt = node->DynCast<IfStmt>()) {
    GenContextLayerGuard guard(context);
    IRValue cond = EmitRValue(if_stmt->condition.get());
    size_t label_id = context.counter();
    std::string true_label = std::format("if.true_{}", label_id);
    std::string false_label = std::format("if.false_{}", label_id);
    std::string end_label = std::format("if.end_{}", label_id);
    builder.CreateCondJmp(cond, true_label,
                          if_stmt->else_branch ? false_label : end_label);
    builder.CreateLabel(true_label);
    {
      GenContextLayerGuard guard(context);
      EmitStmt(if_stmt->then_branch.get());
    }
    bool then_fallthrough = !builder.IsTerminated();
    if (then_fallthrough) {
      builder.CreateJmp(end_label);
    }
    bool else_fallthrough = false;
    if (if_stmt->else_branch) {
      builder.CreateLabel(false_label);
      {
        GenContextLayerGuard guard(context);
        EmitStmt(if_stmt->else_branch.get());
      }
      else_fallthrough = !builder.IsTerminated();
      if (else_fallthrough) {
        builder.CreateJmp(end_label);
      }
    }
    if ((!if_stmt->else_branch) || then_fallthrough || else_fallthrough) {
      builder.CreateLabel(end_label);
    }
    return;
  }
  if (auto ret_stmt = node->DynCast<ReturnStmt>()) {
    if (ret_stmt->ret_value) {
      IRValue ret_val = EmitRValue(ret_stmt->ret_value.get());
      FunctionType *curr_func = context.last_function.type->As<FunctionType>();
      int promote = GetPromote(ret_val.type, curr_func->return_type);
      if (promote == -1) {
        ASSERT(ret_val.type == curr_func->return_type, "Return type mismatch");
      } else {
        ret_val = builder.CreateNumericCast(ret_val, curr_func->return_type);
      }
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
  if (auto struct_decl = node->DynCast<StructDecl>()) {
    context.struct_decls.push_back(GenStructDecl(struct_decl, context));
    return;
  }
  EmitRValue(node);
}
