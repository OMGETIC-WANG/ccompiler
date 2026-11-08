#include "Va.h"

#include "GenExpr.h"
#include "IRBuilder.h"

static ASTNode *GetExprArg(std::vector<IntrinsicCallArg> &args, size_t index) {
  ASSERT(index < args.size(), "Missing intrinsic expression argument");
  auto *arg = std::get_if<std::unique_ptr<ASTNode>>(&args[index]);
  ASSERT(arg != nullptr && *arg != nullptr,
         "Expected an expression intrinsic argument");
  return arg->get();
}

static TypeRef GetTypeArg(std::vector<IntrinsicCallArg> &args, size_t index) {
  ASSERT(index < args.size(), "Missing intrinsic type argument");
  auto *arg = std::get_if<TypeRef>(&args[index]);
  ASSERT(arg != nullptr && *arg != nullptr,
         "Expected a type intrinsic argument");
  return *arg;
}

RValue GenVaStart(std::vector<IntrinsicCallArg> &args,
                  std::variant<ExprEmitter *, GenContext *> arg) {
  ASSERT(std::holds_alternative<ExprEmitter *>(arg),
         "va_start requires ExprEmitter");
  ExprEmitter *emitter = std::get<ExprEmitter *>(arg);
  LValue va_list = emitter->EmitLValue(GetExprArg(args, 0));
  emitter->builder.CreateVaStart(va_list);
  return RValue{emitter->context.Types().getVoid(), nullptr};
}

RValue GenVaArg(std::vector<IntrinsicCallArg> &args,
                std::variant<ExprEmitter *, GenContext *> arg) {
  ASSERT(std::holds_alternative<ExprEmitter *>(arg),
         "va_start requires ExprEmitter");
  ExprEmitter *emitter = std::get<ExprEmitter *>(arg);
  LValue va_list = emitter->EmitLValue(GetExprArg(args, 0));
  return emitter->builder.CreateVaArg(va_list, GetTypeArg(args, 1));
}

RValue GenVaEnd(std::vector<IntrinsicCallArg> &args,
                std::variant<ExprEmitter *, GenContext *> arg) {
  ASSERT(std::holds_alternative<ExprEmitter *>(arg),
         "va_start requires ExprEmitter");
  ExprEmitter *emitter = std::get<ExprEmitter *>(arg);
  LValue va_list = emitter->EmitLValue(GetExprArg(args, 0));
  emitter->builder.CreateVaEnd(va_list);
  return RValue{emitter->context.Types().getVoid(), nullptr};
}
