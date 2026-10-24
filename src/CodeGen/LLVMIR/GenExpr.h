#pragma once

#include "../../AST/ASTNode.h"
#include "GenContext.h"
#include "IRBuilder.h"

class ExprEmitter {
public:
  ExprEmitter(IRBuilder &builder_, GenContext &context_)
      : builder(builder_), context(context_) {}

  IRValue EmitLValue(ASTNode *node);

  IRValue EmitRValue(ASTNode *node);

  void EmitStmt(ASTNode *node);

  IRBuilder &builder;
  GenContext &context;

  struct detail;
};

std::string GenExpr(ASTNode *node, GenContext &context);
