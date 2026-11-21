#pragma once

#include "../../AST/AST.h"
#include "GenContext.h"
#include <llvm/IR/Constants.h>

llvm::Constant *LowerNumericConstant(TypeRef type, ASTNode *node,
                                     GenContext &context);

llvm::Constant *LowerConstantExpression(TypeRef type, ASTNode *node,
                                        GenContext &context);

llvm::Constant *LowerFloatingConstant(llvm::Type *llvm_type, long double value);
