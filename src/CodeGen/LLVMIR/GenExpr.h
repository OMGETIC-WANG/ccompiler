#pragma once

#include "../../AST/ASTNode.h"
#include "GenContext.h"
#include "IRBuilder.h"

#include <string_view>
#include <unordered_map>

namespace llvm {
class BasicBlock;
} // namespace llvm

class ExprEmitter {
public:
  ExprEmitter(IRBuilder &builder_, GenContext &context_)
      : builder(builder_), context(context_) {}

  LValue EmitLValue(ASTNode *node);

  RValue EmitRValue(ASTNode *node);

  void EmitStmt(ASTNode *node);

  IRBuilder &builder;
  GenContext &context;

  struct detail;

private:
  llvm::BasicBlock *GetOrCreateLabelBlock(std::string_view name);

  std::unordered_map<std::string_view, llvm::BasicBlock *> label_blocks;
};
