#pragma once

#include "../../AST/ASTNode.h"
#include "GenContext.h"
#include <llvm/IR/Constants.h>

using NumericValue = std::variant<long double, uint64_t, int64_t>;

NumericValue EvalNumeric(ASTNode *node, GenContext &context);

llvm::Constant *EvalNumericConstant(TypeRef type, ASTNode *node,
                                    GenContext &context);
