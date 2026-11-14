#include "Sizeof.h"

#include "../../AST/AST.h"
#include "GenContext.h"
#include "GenExpr.h"

#include <llvm/IR/Constants.h>

RValue GenSizeof(std::vector<IntrinsicCallArg> &args,
                 std::variant<ExprEmitter *, GenContext *> arg) {
  GenContext *context;
  if (std::holds_alternative<ExprEmitter *>(arg)) {
    context = &std::get<ExprEmitter *>(arg)->context;
  } else {
    context = std::get<GenContext *>(arg);
  }

  TypeRef operand_type;
  if (auto *type = std::get_if<TypeRef>(&args[0])) {
    operand_type = *type;
  } else {
    auto *expression = std::get<std::unique_ptr<ASTNode>>(args[0]).get();
    auto *expr = dynamic_cast<Expr *>(expression);
    ASSERT(expr != nullptr && expr->type != nullptr,
           "sizeof expression is missing its Sema type");
    operand_type = expr->type;
  }

  TypeRef result_type = context->Types().getLongLong();
  return RValue{
      result_type,
      llvm::ConstantInt::get(context->LLVMType(result_type),
                             operand_type->GetSize(), false),
  };
}
